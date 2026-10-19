#include "shared/protocol/protocol_codec.hpp"
#include "tests/unit/check.hpp"
#include <limits>
#include <nlohmann/json.hpp>
using namespace netarena;
using Json = nlohmann::json;
struct Clock final : IClock {
    mutable int calls{};
    MonoTime time{std::chrono::seconds(100)};
    MonoTime now() const noexcept override {
        ++calls;
        return time;
    }
};
ByteView view(const std::string &s) {
    return {reinterpret_cast<const std::byte *>(s.data()), s.size()};
}
std::string text(const Bytes &b) {
    return {reinterpret_cast<const char *>(b.data()), b.size()};
}
Envelope envelope(MessagePayload payload) {
    Envelope e;
    e.request_id = "request";
    e.payload = std::move(payload);
    if (e.kind() >= MessageKind::CreateRoom && e.kind() <= MessageKind::RequestFullSnapshot) {
        e.session_id = SessionId{"s"};
        e.epoch = Epoch{2};
    }
    if (e.kind() == MessageKind::Move || e.kind() == MessageKind::Pickup)
        e.match_id = MatchId{"m"};
    return e;
}
int main() {
    Clock clock;
    ProtocolCodec codec(clock);
    MapDefinition map{"arena", 1, {{0, 0}, {10, 10}}, {{{2, 2}, {3, 3}}}, {{1, 1}}};
    MatchRules rules{{4, .3F, 30}, Tick{1800}, .6F};
    MatchSnapshot snapshot{MatchId{"m"},        Tick{9},
                           SnapshotSeq{7},      {{PlayerId{"p"}, {{1, 1}, {0, 0}}, 2, true}},
                           {{1, {2, 2}, true}}, Tick{1791},
                           Phase::Running,      AckState{Epoch{2}, InputSeq{5}}};
    RoomView room{RoomId{"r"},
                  MatchId{"m"},
                  Phase::Waiting,
                  4,
                  {{{PlayerId{"p"}, "Alice"},
                    true,
                    false,
                    true,
                    1,
                    clock.time + std::chrono::milliseconds(100)}}};
    MatchResult result{MatchId{"m"}, ResultReason::TimeUp, {{PlayerId{"p"}, 2}}, {PlayerId{"p"}}};
    std::vector<MessagePayload> payloads;
    payloads.emplace_back(Hello{1, "Alice"});
    payloads.emplace_back(
        Resume{SessionId{"s"}, SecretToken{Bytes{std::byte{0x01}, std::byte{0xff}}}, MatchId{"m"}});
    payloads.emplace_back(CreateRoom{});
    payloads.emplace_back(JoinRoom{RoomId{"r"}});
    payloads.emplace_back(SetReady{true});
    payloads.emplace_back(StartMatch{});
    payloads.emplace_back(LeaveRoom{});
    payloads.emplace_back(Move{{MatchId{"m"}, Epoch{2}, InputSeq{0}, {1, 0}}});
    payloads.emplace_back(Pickup{{MatchId{"m"}, Epoch{2}, ActionId{"a"}, InputSeq{5}, 1}});
    payloads.emplace_back(Heartbeat{std::numeric_limits<std::uint64_t>::max()});
    payloads.emplace_back(RequestFullSnapshot{});
    payloads.emplace_back(
        Welcome{{PlayerId{"p"}, SessionId{"s"}}, Epoch{2}, SecretToken{Bytes{std::byte{0xa1}}}});
    payloads.emplace_back(RoomUpdate{room});
    payloads.emplace_back(MatchStarted{map, rules, snapshot});
    payloads.emplace_back(Snapshot{snapshot});
    payloads.emplace_back(ResumeState{room, {}, {}, {}, {}});
    payloads.emplace_back(ActionResult{ActionId{"a"}, ActionStatus::Applied, 2});
    payloads.emplace_back(MatchEnded{result});
    payloads.emplace_back(Pong{42});
    payloads.emplace_back(ErrorReply{{ErrorCode::Unauthorized, "denied", "session_id"}});
    CHECK(payloads.size() == 20);
    for (auto &payload : payloads) {
        auto e = envelope(payload);
        auto direction = e.kind() <= MessageKind::RequestFullSnapshot ? Direction::ClientToServer
                                                                      : Direction::ServerToClient;
        const int calls = clock.calls;
        auto encoded = codec.encode(e, direction);
        CHECK(encoded.ok());
        CHECK(clock.calls == calls + 1);
        auto decoded = codec.decode(*encoded.value_if(), direction);
        CHECK(decoded.ok());
        CHECK(decoded.value_if()->kind() == e.kind());
        auto again = codec.encode(*decoded.value_if(), direction);
        CHECK(again.ok());
        CHECK(*again.value_if() == *encoded.value_if());
        CHECK(codec.validate(e, direction).ok());
        CHECK(!codec
                   .encode(e, direction == Direction::ClientToServer ? Direction::ServerToClient
                                                                     : Direction::ClientToServer)
                   .ok());
    }
    auto hello = envelope(Hello{1, "Alice"});
    auto wire = codec.encode(hello, Direction::ClientToServer);
    CHECK(wire.ok());
    const auto base = Json::parse(text(*wire.value_if()));
    auto bad = [&](const std::string &s, ErrorCode expected = ErrorCode::MalformedPacket) {
        auto r = codec.decode(view(s), Direction::ClientToServer);
        CHECK(!r.ok());
        CHECK(r.error_if()->code == expected);
        CHECK(r.error_if()->message.find("secret-marker") == std::string::npos);
    };
    auto mutate = [&](const auto &fn) {
        auto j = base;
        fn(j);
        bad(j.dump());
    };
    bad("{}");
    bad("[]");
    bad("null");
    bad("{");
    bad("{\"kind\":\"Hello\",\"kind\":\"Hello\"}");
    bad("{\"protocol_version\":1,\"kind\":\"Hello\",\"request_id\":\"r\",\"payload\":{\"version\":"
        "1,\"display_name\":\"x\",\"display_name\":\"y\"}}");
    bad(std::string(40, '[') + std::string(40, ']'));
    bad(std::string(16385, ' '));
    mutate([](auto &j) { j["unknown"] = true; });
    mutate([](auto &j) { j.erase("request_id"); });
    mutate([](auto &j) { j["payload"]["version"] = 1.0; });
    mutate([](auto &j) { j["payload"]["version"] = -1; });
    mutate([](auto &j) { j["payload"]["version"] = 65536; });
    mutate([](auto &j) { j["payload"]["display_name"] = std::string(49, 'x'); });
    mutate([](auto &j) { j["kind"] = "Unknown"; });
    mutate([](auto &j) { j["session_id"] = "s"; });
    mutate([](auto &j) { j["epoch"] = nullptr; });
    auto version = base;
    version["protocol_version"] = 2;
    bad(version.dump(), ErrorCode::ProtocolMismatch);
    std::string utf = text(*wire.value_if());
    utf.insert(utf.find("Alice"), 1, static_cast<char>(0xff));
    bad(utf);
    hello.request_id = std::string(65, 'r');
    CHECK(!codec.validate(hello, Direction::ClientToServer).ok());
    hello.request_id = std::string(64, 'r');
    CHECK(codec.validate(hello, Direction::ClientToServer).ok());
    std::get<Hello>(hello.payload).display_name = std::string(48, 'a');
    CHECK(codec.encode(hello, Direction::ClientToServer).ok());
    std::get<Hello>(hello.payload).display_name = std::string(1, static_cast<char>(0xff));
    CHECK(!codec.encode(hello, Direction::ClientToServer).ok());
    auto move = envelope(Move{{MatchId{"m"}, Epoch{2}, InputSeq{1}, {1, 0}}});
    auto moved = codec.encode(move, Direction::ClientToServer);
    CHECK(moved.ok());
    auto moveJson = Json::parse(text(*moved.value_if()));
    for (const Json &v : {Json(1), Json("01"), Json("-1"), Json("18446744073709551616"),
                          Json("1.0"), Json("+1"), Json(1.0)}) {
        auto j = moveJson;
        j["payload"]["frame"]["seq"] = v;
        bad(j.dump());
    }
    std::get<Move>(move.payload).frame.direction.x = std::numeric_limits<float>::infinity();
    CHECK(!codec.encode(move, Direction::ClientToServer).ok());
    std::get<Move>(move.payload).frame.direction.x = 0;
    move.epoch = Epoch{3};
    CHECK(!codec.validate(move, Direction::ClientToServer).ok());
    auto resume = envelope(Resume{SessionId{"s"}, SecretToken{Bytes{std::byte{1}}}, MatchId{"m"}});
    auto resumeWire = codec.encode(resume, Direction::ClientToServer);
    CHECK(resumeWire.ok());
    for (const auto token : {"", "a", "gg", "secret-marker"}) {
        auto j = Json::parse(text(*resumeWire.value_if()));
        j["payload"]["token"] = token;
        bad(j.dump());
    }
    resume.epoch = Epoch{1};
    CHECK(!codec.encode(resume, Direction::ClientToServer).ok());
    RoomView running = room;
    running.phase = Phase::Running;
    auto recovery = envelope(ResumeState{running, {}, {}, {}, {}});
    CHECK(!codec.validate(recovery, Direction::ServerToClient).ok());
    recovery.payload = ResumeState{running, map, rules, snapshot, {}};
    CHECK(codec.encode(recovery, Direction::ServerToClient).ok());
    auto finished = running;
    finished.phase = Phase::Finished;
    auto finishedSnapshot = snapshot;
    finishedSnapshot.phase = Phase::Finished;
    recovery.payload = ResumeState{finished, map, rules, finishedSnapshot, result};
    CHECK(codec.encode(recovery, Direction::ServerToClient).ok());
    std::get<ResumeState>(recovery.payload).result.reset();
    CHECK(!codec.encode(recovery, Direction::ServerToClient).ok());
    recovery.payload = ResumeState{room, map, {}, {}, {}};
    CHECK(!codec.encode(recovery, Direction::ServerToClient).ok());
    auto closed = room;
    closed.phase = Phase::Closed;
    recovery.payload = ResumeState{closed, {}, {}, {}, {}};
    CHECK(!codec.encode(recovery, Direction::ServerToClient).ok());
    auto roomMsg = envelope(RoomUpdate{room});
    auto &member = std::get<RoomUpdate>(roomMsg.payload).room.members[0];
    member.reserve_until = clock.time + std::chrono::nanoseconds(1);
    auto relative = codec.encode(roomMsg, Direction::ServerToClient);
    CHECK(relative.ok());
    CHECK(Json::parse(text(
              *relative.value_if()))["payload"]["room"]["members"][0]["reserve_remaining_ms"] == 1);
    member.reserve_until = clock.time - std::chrono::seconds(1);
    auto expired = codec.encode(roomMsg, Direction::ServerToClient);
    CHECK(expired.ok());
    CHECK(Json::parse(text(
              *expired.value_if()))["payload"]["room"]["members"][0]["reserve_remaining_ms"] == 0);
    // Structural recovery sets must describe the same players in room, snapshot, result.
    auto foreignResult = result;
    foreignResult.scores[0].player_id = PlayerId{"other"};
    foreignResult.winners = {PlayerId{"other"}};
    recovery.payload = ResumeState{finished, map, rules, finishedSnapshot, foreignResult};
    CHECK(!codec.encode(recovery, Direction::ServerToClient).ok());
    // Extreme monotonic epochs must retain a one-nanosecond positive reservation.
    clock.time = MonoTime::max() - std::chrono::seconds(1);
    member.reserve_until = clock.time + std::chrono::nanoseconds(1);
    auto extreme = codec.encode(roomMsg, Direction::ServerToClient);
    CHECK(extreme.ok());
    CHECK(Json::parse(text(
              *extreme.value_if()))["payload"]["room"]["members"][0]["reserve_remaining_ms"] == 1);
    clock.time = MonoTime{std::chrono::seconds(100)};
    CodecLimits alternateLimits;
    alternateLimits.protocol_version = 2;
    ProtocolCodec alternate(clock, alternateLimits);
    auto alternateHello = envelope(Hello{2, "version test"});
    alternateHello.protocol_version = 2;
    auto alternateWire = alternate.encode(alternateHello, Direction::ClientToServer);
    CHECK(alternateWire.ok());
    auto mismatch = codec.decode(*alternateWire.value_if(), Direction::ClientToServer);
    CHECK(!mismatch.ok() && mismatch.error_if()->code == ErrorCode::ProtocolMismatch);
    CHECK(alternate.decode(*alternateWire.value_if(), Direction::ClientToServer).ok());
    bool threw = false;
    try {
        CodecLimits invalid;
        invalid.protocol_version = 0;
        ProtocolCodec rejected(invalid);
    } catch (const std::invalid_argument &) {
        threw = true;
    }
    CHECK(threw);
    const auto sampleHello = envelope(Hello{1, "Alice"});
    auto sizeWire = codec.encode(sampleHello, Direction::ClientToServer);
    CHECK(sizeWire.ok());
    CodecLimits exactLimits;
    exactLimits.max_packet_bytes = sizeWire.value_if()->size();
    ProtocolCodec exact(clock, exactLimits);
    CHECK(exact.encode(sampleHello, Direction::ClientToServer).ok());
    --exactLimits.max_packet_bytes;
    ProtocolCodec tooSmall(clock, exactLimits);
    CHECK(!tooSmall.encode(sampleHello, Direction::ClientToServer).ok());
    CHECK(!tooSmall.decode(*sizeWire.value_if(), Direction::ClientToServer).ok());
    CodecLimits shallowLimits;
    shallowLimits.max_depth = 2;
    ProtocolCodec shallow(clock, shallowLimits);
    CHECK(shallow.decode(*sizeWire.value_if(), Direction::ClientToServer).ok());
    shallowLimits.max_depth = 1;
    ProtocolCodec tooShallow(clock, shallowLimits);
    CHECK(!tooShallow.decode(*sizeWire.value_if(), Direction::ClientToServer).ok());
    auto oversizedToken =
        envelope(Resume{SessionId{"s"}, SecretToken{Bytes(64, std::byte{0xff})}, MatchId{"m"}});
    CHECK(codec.encode(oversizedToken, Direction::ClientToServer).ok());
    std::get<Resume>(oversizedToken.payload).token = SecretToken{Bytes(65, std::byte{0xff})};
    CHECK(!codec.encode(oversizedToken, Direction::ClientToServer).ok());
    auto limited = envelope(MatchStarted{map, rules, snapshot});
    auto &started = std::get<MatchStarted>(limited.payload);
    started.map.obstacles.resize(32, {{2, 2}, {3, 3}});
    CHECK(codec.encode(limited, Direction::ServerToClient).ok());
    started.map.obstacles.push_back({{2, 2}, {3, 3}});
    CHECK(!codec.encode(limited, Direction::ServerToClient).ok());
    started.map.obstacles.clear();
    started.map.spawn_points.resize(4, {1, 1});
    CHECK(codec.encode(limited, Direction::ServerToClient).ok());
    started.map.spawn_points.push_back({1, 1});
    CHECK(!codec.encode(limited, Direction::ServerToClient).ok());
    started.map.spawn_points.resize(1);
    started.snapshot.players.clear();
    for (int n = 0; n < 4; ++n)
        started.snapshot.players.push_back(
            {PlayerId{"p" + std::to_string(n)}, {{1, 1}, {0, 0}}, 0, true});
    CHECK(codec.encode(limited, Direction::ServerToClient).ok());
    started.snapshot.players.push_back({PlayerId{"extra"}, {{1, 1}, {0, 0}}, 0, true});
    CHECK(!codec.encode(limited, Direction::ServerToClient).ok());
    started.snapshot.players.resize(1);
    started.snapshot.coins.clear();
    for (std::uint32_t n = 0; n < 64; ++n)
        started.snapshot.coins.push_back({n, {1, 1}, true});
    CHECK(codec.encode(limited, Direction::ServerToClient).ok());
    started.snapshot.coins.push_back({64, {1, 1}, true});
    CHECK(!codec.encode(limited, Direction::ServerToClient).ok());
    started.snapshot.coins.resize(2);
    started.snapshot.coins[1].coin_id = 0;
    CHECK(!codec.encode(limited, Direction::ServerToClient).ok());
    started.snapshot.coins.clear();
    started.snapshot.phase = static_cast<Phase>(99);
    CHECK(!codec.encode(limited, Direction::ServerToClient).ok());
    auto noToken = envelope(Welcome{{PlayerId{"p"}, SessionId{"s"}}, Epoch{2}, {}});
    auto noTokenWire = codec.encode(noToken, Direction::ServerToClient);
    CHECK(noTokenWire.ok());
    CHECK(codec.decode(*noTokenWire.value_if(), Direction::ServerToClient).ok());
    auto rejectedAction = envelope(ActionResult{ActionId{"a"}, ActionStatus::Rejected, {}});
    CHECK(codec.encode(rejectedAction, Direction::ServerToClient).ok());
    for (int n = 0; n <= static_cast<int>(ErrorCode::InternalError); ++n) {
        auto error = envelope(ErrorReply{{static_cast<ErrorCode>(n), "bounded", {}}});
        auto output = codec.encode(error, Direction::ServerToClient);
        CHECK(output.ok());
        auto parsed = codec.decode(*output.value_if(), Direction::ServerToClient);
        CHECK(parsed.ok());
        CHECK(std::get<ErrorReply>(parsed.value_if()->payload).error.code ==
              static_cast<ErrorCode>(n));
    }
    auto serverBad = [&](Json j) {
        const auto packet = j.dump();
        auto r = codec.decode(view(packet), Direction::ServerToClient);
        CHECK(!r.ok());
        CHECK(r.error_if()->code == ErrorCode::MalformedPacket);
    };
    auto roomWire = codec.encode(envelope(RoomUpdate{room}), Direction::ServerToClient);
    CHECK(roomWire.ok());
    auto roomJson = Json::parse(text(*roomWire.value_if()));
    auto invalidRoom = roomJson;
    invalidRoom["payload"]["room"]["phase"] = "UNKNOWN";
    serverBad(invalidRoom);
    invalidRoom = roomJson;
    invalidRoom["payload"]["room"]["members"][0]["reserve_remaining_ms"] = 60001;
    serverBad(invalidRoom);
    invalidRoom = roomJson;
    invalidRoom["payload"]["room"]["members"][0]["reserve_remaining_ms"] = nullptr;
    serverBad(invalidRoom);
    invalidRoom = roomJson;
    invalidRoom["payload"]["room"]["members"][0]["ready"] = 1;
    serverBad(invalidRoom);
    auto validSnapshot = codec.encode(envelope(Snapshot{snapshot}), Direction::ServerToClient);
    CHECK(validSnapshot.ok());
    auto snapshotJson = Json::parse(text(*validSnapshot.value_if()));
    auto invalidSnapshot = snapshotJson;
    invalidSnapshot["payload"]["snapshot"]["players"][0]["score"] = 4294967296ULL;
    serverBad(invalidSnapshot);
    invalidSnapshot = snapshotJson;
    invalidSnapshot["payload"]["snapshot"]["players"][0]["move"]["position"]["x"] = 1e100;
    serverBad(invalidSnapshot);
    invalidSnapshot = snapshotJson;
    invalidSnapshot["payload"]["snapshot"]["extra"] = 0;
    serverBad(invalidSnapshot);
    invalidSnapshot = snapshotJson;
    invalidSnapshot["match_id"] = "different";
    serverBad(invalidSnapshot);
    member.reserve_until = clock.time + std::chrono::milliseconds(60000);
    CHECK(codec.encode(roomMsg, Direction::ServerToClient).ok());
    member.reserve_until =
        clock.time + std::chrono::milliseconds(60000) + std::chrono::nanoseconds(1);
    CHECK(!codec.encode(roomMsg, Direction::ServerToClient).ok());
    clock.time = MonoTime::max();
    serverBad(roomJson);
    member.reserve_until = MonoTime::min();
    CHECK(codec.encode(roomMsg, Direction::ServerToClient).ok());
    CHECK(!codec.encode(sampleHello, static_cast<Direction>(99)).ok());
    std::cout << checks << " protocol checks passed\n";
}
