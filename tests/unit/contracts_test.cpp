#include "shared/contracts/messages.hpp"
#include "shared/contracts/transport.hpp"
#include "check.hpp"
#include <limits>
#include <type_traits>
#include <utility>
using namespace netarena;

template <typename T, typename Variant> struct variant_contains;
template <typename T, typename... U>
struct variant_contains<T, std::variant<U...>> : std::bool_constant<(std::is_same_v<T, U> || ...)> {};
static_assert(!variant_contains<ActorContext, MessagePayload>::value);
static_assert(!variant_contains<SecretToken, MessagePayload>::value);
static_assert(std::has_virtual_destructor_v<ITransport>);
static_assert(std::is_abstract_v<ITransport>);
static_assert(std::is_same_v<decltype(&ITransport::listen), Result<void> (ITransport::*)(const Endpoint&)>);
static_assert(std::is_same_v<decltype(&ITransport::connect), Result<ConnectionId> (ITransport::*)(const Endpoint&)>);
static_assert(std::is_same_v<decltype(&ITransport::poll), Result<std::vector<TransportEvent>> (ITransport::*)(std::size_t, Duration)>);
static_assert(std::is_same_v<decltype(&ITransport::send), Result<SendReceipt> (ITransport::*)(ConnectionId, Channel, ByteView)>);
static_assert(std::is_same_v<decltype(&ITransport::close), Result<void> (ITransport::*)(ConnectionId, CloseReason)>);
static_assert(std::is_same_v<decltype(&ITransport::stats), std::optional<TransportStats> (ITransport::*)(ConnectionId) const>);
template <typename T> concept HasDt = requires(T value) { value.dt; };
static_assert(!HasDt<InputFrame>);

template <typename T>
void check_kind(T payload, MessageKind expected) {
    Envelope envelope{1, "request", std::nullopt, std::nullopt, std::nullopt, std::move(payload)};
    CHECK(envelope.kind() == expected);
}
int main() {
    check_kind(Hello{}, MessageKind::Hello);
    check_kind(Resume{SessionId{"session"}, SecretToken{Bytes{}}, MatchId{"match"}}, MessageKind::Resume);
    check_kind(CreateRoom{}, MessageKind::CreateRoom); check_kind(JoinRoom{}, MessageKind::JoinRoom);
    check_kind(SetReady{}, MessageKind::SetReady); check_kind(StartMatch{}, MessageKind::StartMatch);
    check_kind(LeaveRoom{}, MessageKind::LeaveRoom); check_kind(Move{}, MessageKind::Move);
    check_kind(Pickup{}, MessageKind::Pickup); check_kind(Heartbeat{}, MessageKind::Heartbeat);
    check_kind(RequestFullSnapshot{}, MessageKind::RequestFullSnapshot);
    check_kind(Welcome{}, MessageKind::Welcome); check_kind(RoomUpdate{}, MessageKind::RoomUpdate);
    check_kind(MatchStarted{}, MessageKind::MatchStarted); check_kind(Snapshot{}, MessageKind::Snapshot);
    check_kind(ResumeState{}, MessageKind::ResumeState); check_kind(ActionResult{}, MessageKind::ActionResult);
    check_kind(MatchEnded{}, MessageKind::MatchEnded); check_kind(Pong{}, MessageKind::Pong);
    check_kind(ErrorReply{{ErrorCode::InvalidArgument, "invalid", std::nullopt}}, MessageKind::ErrorReply);
    std::string name = "original";
    MessagePayload owned = Hello{1, name}; name.assign("changed");
    CHECK(std::get<Hello>(owned).display_name == "original");
    Bytes source{std::byte{7}};
    TransportEvent received = Received{{1, 2}, Channel::Input, source};
    source.clear();
    CHECK(std::get<Received>(received).bytes[0] == std::byte{7});
    TransportStats stats;
    CHECK(!stats.rtt); CHECK(!stats.sent_bytes); CHECK(!stats.received_bytes);
    CHECK(!stats.queued_bytes); CHECK(!stats.packet_loss_rate);
    stats.rtt = Duration{12}; CHECK(*stats.rtt == Duration{12});
    const auto infinity = std::numeric_limits<float>::infinity();
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(validate(Vec2{1, -1}).ok());
    CHECK(!validate(Vec2{nan, 0}).ok()); CHECK(!validate(Vec2{0, infinity}).ok());
    CHECK(validate(Rect{{0, 0}, {5, 10}}).ok());
    CHECK(!validate(Rect{{1, 0}, {0, 10}}).ok());
    CHECK(!validate(Rect{{0, 0}, {infinity, 10}}).ok());
    CHECK(!validate(Rect{{0, 0}, {0, 10}}).ok());
    CHECK(validate(Endpoint{"localhost", 7777}).ok());
    CHECK(!validate(Endpoint{"localhost", 0}).ok());
    CHECK(!validate(Endpoint{"", 7777}).ok());
    InputFrame frame{MatchId{"m"}, Epoch{2}, InputSeq{3}, {0, 1}};
    CHECK(validate(frame).ok()); frame.direction.x = nan; CHECK(!validate(frame).ok());
    CHECK(validate(frame).error_if()->field == "direction");
    ResumeState waiting;
    waiting.room.phase = Phase::Waiting;
    CHECK(!waiting.map && !waiting.rules && !waiting.snapshot && !waiting.result);
    ResumeState finished;
    finished.room.phase = Phase::Finished;
    finished.map = MapDefinition{"arena", 1, {{0, 0}, {10, 10}}, {{{2, 2}, {3, 3}}}, {{1, 1}}};
    finished.rules = MatchRules{MovementConfig{4, 0.3F, 30}, Tick{1800}, 0.6F};
    finished.snapshot = MatchSnapshot{};
    finished.snapshot->self_ack = AckState{Epoch{2}, InputSeq{99}};
    finished.result = MatchResult{MatchId{"m"}, ResultReason::TimeUp, {{PlayerId{"p"}, 3}}, {PlayerId{"p"}}};
    MessagePayload recovery = finished;
    finished.map->map_id = "changed";
    const auto& saved = std::get<ResumeState>(recovery);
    CHECK(saved.map->map_id == "arena"); CHECK(saved.map->obstacles.size() == 1);
    CHECK(saved.rules->duration_ticks == Tick{1800});
    CHECK(saved.snapshot->self_ack->terminal_input_seq == InputSeq{99});
    CHECK(saved.result->winners[0] == PlayerId{"p"});
    MemberView member; CHECK(!member.reserve_until);
    Welcome welcome; CHECK(!welcome.recovery_token);
    welcome.recovery_token = SecretToken{Bytes{std::byte{9}}};
    CHECK(welcome.recovery_token->bytes().size() == 1);
    ActionResult result{ActionId{"action"}, ActionStatus::Applied, 42};
    CHECK(result.score == 42);
    Envelope envelope;
    CHECK(envelope.protocol_version == 1);
    envelope.payload = Pong{123}; CHECK(envelope.kind() == MessageKind::Pong);
    std::cout << "contracts: " << checks << " checks passed\n";
}
