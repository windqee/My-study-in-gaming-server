#include "adapters/transport/enet_transport.hpp"
#include <algorithm>
#include <chrono>
#include <enet/enet.h>
#include <limits>
#include <mutex>

namespace netarena {
namespace {
std::mutex runtime_mutex;
std::size_t runtime_users{};
Error error(ErrorCode code, const char *message) {
    return {code, message, std::nullopt};
}
Result<ENetAddress> address_for(const Endpoint &endpoint, bool binding) {
    if (endpoint.host.empty() || endpoint.host.find('\0') != std::string::npos ||
        (!binding && !endpoint.port))
        return Result<ENetAddress>::failure(error(ErrorCode::InvalidArgument, "invalid endpoint"));
    if (endpoint.host.size() > 15 ||
        !std::all_of(endpoint.host.begin(), endpoint.host.end(),
                     [](char c) { return (c >= '0' && c <= '9') || c == '.'; }))
        return Result<ENetAddress>::failure(
            error(ErrorCode::Unsupported, "numeric IPv4 required; DNS is not performed"));
    ENetAddress address{};
    address.port = endpoint.port;
    if (enet_address_set_host_ip(&address, endpoint.host.c_str()) != 0)
        return Result<ENetAddress>::failure(
            error(ErrorCode::InvalidArgument, "invalid IPv4 address"));
    return Result<ENetAddress>::success(address);
}
} // namespace
struct EnetTransport::Impl {
    enum class State { Empty, Connecting, Connected, Closing };
    struct Slot {
        ENetPeer *peer{};
        ConnectionId id{};
        State state{State::Empty};
        MonoTime deadline{};
        CloseReason close_reason{CloseReason::Requested};
        std::size_t queued_bytes{};
        std::uint64_t received_bytes{};
    };
    TransportConfig config;
    ENetHost *host{};
    bool initialized{};
    std::optional<Endpoint> bound;
    std::vector<Slot> slots;
    std::uint64_t generation{};
    std::size_t deadline_cursor{};
    explicit Impl(TransportConfig value) : config(value) {
        std::lock_guard lock(runtime_mutex);
        if (runtime_users || enet_initialize() == 0) {
            ++runtime_users;
            initialized = true;
        }
    }
    ~Impl() {
        // Packet free callbacks reference slots, so destroy the host first.
        if (host)
            enet_host_destroy(host);
        std::lock_guard lock(runtime_mutex);
        if (initialized && --runtime_users == 0)
            enet_deinitialize();
    }
    bool valid_config() const {
        return (config.role == TransportRole::Client || config.role == TransportRole::Server) &&
               config.max_connections > 0 &&
               config.max_connections <= ENET_PROTOCOL_MAXIMUM_PEER_ID &&
               config.max_packet_bytes > 0 &&
               config.max_packet_bytes <= ENET_HOST_DEFAULT_MAXIMUM_PACKET_SIZE &&
               config.max_pending_send_bytes_per_connection > 0 && config.connect_timeout_ms > 0;
    }
    Result<void> create_host(const ENetAddress *address) {
        if (!initialized)
            return Result<void>::failure(error(ErrorCode::IoError, "ENet initialization failed"));
        slots.resize(config.max_connections);
        host = enet_host_create(address, config.max_connections, 3, 0, 0);
        if (!host)
            return Result<void>::failure(error(ErrorCode::IoError, "ENet host creation failed"));
        host->maximumPacketSize = config.max_packet_bytes;
        // Bound reassembly/queued receive data per peer as well as individual packets.
        host->maximumWaitingData = config.max_pending_send_bytes_per_connection;
        return Result<void>::success();
    }
    Slot *find(ConnectionId id) {
        if (id.slot >= slots.size())
            return nullptr;
        auto &slot = slots[static_cast<std::size_t>(id.slot)];
        return slot.state != State::Empty && slot.id == id ? &slot : nullptr;
    }
    Slot *allocate(ENetPeer *peer, State state) {
        if (generation == std::numeric_limits<std::uint64_t>::max())
            return nullptr;
        const auto index = static_cast<std::size_t>(peer - host->peers);
        auto &slot = slots[index];
        if (slot.state != State::Empty)
            return nullptr;
        slot = Slot{};
        slot.id = {index, ++generation};
        slot.peer = peer;
        slot.state = state;
        slot.deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(config.connect_timeout_ms);
        return &slot;
    }
    void release(Slot &slot) {
        enet_peer_reset(slot.peer); // Frees queued packets and runs accounting callbacks.
        slot = Slot{};
    }
    static void packet_freed(ENetPacket *packet) {
        auto *slot = static_cast<Slot *>(packet->userData);
        slot->queued_bytes -= packet->dataLength;
    }
};
EnetTransport::EnetTransport(TransportConfig config) : impl_(std::make_unique<Impl>(config)) {}
EnetTransport::~EnetTransport() = default;
Result<void> EnetTransport::listen(const Endpoint &endpoint) {
    auto &self = *impl_;
    if (!self.valid_config())
        return Result<void>::failure(
            error(ErrorCode::InvalidArgument, "invalid transport configuration"));
    if (self.config.role != TransportRole::Server || self.host)
        return Result<void>::failure(
            error(ErrorCode::InvalidState, "listen requires an unbound server"));
    const auto address = address_for(endpoint, true);
    if (!address.ok())
        return Result<void>::failure(*address.error_if());
    const auto created = self.create_host(address.value_if());
    if (!created.ok())
        return Result<void>::failure(*created.error_if());
    ENetAddress actual{};
    if (enet_socket_get_address(self.host->socket, &actual) != 0) {
        enet_host_destroy(self.host);
        self.host = nullptr;
        return Result<void>::failure(error(ErrorCode::IoError, "cannot inspect bound endpoint"));
    }
    self.bound = Endpoint{endpoint.host, actual.port};
    return Result<void>::success();
}
Result<ConnectionId> EnetTransport::connect(const Endpoint &endpoint) {
    auto &self = *impl_;
    if (!self.valid_config())
        return Result<ConnectionId>::failure(
            error(ErrorCode::InvalidArgument, "invalid transport configuration"));
    if (self.config.role != TransportRole::Client)
        return Result<ConnectionId>::failure(
            error(ErrorCode::InvalidState, "connect requires a client"));
    const auto address = address_for(endpoint, false);
    if (!address.ok())
        return Result<ConnectionId>::failure(*address.error_if());
    if (!self.host) {
        const auto created = self.create_host(nullptr);
        if (!created.ok())
            return Result<ConnectionId>::failure(*created.error_if());
    }
    if (self.generation == std::numeric_limits<std::uint64_t>::max())
        return Result<ConnectionId>::failure(
            error(ErrorCode::InvalidState, "connection generation exhausted"));
    auto *peer = enet_host_connect(self.host, address.value_if(), 3, 0);
    if (!peer)
        return Result<ConnectionId>::failure(
            error(ErrorCode::QueueFull, "connection capacity reached"));
    auto *slot = self.allocate(peer, Impl::State::Connecting);
    if (!slot) {
        enet_peer_reset(peer);
        return Result<ConnectionId>::failure(
            error(ErrorCode::QueueFull, "connection capacity reached"));
    }
    return Result<ConnectionId>::success(slot->id);
}
Result<std::vector<TransportEvent>> EnetTransport::poll(std::size_t max_events, Duration budget) {
    auto &self = *impl_;
    if (!max_events || budget <= Duration::zero())
        return Result<std::vector<TransportEvent>>::failure(
            error(ErrorCode::InvalidArgument, "poll requires positive event and time limits"));
    std::vector<TransportEvent> events;
    if (!self.host)
        return Result<std::vector<TransportEvent>>::success(std::move(events));
    const auto started = std::chrono::steady_clock::now();
    const auto within_budget = [&] { return std::chrono::steady_clock::now() - started < budget; };
    // No persistent event cache: leave undispatched ENet events in its bounded queues.
    // Round-robin deadline checks prevent a tiny time budget from starving later peers.
    for (std::size_t checked = 0; checked < self.slots.size(); ++checked) {
        auto &slot = self.slots[self.deadline_cursor];
        self.deadline_cursor = (self.deadline_cursor + 1) % self.slots.size();
        if (slot.state == Impl::State::Connecting && started >= slot.deadline) {
            events.emplace_back(
                ConnectFailed{slot.id, error(ErrorCode::Timeout, "connection timed out")});
            self.release(slot);
        } else if (slot.state == Impl::State::Closing &&
                   (started >= slot.deadline || slot.peer->state == ENET_PEER_STATE_DISCONNECTED)) {
            events.emplace_back(Disconnected{slot.id, slot.close_reason});
            self.release(slot);
        }
        if (events.size() >= max_events || !within_budget())
            break;
    }
    // Bound ignored/malformed events too, independently of emitted event count.
    const auto attempts_limit = std::min<std::size_t>(max_events, 4096) + self.slots.size() + 16;
    for (std::size_t attempt = 0; attempt < attempts_limit && events.size() < max_events;
         ++attempt) {
        if (attempt > 0 && !within_budget())
            break;
        ENetEvent event{};
        const int status = enet_host_service(self.host, &event, 0);
        if (status < 0)
            return Result<std::vector<TransportEvent>>::failure(
                error(ErrorCode::IoError, "ENet service failed"));
        if (!status)
            break;
        const auto index = static_cast<std::size_t>(event.peer - self.host->peers);
        auto &slot = self.slots[index];
        switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT: {
            auto *active = &slot;
            if (slot.state == Impl::State::Empty)
                active = self.allocate(event.peer, Impl::State::Connected);
            if (!active) {
                enet_peer_disconnect_now(event.peer, 0);
                break;
            }
            if (active->state == Impl::State::Closing)
                break;
            active->state = Impl::State::Connected;
            events.emplace_back(Connected{active->id});
            break;
        }
        case ENET_EVENT_TYPE_RECEIVE: {
            // Own the packet even if byte allocation throws or validation rejects it.
            const std::unique_ptr<ENetPacket, decltype(&enet_packet_destroy)> packet(
                event.packet, enet_packet_destroy);
            if (slot.state != Impl::State::Connected || event.channelID >= 3 ||
                packet->dataLength > self.config.max_packet_bytes)
                break;
            Bytes bytes(packet->dataLength);
            if (!bytes.empty())
                std::copy_n(reinterpret_cast<const std::byte *>(packet->data), bytes.size(),
                            bytes.begin());
            slot.received_bytes += bytes.size();
            events.emplace_back(
                Received{slot.id, static_cast<Channel>(event.channelID), std::move(bytes)});
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT:
            if (slot.state == Impl::State::Connecting)
                events.emplace_back(
                    ConnectFailed{slot.id, error(ErrorCode::Timeout, "connection failed")});
            else if (slot.state != Impl::State::Empty)
                events.emplace_back(Disconnected{slot.id, slot.state == Impl::State::Closing
                                                              ? slot.close_reason
                                                              : CloseReason::RemoteClosed});
            if (slot.state != Impl::State::Empty)
                self.release(slot);
            break;
        case ENET_EVENT_TYPE_NONE:
            break;
        }
    }
    enet_host_flush(self.host);
    return Result<std::vector<TransportEvent>>::success(std::move(events));
}
Result<SendReceipt> EnetTransport::send(ConnectionId id, Channel channel, ByteView bytes) {
    auto &self = *impl_;
    auto *slot = self.find(id);
    if (!slot)
        return Result<SendReceipt>::failure(error(ErrorCode::NotFound, "unknown connection"));
    if (slot->state != Impl::State::Connected)
        return Result<SendReceipt>::failure(
            error(ErrorCode::InvalidState, "connection is not open"));
    if (channel != Channel::Control && channel != Channel::Input && channel != Channel::Snapshot)
        return Result<SendReceipt>::failure(error(ErrorCode::InvalidArgument, "unknown channel"));
    if (bytes.empty() || bytes.size() > self.config.max_packet_bytes)
        return Result<SendReceipt>::failure(
            error(ErrorCode::InvalidArgument, "packet size outside configured limits"));
    if (bytes.size() > self.config.max_pending_send_bytes_per_connection - slot->queued_bytes)
        return Result<SendReceipt>::failure(
            error(ErrorCode::QueueFull, "send queue capacity reached"));
    const auto channel_id = static_cast<enet_uint8>(channel);
    if (channel_id >= slot->peer->channelCount) {
        return Result<SendReceipt>::failure(
            error(ErrorCode::Unsupported, "peer did not negotiate this channel"));
    }
    // ENet promotes unreliable packets to reliable on sequence rollover. Reject
    // instead, preserving Snapshot delivery semantics; the caller can reconnect.
    if (channel == Channel::Snapshot &&
        slot->peer->channels[channel_id].outgoingUnreliableSequenceNumber == 0xFFFF)
        return Result<SendReceipt>::failure(
            error(ErrorCode::InvalidState, "snapshot sequence exhausted; reconnect required"));
    const enet_uint32 flags = channel == Channel::Snapshot ? ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT
                                                           : ENET_PACKET_FLAG_RELIABLE;
    auto *packet = enet_packet_create(bytes.data(), bytes.size(), flags);
    if (!packet)
        return Result<SendReceipt>::failure(error(ErrorCode::IoError, "packet allocation failed"));
    slot->queued_bytes += bytes.size();
    packet->userData = slot;
    packet->freeCallback = &Impl::packet_freed;
    if (enet_peer_send(slot->peer, channel_id, packet) != 0) {
        enet_packet_destroy(packet);
        return Result<SendReceipt>::failure(error(ErrorCode::IoError, "ENet send failed"));
    }
    return Result<SendReceipt>::success(SendReceipt{bytes.size()});
}
Result<void> EnetTransport::close(ConnectionId id, CloseReason reason) {
    auto &self = *impl_;
    auto *slot = self.find(id);
    if (!slot || slot->state == Impl::State::Closing)
        return Result<void>::success();
    slot->state = Impl::State::Closing;
    slot->close_reason = reason;
    slot->deadline = std::chrono::steady_clock::now() +
                     std::chrono::milliseconds(self.config.connect_timeout_ms);
    enet_peer_disconnect_later(slot->peer, 0);
    return Result<void>::success();
}
std::optional<TransportStats> EnetTransport::stats(ConnectionId id) const {
    const auto *slot = impl_->find(id);
    if (!slot)
        return std::nullopt;
    TransportStats result;
    // lastReceiveTime is set by ENet's ACK RTT estimator, not merely by receipt.
    if (slot->peer->lastReceiveTime)
        result.rtt = std::chrono::milliseconds(slot->peer->roundTripTime);
    result.queued_bytes = slot->queued_bytes;
    result.received_bytes = slot->received_bytes; // Application payload bytes delivered by poll.
    // ENet only exposes resettable bandwidth counters per peer, not cumulative
    // actual sent bytes, so sent_bytes and packet_loss_rate remain unknown.
    return result;
}
std::optional<Endpoint> EnetTransport::bound_endpoint() const {
    return impl_->bound;
}
} // namespace netarena
