#pragma once

#include "shared/contracts/messages.hpp"

#include <cstddef>

namespace netarena {

struct MatchConfig {
    MatchId match_id;
    Tick duration_ticks{1800};
    std::vector<CoinState> initial_coins;
    float pickup_radius{0.6F};
    std::size_t max_actions_per_player{1024};
    std::size_t max_pickups_per_second{10};
    std::uint64_t seed{};
};

struct PlayerMove {
    PlayerId player_id;
    std::optional<InputFrame> frame;
};

struct AuthorizedPickup {
    PlayerId actor_player_id;
    PickupCommand command;
};

struct WorldView {
    MatchId match_id;
    Tick tick;
    std::vector<PlayerState> players;
    std::vector<CoinState> coins;
    Tick remaining_ticks;
    std::optional<MatchResult> result;
};

struct ActionOutcome {
    ActionId action_id;
    ActionStatus disposition{ActionStatus::Rejected};
    std::optional<std::uint32_t> score_after;
    std::optional<Error> error;
};

struct PickupResolved {
    Tick tick;
    PlayerId player_id;
    std::uint32_t coin_id{};
    ActionOutcome outcome;
};

struct PlayerRemoved {
    Tick tick;
    PlayerId player_id;
    std::uint32_t final_score{};
};

struct MatchFinished {
    Tick tick;
    MatchResult result;
};

// In-memory domain events. Persistent recording has its own future contract.
using DomainEvent = std::variant<PickupResolved, PlayerRemoved, MatchFinished>;

struct SimulationFrame {
    WorldView world;
    std::vector<DomainEvent> events;
    // Present only on the frame that settles the match. world.result persists.
    std::optional<MatchResult> result;
};

} // namespace netarena
