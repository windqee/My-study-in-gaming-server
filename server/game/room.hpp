#pragma once

#include "server/game/match_simulation.hpp"

namespace netarena {

struct RoomConfig {
    RoomId room_id;
    MatchId match_id;
    std::size_t max_players{4};
    MatchConfig match;
    MapDefinition map;
    MovementConfig movement;
    std::chrono::seconds finished_hold{15};
};

struct FrameOutcome {
    std::optional<WorldView> world;
    std::vector<DomainEvent> events;
    std::optional<MatchResult> result;
    Phase phase{Phase::Waiting};
    bool phase_changed{};
};

class Room {
public:
    explicit Room(RoomConfig config);

    [[nodiscard]] Result<RoomView> join(PlayerProfile player, Epoch epoch);
    [[nodiscard]] Result<RoomView> set_ready(const PlayerId& player, bool ready);
    [[nodiscard]] Result<FrameOutcome> start(const PlayerId& requester, MonoTime now);
    [[nodiscard]] Result<FrameOutcome> step(MonoTime now, const std::vector<PlayerMove>& moves = {},
                                           const std::vector<AuthorizedPickup>& pickups = {});
    [[nodiscard]] Result<RoomView> leave(const PlayerId& player);
    // Trusted local presence update. Session replacement and expiry belong to the application.
    [[nodiscard]] Result<RoomView> set_connected(const PlayerId& player, bool connected);
    [[nodiscard]] Result<FrameOutcome> maintain(MonoTime now);
    [[nodiscard]] RoomView view() const;
    [[nodiscard]] FrameOutcome current_frame() const;

private:
    struct Member {
        MemberView view;
        Epoch epoch;
    };

    [[nodiscard]] auto find_member(const PlayerId& player) -> std::vector<Member>::iterator;
    [[nodiscard]] std::optional<Error> check_actor(const PlayerId& player, const MatchId* match,
                                                   const Epoch* epoch) const;
    void choose_host();
    void close();
    void revise();

    RoomConfig config_;
    MovementModel movement_;
    std::vector<Member> members_;
    std::optional<MatchSimulation> simulation_;
    Phase phase_{Phase::Waiting};
    std::uint64_t revision_{};
    std::uint64_t next_join_order_{};
    std::optional<MonoTime> last_time_;
    std::optional<MonoTime> finished_until_;
    std::optional<FrameOutcome> pending_closure_;
};

} // namespace netarena
