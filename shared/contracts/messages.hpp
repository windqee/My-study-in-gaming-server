#pragma once

#include "shared/foundation/clock.hpp"
#include "shared/foundation/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace netarena {

// Owned in-memory DTOs. Network boundaries must validate lengths, direction,
// phase-dependent fields, identities and envelope consistency in WP-02.
struct Endpoint { std::string host; std::uint16_t port{}; };
struct PlayerProfile { PlayerId id; std::string display_name; };
// Trusted application context only: never a MessagePayload alternative.
struct ActorContext {
    PlayerId player_id;
    SessionId session_id;
    Epoch epoch;
    ConnectionId connection_id;
};
struct InputFrame { MatchId match_id; Epoch epoch; InputSeq seq; Vec2 direction; };
struct PickupCommand {
    MatchId match_id;
    Epoch epoch;
    ActionId action_id;
    InputSeq input_seq;
    std::uint32_t coin_id{};
};
struct MoveState { Vec2 position; Vec2 velocity; };
// Axis-aligned bounds in world units; min < max on both axes.
struct Rect { Vec2 min; Vec2 max; };
struct MapDefinition {
    std::string map_id;
    std::uint32_t revision{};
    Rect bounds;
    std::vector<Rect> obstacles;
    std::vector<Vec2> spawn_points;
};
struct MovementConfig {
    float speed{4.0F};
    float player_radius{0.3F};
    std::uint32_t tick_hz{30};
};
struct PlayerState { PlayerId player_id; MoveState move; std::uint32_t score{}; bool connected{}; };
struct CoinState { std::uint32_t coin_id{}; Vec2 position; bool active{}; };
// Terminal watermark includes explicitly discarded inputs, not only applied ones.
struct AckState { Epoch epoch; InputSeq terminal_input_seq; };
enum class Phase { Waiting, Running, Finished, Closed };
struct MatchSnapshot {
    MatchId match_id;
    Tick tick;
    SnapshotSeq snapshot_seq;
    std::vector<PlayerState> players;
    std::vector<CoinState> coins;
    Tick remaining_ticks;
    Phase phase{Phase::Waiting};
    std::optional<AckState> self_ack;
};
struct MemberView {
    PlayerProfile profile;
    bool ready{};
    bool connected{};
    bool is_host{};
    std::uint64_t join_order{};
    // Local monotonic deadline. A future codec sends relative milliseconds.
    std::optional<MonoTime> reserve_until;
};
struct RoomView {
    RoomId room_id;
    MatchId match_id;
    Phase phase{Phase::Waiting};
    std::uint64_t revision{};
    std::vector<MemberView> members;
};
struct PlayerScore { PlayerId player_id; std::uint32_t score{}; };
enum class ResultReason { TimeUp, CoinsExhausted, Aborted };
struct MatchResult {
    MatchId match_id;
    ResultReason reason{ResultReason::Aborted};
    std::vector<PlayerScore> scores;
    std::vector<PlayerId> winners;
};
struct PublicIdentity { PlayerId player_id; SessionId session_id; };
struct MatchRules { MovementConfig movement; Tick duration_ticks; float pickup_radius{}; };
enum class ActionStatus { Applied, Duplicate, Rejected };

enum class MessageKind {
    Hello, Resume, CreateRoom, JoinRoom, SetReady, StartMatch, LeaveRoom,
    Move, Pickup, Heartbeat, RequestFullSnapshot, Welcome, RoomUpdate,
    MatchStarted, Snapshot, ResumeState, ActionResult, MatchEnded, Pong, ErrorReply
};

struct Hello {
    static constexpr auto kind = MessageKind::Hello;
    std::uint16_t version{1};
    std::string display_name;
};
struct Resume {
    static constexpr auto kind = MessageKind::Resume;
    SessionId session_id;
    SecretToken token;
    MatchId match_id;
};
struct CreateRoom { static constexpr auto kind = MessageKind::CreateRoom; };
struct JoinRoom { static constexpr auto kind = MessageKind::JoinRoom; RoomId room_id; };
struct SetReady { static constexpr auto kind = MessageKind::SetReady; bool ready{}; };
struct StartMatch { static constexpr auto kind = MessageKind::StartMatch; };
struct LeaveRoom { static constexpr auto kind = MessageKind::LeaveRoom; };
struct Move { static constexpr auto kind = MessageKind::Move; InputFrame frame; };
struct Pickup { static constexpr auto kind = MessageKind::Pickup; PickupCommand command; };
struct Heartbeat { static constexpr auto kind = MessageKind::Heartbeat; std::uint64_t nonce{}; };
struct RequestFullSnapshot { static constexpr auto kind = MessageKind::RequestFullSnapshot; };
struct Welcome {
    static constexpr auto kind = MessageKind::Welcome;
    PublicIdentity identity;
    Epoch epoch;
    std::optional<SecretToken> recovery_token;
};
struct RoomUpdate { static constexpr auto kind = MessageKind::RoomUpdate; RoomView room; };
struct MatchStarted {
    static constexpr auto kind = MessageKind::MatchStarted;
    MapDefinition map;
    MatchRules rules;
    MatchSnapshot snapshot;
};
struct Snapshot { static constexpr auto kind = MessageKind::Snapshot; MatchSnapshot snapshot; };
struct ResumeState {
    static constexpr auto kind = MessageKind::ResumeState;
    RoomView room;
    std::optional<MapDefinition> map;
    std::optional<MatchRules> rules;
    std::optional<MatchSnapshot> snapshot;
    std::optional<MatchResult> result;
};
struct ActionResult {
    static constexpr auto kind = MessageKind::ActionResult;
    ActionId action_id;
    ActionStatus status{ActionStatus::Rejected};
    std::optional<std::uint32_t> score;
};
struct MatchEnded { static constexpr auto kind = MessageKind::MatchEnded; MatchResult result; };
struct Pong { static constexpr auto kind = MessageKind::Pong; std::uint64_t nonce{}; };
struct ErrorReply { static constexpr auto kind = MessageKind::ErrorReply; Error error; };

using MessagePayload = std::variant<Hello, Resume, CreateRoom, JoinRoom, SetReady,
    StartMatch, LeaveRoom, Move, Pickup, Heartbeat, RequestFullSnapshot, Welcome,
    RoomUpdate, MatchStarted, Snapshot, ResumeState, ActionResult, MatchEnded,
    Pong, ErrorReply>;

struct Envelope {
    std::uint16_t protocol_version{1};
    std::string request_id;
    std::optional<SessionId> session_id;
    std::optional<Epoch> epoch;
    std::optional<MatchId> match_id;
    MessagePayload payload;

    // Derived from the active alternative: no independently mutable kind field.
    [[nodiscard]] MessageKind kind() const {
        return std::visit([](const auto& message) {
            return std::remove_cvref_t<decltype(message)>::kind;
        }, payload);
    }
};

// Deliberately minimal pure validation, not the future protocol validator.
[[nodiscard]] inline Result<void> validate(const Vec2& value) {
    if (!value.finite()) {
        return Result<void>::failure({ErrorCode::InvalidArgument, "vector must be finite", std::nullopt});
    }
    return Result<void>::success();
}
[[nodiscard]] inline Result<void> validate(const Rect& value) {
    if (!value.min.finite() || !value.max.finite() ||
        value.min.x >= value.max.x || value.min.y >= value.max.y) {
        return Result<void>::failure({ErrorCode::InvalidArgument, "rectangle requires finite ordered bounds", "bounds"});
    }
    return Result<void>::success();
}
[[nodiscard]] inline Result<void> validate(const Endpoint& value) {
    if (value.host.empty() || value.port == 0) {
        return Result<void>::failure({ErrorCode::InvalidArgument, "endpoint requires host and nonzero port",
            value.host.empty() ? "host" : "port"});
    }
    return Result<void>::success();
}
[[nodiscard]] inline Result<void> validate(const InputFrame& value) {
    if (!value.direction.finite()) {
        return Result<void>::failure({ErrorCode::InvalidArgument, "input direction must be finite", "direction"});
    }
    return Result<void>::success();
}

} // namespace netarena
