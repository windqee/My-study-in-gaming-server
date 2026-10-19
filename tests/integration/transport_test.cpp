#include "adapters/transport/enet_transport.hpp"
#include "check.hpp"
#include <algorithm>
#include <chrono>
#include <enet/enet.h>
#include <thread>
#include <type_traits>
using namespace netarena;
using namespace std::chrono_literals;
static_assert(std::is_final_v<EnetTransport>);
static_assert(!std::is_copy_constructible_v<EnetTransport>);

namespace {
const auto test_deadline = std::chrono::steady_clock::now() + 18s;
template <class T> void error_is(const Result<T> &result, ErrorCode code) {
    CHECK(!result.ok());
    CHECK(result.error_if()->code == code);
}
std::vector<TransportEvent> poll(EnetTransport &transport, std::size_t limit = 32) {
    auto result = transport.poll(limit, 2ms);
    CHECK(result.ok());
    CHECK(result.value_if()->size() <= limit);
    return std::move(*result.value_if());
}
template <class Predicate> void until(Predicate predicate) {
    auto deadline = std::min(test_deadline, std::chrono::steady_clock::now() + 3s);
    while (!predicate()) {
        CHECK(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(1ms);
    }
}
struct Pair {
    EnetTransport server;
    EnetTransport client;
    ConnectionId client_id{}, server_id{};
    explicit Pair(std::size_t packet = 16384, std::size_t queue = 262144)
        : server({TransportRole::Server, 2, packet, queue, 100}),
          client({TransportRole::Client, 2, packet, queue, 100}) {
        CHECK(server.listen({"127.0.0.1", 0}).ok());
        CHECK(server.bound_endpoint()->port != 0);
        connect();
    }
    void connect() {
        auto result = client.connect(*server.bound_endpoint());
        CHECK(result.ok());
        client_id = *result.value_if();
        CHECK(client.stats(client_id).has_value());
        CHECK(!client.stats(client_id)->rtt.has_value());
        error_is(client.send(client_id, Channel::Control, Bytes{std::byte{1}}),
                 ErrorCode::InvalidState);
        bool a = false, b = false;
        until([&] {
            for (const auto &event : poll(client))
                if (const auto *c = std::get_if<Connected>(&event)) {
                    CHECK(c->id == client_id);
                    a = true;
                }
            for (const auto &event : poll(server))
                if (const auto *c = std::get_if<Connected>(&event)) {
                    server_id = c->id;
                    b = true;
                }
            return a && b;
        });
    }
};
void validation() {
    EnetTransport client;
    error_is(client.listen({"127.0.0.1", 0}), ErrorCode::InvalidState);
    error_is(client.connect({"127.0.0.1", 0}), ErrorCode::InvalidArgument);
    error_is(client.connect({"localhost", 1234}), ErrorCode::Unsupported);
    error_is(client.connect({"", 1234}), ErrorCode::InvalidArgument);
    error_is(client.connect({std::string("127.0.0.1\0bad", 13), 1234}), ErrorCode::InvalidArgument);
    error_is(client.poll(0, 1ms), ErrorCode::InvalidArgument);
    error_is(client.poll(1, 0ns), ErrorCode::InvalidArgument);
    error_is(client.poll(1, -1ns), ErrorCode::InvalidArgument);
    CHECK(poll(client).empty());
    EnetTransport server({.role = TransportRole::Server});
    error_is(server.connect({"127.0.0.1", 1234}), ErrorCode::InvalidState);
    CHECK(server.listen({"127.0.0.1", 0}).ok());
    error_is(server.listen({"127.0.0.1", 0}), ErrorCode::InvalidState);
    for (auto config :
         {TransportConfig{TransportRole::Client, 0}, TransportConfig{TransportRole::Client, 8, 0},
          TransportConfig{TransportRole::Client, 8, 10, 0},
          TransportConfig{TransportRole::Client, 8, 10, 10, 0},
          TransportConfig{static_cast<TransportRole>(99)}}) {
        EnetTransport invalid(config);
        error_is(invalid.connect({"127.0.0.1", 1000}), ErrorCode::InvalidArgument);
        error_is(invalid.listen({"127.0.0.1", 0}), ErrorCode::InvalidArgument);
    }
}
void exchange_and_close() {
    Pair pair;
    std::vector<Received> received;
    for (const auto channel : {Channel::Control, Channel::Input, Channel::Snapshot}) {
        Bytes data{std::byte{17}, static_cast<std::byte>(channel)};
        CHECK(pair.client.send(pair.client_id, channel, data).ok());
        data[0] = std::byte{99};
    }
    until([&] {
        poll(pair.client);
        for (auto &event : poll(pair.server, 1))
            if (auto *r = std::get_if<Received>(&event))
                received.push_back(std::move(*r));
        return received.size() == 3;
    });
    for (const auto channel : {Channel::Control, Channel::Input, Channel::Snapshot}) {
        auto it = std::find_if(received.begin(), received.end(),
                               [&](const auto &r) { return r.channel == channel; });
        CHECK(it != received.end());
        CHECK(it->id == pair.server_id);
        CHECK(it->bytes == Bytes({std::byte{17}, static_cast<std::byte>(channel)}));
        CHECK(pair.server.send(pair.server_id, channel, it->bytes).ok());
    }
    std::size_t replies = 0;
    until([&] {
        poll(pair.server);
        for (const auto &e : poll(pair.client))
            if (std::holds_alternative<Received>(e))
                ++replies;
        return replies == 3;
    });
    CHECK(pair.server.stats(pair.server_id)->received_bytes == 6);
    CHECK(!pair.client.stats(pair.client_id)->packet_loss_rate);
    until([&] {
        poll(pair.server);
        poll(pair.client);
        return pair.client.stats(pair.client_id)->queued_bytes == 0;
    });
    CHECK(pair.client.stats(pair.client_id)->rtt.has_value());
    auto old = pair.client_id;
    CHECK(pair.client.close(old, CloseReason::Requested).ok());
    CHECK(pair.client.close(old, CloseReason::Requested).ok());
    bool a = false, b = false;
    until([&] {
        for (const auto &e : poll(pair.client, 1))
            if (const auto *d = std::get_if<Disconnected>(&e)) {
                CHECK(d->id == old);
                CHECK(d->reason == CloseReason::Requested);
                a = true;
            }
        for (const auto &e : poll(pair.server, 1))
            if (const auto *d = std::get_if<Disconnected>(&e)) {
                CHECK(d->id == pair.server_id);
                CHECK(d->reason == CloseReason::RemoteClosed);
                b = true;
            }
        return a && b;
    });
    CHECK(!pair.client.stats(old));
    error_is(pair.client.send(old, Channel::Control, Bytes{std::byte{1}}), ErrorCode::NotFound);
    CHECK(pair.client.close(old, CloseReason::Requested).ok());
    pair.connect();
    CHECK(pair.client_id != old);
    error_is(pair.client.send(old, Channel::Control, Bytes{std::byte{1}}), ErrorCode::NotFound);
}
void limits() {
    Pair pair(128, 256);
    error_is(pair.client.send(pair.client_id, static_cast<Channel>(7), Bytes{std::byte{1}}),
             ErrorCode::InvalidArgument);
    error_is(pair.client.send(pair.client_id, Channel::Control, {}), ErrorCode::InvalidArgument);
    error_is(pair.client.send(pair.client_id, Channel::Control, Bytes(129)),
             ErrorCode::InvalidArgument);
    CHECK(pair.client.send(pair.client_id, Channel::Control, Bytes(128)).ok());
    CHECK(pair.client.send(pair.client_id, Channel::Input, Bytes(128)).ok());
    CHECK(pair.client.stats(pair.client_id)->queued_bytes == 256);
    error_is(pair.client.send(pair.client_id, Channel::Snapshot, Bytes(1)), ErrorCode::QueueFull);
    until([&] {
        poll(pair.client, 1);
        poll(pair.server, 1);
        return pair.client.stats(pair.client_id)->queued_bytes == 0;
    });
    auto begin = std::chrono::steady_clock::now();
    CHECK(pair.client.poll(1000000, 1ns).ok());
    CHECK(std::chrono::steady_clock::now() - begin < 100ms);
}
void timeout_and_close_fallback() {
    // Keep a socket bound without servicing it: no fixed ports or external traffic.
    EnetTransport silent({.role = TransportRole::Server});
    CHECK(silent.listen({"127.0.0.1", 0}).ok());
    EnetTransport client({TransportRole::Client, 1, 16384, 262144, 40});
    auto pending = client.connect(*silent.bound_endpoint());
    CHECK(pending.ok());
    error_is(client.connect(*silent.bound_endpoint()), ErrorCode::QueueFull);
    bool failed = false;
    until([&] {
        for (const auto &e : poll(client, 1))
            if (const auto *f = std::get_if<ConnectFailed>(&e)) {
                CHECK(f->id == *pending.value_if());
                CHECK(f->error.code == ErrorCode::Timeout);
                failed = true;
            }
        return failed;
    });
    CHECK(!client.stats(*pending.value_if()));
    auto next = client.connect(*silent.bound_endpoint());
    CHECK(next.ok());
    CHECK(*next.value_if() != *pending.value_if());
    CHECK(client.close(*next.value_if(), CloseReason::Requested).ok());
    bool closed = false;
    until([&] {
        for (const auto &e : poll(client, 1))
            if (const auto *d = std::get_if<Disconnected>(&e)) {
                CHECK(d->id == *next.value_if());
                closed = true;
            }
        return closed;
    });
    Pair pair;
    CHECK(pair.client.close(pair.client_id, CloseReason::Requested).ok());
    bool fallback = false;
    until([&] {
        for (const auto &e : poll(pair.client, 1))
            if (std::holds_alternative<Disconnected>(e))
                fallback = true;
        return fallback;
    });
}
void limited_channel_peer_is_rejected_safely() {
    EnetTransport server({.role = TransportRole::Server});
    CHECK(server.listen({"127.0.0.1", 0}).ok());
    const std::unique_ptr<ENetHost, decltype(&enet_host_destroy)> raw(
        enet_host_create(nullptr, 1, 1, 0, 0), enet_host_destroy);
    CHECK(raw != nullptr);
    ENetAddress address{};
    address.port = server.bound_endpoint()->port;
    CHECK(enet_address_set_host_ip(&address, "127.0.0.1") == 0);
    CHECK(enet_host_connect(raw.get(), &address, 1, 0) != nullptr);
    std::optional<ConnectionId> id;
    until([&] {
        ENetEvent event{};
        CHECK(enet_host_service(raw.get(), &event, 0) >= 0);
        if (event.type == ENET_EVENT_TYPE_RECEIVE)
            enet_packet_destroy(event.packet);
        for (const auto &e : poll(server))
            if (const auto *connected = std::get_if<Connected>(&e))
                id = connected->id;
        return id.has_value();
    });
    const Bytes payload{std::byte{1}};
    error_is(server.send(*id, Channel::Input, payload), ErrorCode::Unsupported);
    error_is(server.send(*id, Channel::Snapshot, payload), ErrorCode::Unsupported);
    CHECK(server.send(*id, Channel::Control, payload).ok());
}
void wire_channels_and_reliability_match_contract() {
    EnetTransport server({.role = TransportRole::Server});
    CHECK(server.listen({"127.0.0.1", 0}).ok());
    const std::unique_ptr<ENetHost, decltype(&enet_host_destroy)> raw(
        enet_host_create(nullptr, 1, 3, 0, 0), enet_host_destroy);
    CHECK(raw != nullptr);
    ENetAddress address{};
    address.port = server.bound_endpoint()->port;
    CHECK(enet_address_set_host_ip(&address, "127.0.0.1") == 0);
    CHECK(enet_host_connect(raw.get(), &address, 3, 0) != nullptr);
    std::optional<ConnectionId> id;
    until([&] {
        ENetEvent event{};
        CHECK(enet_host_service(raw.get(), &event, 0) >= 0);
        if (event.type == ENET_EVENT_TYPE_RECEIVE)
            enet_packet_destroy(event.packet);
        for (const auto &e : poll(server))
            if (const auto *connected = std::get_if<Connected>(&e))
                id = connected->id;
        return id.has_value();
    });
    for (const auto channel : {Channel::Control, Channel::Input}) {
        for (int i = 0; i < 12; ++i) {
            CHECK(server.send(*id, channel, Bytes{static_cast<std::byte>(i)}).ok());
        }
    }
    const Bytes snapshot(8000, std::byte{42});
    CHECK(server.send(*id, Channel::Snapshot, snapshot).ok());
    std::size_t controls = 0, inputs = 0;
    bool got_snapshot = false;
    until([&] {
        poll(server);
        ENetEvent event{};
        CHECK(enet_host_service(raw.get(), &event, 0) >= 0);
        if (event.type == ENET_EVENT_TYPE_RECEIVE) {
            const std::unique_ptr<ENetPacket, decltype(&enet_packet_destroy)> packet(
                event.packet, enet_packet_destroy);
            CHECK(event.channelID < 3);
            if (event.channelID == 2) {
                CHECK((packet->flags & ENET_PACKET_FLAG_RELIABLE) == 0);
                CHECK(packet->dataLength == snapshot.size());
                CHECK(std::equal(snapshot.begin(), snapshot.end(),
                                 reinterpret_cast<const std::byte *>(packet->data)));
                got_snapshot = true;
            } else {
                CHECK((packet->flags & ENET_PACKET_FLAG_RELIABLE) != 0);
                CHECK(packet->dataLength == 1);
                auto &count = event.channelID == 0 ? controls : inputs;
                CHECK(packet->data[0] == count);
                ++count;
            }
        }
        return controls == 12 && inputs == 12 && got_snapshot;
    });
}
void graceful_close_delivers_final_control_message() {
    Pair pair;
    const Bytes final_message{std::byte{44}, std::byte{45}};
    CHECK(pair.server.send(pair.server_id, Channel::Control, final_message).ok());
    CHECK(pair.server.close(pair.server_id, CloseReason::Requested).ok());
    bool received = false;
    bool disconnected = false;
    bool local_disconnected = false;
    until([&] {
        for (const auto &event : poll(pair.server)) {
            if (std::holds_alternative<Disconnected>(event))
                local_disconnected = true;
        }
        for (const auto &event : poll(pair.client, 1)) {
            if (const auto *packet = std::get_if<Received>(&event)) {
                CHECK(packet->channel == Channel::Control);
                CHECK(packet->bytes == final_message);
                CHECK(!disconnected);
                received = true;
            }
            if (std::holds_alternative<Disconnected>(event)) {
                CHECK(received);
                disconnected = true;
            }
        }
        return received && disconnected && local_disconnected;
    });
}
void tiny_budget_makes_timeout_progress() {
    EnetTransport silent({.role = TransportRole::Server});
    CHECK(silent.listen({"127.0.0.1", 0}).ok());
    EnetTransport client({TransportRole::Client, 2, 128, 256, 10});
    auto first = client.connect(*silent.bound_endpoint());
    auto second = client.connect(*silent.bound_endpoint());
    CHECK(first.ok());
    CHECK(second.ok());
    std::vector<ConnectionId> failures;
    until([&] {
        auto events = client.poll(1, 1ns);
        CHECK(events.ok());
        CHECK(events.value_if()->size() <= 1);
        for (const auto &e : *events.value_if())
            if (const auto *f = std::get_if<ConnectFailed>(&e))
                failures.push_back(f->id);
        return failures.size() == 2;
    });
    CHECK(failures[0] != failures[1]);
}
void fragmented_snapshot() {
    Pair pair;
    Bytes bytes(8000, std::byte{71});
    CHECK(pair.client.send(pair.client_id, Channel::Snapshot, bytes).ok());
    bool received = false;
    until([&] {
        poll(pair.client);
        for (const auto &e : poll(pair.server))
            if (const auto *r = std::get_if<Received>(&e)) {
                CHECK(r->channel == Channel::Snapshot);
                CHECK(r->bytes == bytes);
                received = true;
            }
        return received;
    });
}
} // namespace
int main() {
    wire_channels_and_reliability_match_contract();
    limited_channel_peer_is_rejected_safely();
    graceful_close_delivers_final_control_message();
    tiny_budget_makes_timeout_progress();
    validation();
    exchange_and_close();
    limits();
    timeout_and_close_fallback();
    fragmented_snapshot();
    for (int i = 0; i < 4; ++i) {
        Pair pair;
    }
    std::cout << "transport: " << checks << " checks passed\n";
}
