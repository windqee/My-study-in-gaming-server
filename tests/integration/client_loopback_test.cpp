#include "adapters/transport/enet_transport.hpp"
#include "client/core/interop_client.hpp"
#include "tests/unit/check.hpp"
#include <thread>
using namespace netarena;
int main() {
    EnetTransport server{TransportConfig{.role = TransportRole::Server}};
    CHECK(server.listen({"127.0.0.1", 0}).ok()); CHECK(server.bound_endpoint());
    EnetTransport transport;
    InteropClient client{transport}; ProtocolCodec codec;
    CHECK(client.connect(*server.bound_endpoint(), "Native Client").ok());
    constexpr std::uint64_t nonce = 18446744073709551615ULL;
    bool sent = false, closing = false, received_ping = false, server_closed = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < deadline) {
        auto events = server.poll(16, std::chrono::milliseconds(1)); CHECK(events.ok());
        for (const auto& event : *events.value_if()) {
            if (std::holds_alternative<Disconnected>(event)) server_closed = true;
            const auto* packet = std::get_if<Received>(&event); if (!packet) continue;
            auto decoded = codec.decode(packet->bytes, Direction::ClientToServer); CHECK(decoded.ok());
            Envelope response;
            if (std::holds_alternative<Hello>(decoded.value_if()->payload)) {
                response.payload = Welcome{{PlayerId{"native-player"}, SessionId{"native-session"}}, Epoch{7}, std::nullopt};
            } else {
                const auto* heartbeat = std::get_if<Heartbeat>(&decoded.value_if()->payload);
                CHECK(heartbeat); CHECK(heartbeat->nonce == nonce);
                CHECK(decoded.value_if()->session_id == SessionId{"native-session"});
                CHECK(decoded.value_if()->epoch == Epoch{7});
                received_ping = true; response.payload = Pong{heartbeat->nonce};
            }
            auto bytes = codec.encode(response, Direction::ServerToClient); CHECK(bytes.ok());
            CHECK(server.send(packet->id, Channel::Control, *bytes.value_if()).ok());
        }
        CHECK(client.update().ok());
        if (client.view().state == ClientState::Ready && !sent) {
            CHECK(client.send_ping(nonce).ok()); sent = true;
        }
        if (client.view().last_pong == nonce && !closing) { CHECK(client.disconnect().ok()); closing = true; }
        if (closing && client.view().state == ClientState::Disconnected && server_closed) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(received_ping); CHECK(client.view().last_pong == nonce);
    CHECK(client.view().state == ClientState::Disconnected); CHECK(server_closed);
    CHECK(client.connect(*server.bound_endpoint(), "Version probe", 2).ok());
    bool rejected = false;
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < deadline) {
        auto events = server.poll(16, std::chrono::milliseconds(1)); CHECK(events.ok());
        for (const auto& event : *events.value_if()) {
            const auto* packet = std::get_if<Received>(&event); if (!packet) continue;
            auto decoded = codec.decode(packet->bytes, Direction::ClientToServer);
            CHECK(!decoded.ok()); CHECK(decoded.error_if()->code == ErrorCode::ProtocolMismatch);
            Envelope response; response.payload = ErrorReply{{ErrorCode::ProtocolMismatch, "unsupported version", std::nullopt}};
            auto bytes = codec.encode(response, Direction::ServerToClient); CHECK(bytes.ok());
            CHECK(server.send(packet->id, Channel::Control, *bytes.value_if()).ok());
            CHECK(server.close(packet->id, CloseReason::ProtocolError).ok()); rejected = true;
        }
        (void)client.update();
        if (client.view().state == ClientState::Failed) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(rejected); CHECK(client.view().state == ClientState::Failed);
    CHECK(client.view().error); CHECK(client.view().error->code == ErrorCode::ProtocolMismatch);
    std::cout << "native client loopback checks: " << checks << '\n';
}
