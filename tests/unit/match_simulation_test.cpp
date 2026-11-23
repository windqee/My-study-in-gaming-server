#include "server/game/match_simulation.hpp"
#include "game_fixtures.hpp"
#include "check.hpp"

using namespace netarena;
using namespace fixtures;

namespace {
const PlayerId alice{"a"};
const PlayerId bob{"b"};
const MatchId match{"match"};

MatchConfig rules() {
    MatchConfig config;
    config.match_id = match;
    config.initial_coins = {{1, {2, 2.5F}, true}, {2, {2, 2}, true}, {3, {15, 15}, true}};
    return config;
}

AuthorizedPickup claim(PlayerId player, const char* action, std::uint32_t coin,
                       std::uint64_t seq = 1) {
    return {std::move(player), {match, Epoch{1}, ActionId{action}, InputSeq{seq}, coin}};
}

PlayerMove move(PlayerId player, Vec2 direction) {
    return {std::move(player), InputFrame{match, Epoch{1}, InputSeq{1}, direction}};
}

void stable_race_and_retries() {
    for (bool swap : {false, true}) {
        MatchSimulation game(rules(), players(), MovementModel(map()));
        auto a = claim(alice, "a-1", 1);
        auto b = claim(bob, "b-1", 1);
        auto frame = game.step({}, swap ? std::vector{a, b} : std::vector{b, a});
        CHECK(frame.ok());
        CHECK(frame.value_if()->events.size() == 2);
        CHECK(std::get<PickupResolved>(frame.value_if()->events[0]).player_id == alice);
        CHECK(frame.value_if()->world.players[0].score == 1);
        CHECK(frame.value_if()->world.players[1].score == 0);

        a.command.coin_id = 2;
        auto retry = game.pickup(a);
        CHECK(retry.ok());
        CHECK(retry.value_if()->disposition == ActionStatus::Duplicate);
        CHECK(retry.value_if()->score_after == 1);
        CHECK(game.view().coins[1].active);

        const auto rejected = game.pickup(b);
        CHECK(rejected.ok());
        CHECK(rejected.value_if()->disposition == ActionStatus::Duplicate);
        CHECK(rejected.value_if()->error->code == ErrorCode::NotFound);
        CHECK(game.pickup(claim(alice, "new", 1)).value_if()->error->code == ErrorCode::NotFound);
    }
}

void movement_then_pickup_and_atomic_batch() {
    auto config = rules();
    config.initial_coins[0].position = {2.7F, 2};
    MatchSimulation game(config, players(), MovementModel(map()));
    auto wrong = move(bob, {1, 0});
    wrong.frame->match_id = MatchId{"old"};
    CHECK(game.step({move(alice, {1, 0}), wrong}, {}).error_if()->code == ErrorCode::StaleMatch);
    CHECK(!game.step({move(alice, {1, 0}), move(alice, {0, 1})}, {}).ok());
    CHECK(!game.step({move(alice, {NAN, 0})}, {}).ok());
    CHECK(game.view().tick == Tick{});
    CHECK(game.view().players[0].move.position.x == 2);
    const auto valid = game.step({move(alice, {1, 0})}, {claim(alice, "first", 1)});
    CHECK(valid.ok());
    CHECK(valid.value_if()->world.players[0].score == 1);
    CHECK(game.step({}, {}).value_if()->world.players[0].move.velocity.x == 0);
}

void bounded_actions_and_rate() {
    auto config = rules();
    config.max_actions_per_player = 4;
    config.max_pickups_per_second = 1;
    MatchSimulation game(config, players(), MovementModel(map(), {4, .3F, 2}));
    auto rejected = game.pickup(claim(alice, "far", 3));
    CHECK(rejected.ok());
    CHECK(rejected.value_if()->disposition == ActionStatus::Rejected);
    CHECK(game.pickup(claim(alice, "limited", 2)).value_if()->error->code == ErrorCode::RateLimited);
    CHECK(game.step({}, {}).ok());
    CHECK(game.step({}, {}).ok());
    CHECK(game.pickup(claim(alice, "allowed", 2)).value_if()->score_after == 1);
    CHECK(game.set_connected(bob, false).ok());
    CHECK(game.pickup(claim(bob, "offline", 1)).value_if()->error->code == ErrorCode::InvalidState);
    CHECK(game.pickup(claim(alice, "fourth", 3)).ok());
    CHECK(game.pickup(claim(alice, "overflow", 3)).value_if()->error->code == ErrorCode::QueueFull);
    CHECK(game.pickup(claim(alice, "far", 2)).value_if()->disposition == ActionStatus::Duplicate);
}

void settlement_and_removal() {
    auto config = rules();
    config.duration_ticks = Tick{1};
    MatchSimulation game(config, players(), MovementModel(map()));
    auto end = game.step({}, {});
    CHECK(end.ok());
    CHECK(end.value_if()->result->reason == ResultReason::TimeUp);
    CHECK(end.value_if()->result->winners.size() == 2);
    CHECK(std::holds_alternative<MatchFinished>(end.value_if()->events.back()));
    auto repeat = game.step({}, {});
    CHECK(repeat.ok());
    CHECK(repeat.value_if()->events.empty());
    CHECK(!repeat.value_if()->result);
    CHECK(repeat.value_if()->world.result);
    CHECK(game.view().tick == Tick{1});
    CHECK(!game.pickup(claim(alice, "late", 2)).ok());

    config.duration_ticks = Tick{10};
    config.initial_coins.resize(1);
    MatchSimulation one_coin(config, players(), MovementModel(map()));
    const auto exhausted = one_coin.step({}, {claim(alice, "only", 1)});
    CHECK(exhausted.value_if()->result->reason == ResultReason::CoinsExhausted);
    CHECK(exhausted.value_if()->result->winners == std::vector{alice});

    MatchSimulation leaving(rules(), players(), MovementModel(map()));
    CHECK(leaving.pickup(claim(alice, "before-leave", 2)).value_if()->score_after == 1);
    CHECK(leaving.remove_player(alice).ok());
    CHECK(leaving.remove_player(alice).ok());
    const auto next = leaving.step({}, {});
    CHECK(next.value_if()->world.players.size() == 1);
    CHECK(std::get<PlayerRemoved>(next.value_if()->events[0]).final_score == 1);
    auto copy = leaving.view();
    copy.players[0].score = 100;
    CHECK(leaving.view().players[0].score == 0);
}

void reject_bad_config() {
    CHECK(throws([] { MatchSimulation game(rules(), {players()[0]}, MovementModel(map())); }));
    auto bad = rules();
    bad.initial_coins[1].coin_id = bad.initial_coins[0].coin_id;
    CHECK(throws([&] { MatchSimulation game(bad, players(), MovementModel(map())); }));
}
}

int main() {
    stable_race_and_retries();
    movement_then_pickup_and_atomic_batch();
    bounded_actions_and_rate();
    settlement_and_removal();
    reject_bad_config();
    std::cout << "match_simulation: " << checks << " checks passed\n";
}
