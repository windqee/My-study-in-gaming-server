#pragma once

#include "server/game/domain_types.hpp"
#include "shared/movement/movement_model.hpp"

#include <deque>
#include <map>

namespace netarena {

class MatchSimulation {
public:
    static constexpr std::size_t max_pickups_per_tick = 64;

    MatchSimulation(MatchConfig config, std::vector<PlayerProfile> players, MovementModel movement);
    static void validate_configuration(const MatchConfig& config, const MovementModel& movement);

    // Caller supplies trusted player identities. Room validates the session epoch.
    [[nodiscard]] Result<SimulationFrame> step(const std::vector<PlayerMove>& moves,
                                               const std::vector<AuthorizedPickup>& pickups);
    // Direct rule entry point returns only the outcome. step publishes events and
    // evaluates settlement. Game callers should normally use the batched entry.
    [[nodiscard]] Result<ActionOutcome> pickup(const AuthorizedPickup& request);
    [[nodiscard]] Result<void> set_connected(const PlayerId& player, bool connected);
    [[nodiscard]] Result<void> remove_player(const PlayerId& player);
    [[nodiscard]] WorldView view() const;

private:
    struct Participant {
        PlayerState state;
        std::map<ActionId, ActionOutcome> actions;
        std::deque<Tick> attempts;
    };

    [[nodiscard]] std::optional<Error> validate_pickup(const AuthorizedPickup& request) const;
    [[nodiscard]] ActionOutcome apply_pickup(const AuthorizedPickup& request);
    void settle(ResultReason reason);

    MatchConfig config_;
    MovementModel movement_;
    Tick tick_;
    std::map<PlayerId, Participant> players_;
    std::vector<CoinState> coins_;
    std::vector<DomainEvent> pending_removals_;
    std::optional<MatchResult> result_;
};

} // namespace netarena
