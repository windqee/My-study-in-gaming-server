#include "adapters/transport/enet_transport.hpp"
#include "shared/protocol/protocol_codec.hpp"
#include <charconv>
#include <csignal>
#include <iostream>
#include <map>
#include <thread>
namespace {
volatile std::sig_atomic_t stopping = 0;
void stop(int) { stopping = 1; }
bool number(const char* text, unsigned& value) {
    std::string_view input{text}; auto parsed = std::from_chars(input.data(), input.data() + input.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == input.data() + input.size();
}
}
int main(int argc, char** argv) {
    using namespace netarena;
    unsigned port = 9000, seconds = 60;
    for (int i = 1; i < argc; ++i) {
        std::string_view option{argv[i]};
        if (++i >= argc || (option != "--port" && option != "--max-seconds")) return 2;
        if (!number(argv[i], option == "--port" ? port : seconds)) return 2;
    }
    if (port > 65535 || seconds == 0 || seconds > 3600) return 2;
    std::signal(SIGINT, stop); std::signal(SIGTERM, stop);
    EnetTransport transport{TransportConfig{.role = TransportRole::Server}};
    ProtocolCodec codec;
    if (!transport.listen({"127.0.0.1", static_cast<std::uint16_t>(port)}).ok()) {
        std::cerr << "LISTEN_FAILED\n"; return 1;
    }
    const auto bound = transport.bound_endpoint(); if (!bound) return 1;
    std::cout << "READY port=" << bound->port << std::endl;
    struct Peer { std::optional<PublicIdentity> identity; bool closing{}; };
    std::map<ConnectionId, Peer> peers;
    auto send = [&](ConnectionId id, Envelope reply) {
        auto packet = codec.encode(reply, Direction::ServerToClient);
        return packet.ok() && transport.send(id, Channel::Control, *packet.value_if()).ok();
    };
    auto reject = [&](ConnectionId id, ErrorCode code) {
        Envelope reply; reply.payload = ErrorReply{{code,
            code == ErrorCode::ProtocolMismatch ? "unsupported protocol version" : "request rejected", std::nullopt}};
        (void)send(id, std::move(reply));
        // ENet close is graceful: its accepted reliable control queue is drained.
        (void)transport.close(id, CloseReason::ProtocolError);
        peers[id].closing = true;
        if (code == ErrorCode::ProtocolMismatch) std::cout << "PROTOCOL_MISMATCH" << std::endl;
    };
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (!stopping && std::chrono::steady_clock::now() < end) {
        auto events = transport.poll(64, std::chrono::milliseconds(2));
        if (!events.ok()) { std::cerr << "POLL_FAILED\n"; return 1; }
        for (const auto& event : *events.value_if()) {
            if (const auto* connected = std::get_if<Connected>(&event)) {
                if (peers.size() >= 8) { (void)transport.close(connected->id, CloseReason::Requested); continue; }
                peers.emplace(connected->id, Peer{}); std::cout << "CONNECTED" << std::endl;
            } else if (const auto* disconnected = std::get_if<Disconnected>(&event)) {
                peers.erase(disconnected->id); std::cout << "DISCONNECTED" << std::endl;
            } else if (const auto* received = std::get_if<Received>(&event)) {
                auto peer = peers.find(received->id);
                if (peer == peers.end() || peer->second.closing) continue;
                if (received->channel != Channel::Control) { reject(received->id, ErrorCode::MalformedPacket); continue; }
                auto decoded = codec.decode(received->bytes, Direction::ClientToServer);
                if (!decoded.ok()) { reject(received->id, decoded.error_if()->code); continue; }
                const auto& request = *decoded.value_if();
                if (std::holds_alternative<Hello>(request.payload)) {
                    if (peer->second.identity) { reject(received->id, ErrorCode::InvalidState); continue; }
                    const auto suffix = std::to_string(received->id.slot) + "-" + std::to_string(received->id.generation);
                    PublicIdentity identity{PlayerId{"player-" + suffix}, SessionId{"session-" + suffix}};
                    Envelope reply; reply.request_id = request.request_id;
                    reply.payload = Welcome{identity, Epoch{1}, std::nullopt};
                    if (!send(received->id, std::move(reply))) { reject(received->id, ErrorCode::QueueFull); continue; }
                    peer->second.identity = std::move(identity); std::cout << "WELCOME" << std::endl;
                } else if (const auto* ping = std::get_if<Heartbeat>(&request.payload)) {
                    if (!peer->second.identity || request.session_id != peer->second.identity->session_id || request.epoch != Epoch{1}) {
                        reject(received->id, ErrorCode::InvalidState); continue;
                    }
                    Envelope reply; reply.request_id = request.request_id; reply.payload = Pong{ping->nonce};
                    if (!send(received->id, std::move(reply))) { reject(received->id, ErrorCode::QueueFull); continue; }
                    std::cout << "PONG" << std::endl;
                } else reject(received->id, ErrorCode::Unsupported);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for (const auto& [id, peer] : peers) { (void)peer; (void)transport.close(id, CloseReason::Requested); }
}
