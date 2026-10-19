#include "client/core/interop_client.hpp"
#include "check.hpp"
#include <deque>
using namespace netarena;
class FakeTransport final : public ITransport {
public:
    ConnectionId id{1, 1}; int connects{}, closes{};
    std::deque<TransportEvent> events; std::vector<Bytes> sent;
    Result<void> listen(const Endpoint&) override { return Result<void>::success(); }
    Result<ConnectionId> connect(const Endpoint&) override { ++connects; return Result<ConnectionId>::success(id); }
    Result<std::vector<TransportEvent>> poll(std::size_t max, Duration) override {
        std::vector<TransportEvent> out;
        while (!events.empty() && out.size() < max) { out.push_back(std::move(events.front())); events.pop_front(); }
        return Result<std::vector<TransportEvent>>::success(std::move(out));
    }
    Result<SendReceipt> send(ConnectionId, Channel channel, ByteView bytes) override {
        CHECK(channel == Channel::Control); sent.emplace_back(bytes.begin(), bytes.end());
        return Result<SendReceipt>::success({bytes.size()});
    }
    Result<void> close(ConnectionId, CloseReason) override { ++closes; return Result<void>::success(); }
    std::optional<TransportStats> stats(ConnectionId) const override { return std::nullopt; }
    void receive(Envelope envelope) {
        auto encoded = ProtocolCodec{}.encode(envelope, Direction::ServerToClient); CHECK(encoded.ok());
        events.push_back(Received{id, Channel::Control, *encoded.value_if()});
    }
};
int main() {
    FakeTransport transport; InteropClient client{transport};
    CHECK(!client.connect({"127.0.0.1", 0}, "Alice").ok()); CHECK(transport.connects == 0);
    CHECK(!client.connect({"127.0.0.1", 99}, std::string(49, 'a')).ok()); CHECK(transport.connects == 0);
    CHECK(!client.send_ping(1).ok());
    CHECK(client.connect({"127.0.0.1", 99}, "Alice").ok());
    CHECK(client.view().state == ClientState::Connecting); CHECK(transport.sent.empty());
    transport.events.push_back(Connected{transport.id}); CHECK(client.update().ok());
    CHECK(client.view().state == ClientState::Handshaking); CHECK(transport.sent.size() == 1);
    auto hello = ProtocolCodec{}.decode(transport.sent[0], Direction::ClientToServer); CHECK(hello.ok());
    CHECK(std::get<Hello>(hello.value_if()->payload).display_name == "Alice");
    CHECK(!client.send_ping(2).ok());
    Envelope welcome; welcome.payload = Welcome{{PlayerId{"player-1"}, SessionId{"session-1"}}, Epoch{1}, std::nullopt};
    transport.receive(welcome); CHECK(client.update().ok()); CHECK(client.view().state == ClientState::Ready);
    CHECK(client.view().identity->player_id == PlayerId{"player-1"});
    constexpr std::uint64_t nonce = 9007199254740993ULL;
    CHECK(client.send_ping(nonce).ok());
    CHECK(!client.send_ping(nonce + 1).ok()); // At most one pending ping.
    auto ping = ProtocolCodec{}.decode(transport.sent.back(), Direction::ClientToServer); CHECK(ping.ok());
    CHECK(ping.value_if()->session_id == SessionId{"session-1"}); CHECK(ping.value_if()->epoch == Epoch{1});
    CHECK(std::get<Heartbeat>(ping.value_if()->payload).nonce == nonce);
    Envelope pong; pong.payload = Pong{nonce}; transport.receive(pong); CHECK(client.update().ok());
    CHECK(client.view().last_pong == nonce);
    CHECK(client.disconnect().ok()); CHECK(client.view().state == ClientState::Closing);
    transport.events.push_back(Disconnected{transport.id, CloseReason::Requested}); CHECK(client.update().ok());
    CHECK(client.view().state == ClientState::Disconnected); CHECK(!client.view().identity);
    CHECK(client.connect({"127.0.0.1", 99}, "Alice", 2).ok());
    transport.events.push_back(Connected{transport.id}); CHECK(client.update().ok());
    Envelope rejected; rejected.payload = ErrorReply{{ErrorCode::ProtocolMismatch, "unsupported version", std::nullopt}};
    transport.receive(rejected); CHECK(!client.update().ok());
    CHECK(client.view().state == ClientState::Failed); CHECK(client.view().error->code == ErrorCode::ProtocolMismatch);
    transport.events.push_back(Disconnected{transport.id, CloseReason::ProtocolError}); (void)client.update();
    CHECK(client.view().state == ClientState::Failed); CHECK(client.view().error->code == ErrorCode::ProtocolMismatch);
    FakeTransport absent; InteropClient timeout{absent}; CHECK(timeout.connect({"127.0.0.1", 99}, "Alice").ok());
    absent.events.push_back(ConnectFailed{absent.id, {ErrorCode::Timeout, "sensitive backend message", std::nullopt}});
    CHECK(!timeout.update().ok()); CHECK(timeout.view().error->code == ErrorCode::Timeout);
    CHECK(timeout.view().error->message.find("sensitive") == std::string::npos);
    std::cout << "interop client checks: " << checks << '\n';
}
