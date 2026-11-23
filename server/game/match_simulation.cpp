#include "server/game/match_simulation.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <tuple>

namespace netarena {
namespace {

Error error(ErrorCode code, const char* message) {
    return {code, message, std::nullopt};
}

bool valid_id(std::string_view value) {
    return !value.empty() && value.size() <= 64;
}

} // namespace

void MatchSimulation::validate_configuration(const MatchConfig& config, const MovementModel& movement) {
    if (!valid_id(config.match_id.value()) || config.duration_ticks == Tick{} ||
        !std::isfinite(config.pickup_radius) || config.pickup_radius <= 0 || config.pickup_radius > 10000 ||
        config.max_actions_per_player == 0 || config.max_actions_per_player > 4096 ||
        config.max_pickups_per_second == 0 || config.max_pickups_per_second > 1000 ||
        config.initial_coins.size() > 64)
        throw std::invalid_argument("invalid match configuration");

    std::set<std::uint32_t> ids;
    const auto bounds = movement.map().bounds;
    for (const auto& coin : config.initial_coins) {
        if (!ids.insert(coin.coin_id).second || !coin.position.finite() ||
            coin.position.x < bounds.min.x || coin.position.x > bounds.max.x ||
            coin.position.y < bounds.min.y || coin.position.y > bounds.max.y)
            throw std::invalid_argument("invalid initial coin");
    }
}

MatchSimulation::MatchSimulation(MatchConfig config, std::vector<PlayerProfile> players,
                                 MovementModel movement)
    : config_(std::move(config)), movement_(std::move(movement)) {
    validate_configuration(config_, movement_);
    const auto& spawns = movement_.map().spawn_points;
    if (players.size() < 2 || players.size() > 4 || players.size() > spawns.size())
        throw std::invalid_argument("invalid match roster size");

    for (std::size_t i = 0; i < players.size(); ++i) {
        const auto& id = players[i].id;
        if (!valid_id(id.value()) ||
            !players_.emplace(id, Participant{PlayerState{id, {spawns[i], {}}, 0, true}, {}, {}}).second)
            throw std::invalid_argument("invalid or duplicated player ID");
    }
    coins_ = std::move(config_.initial_coins);
}

std::optional<Error> MatchSimulation::validate_pickup(const AuthorizedPickup& request) const {
    if (request.command.match_id != config_.match_id)
        return error(ErrorCode::StaleMatch, "pickup belongs to another match");
    if (!players_.contains(request.actor_player_id))
        return error(ErrorCode::NotFound, "unknown pickup player");
    if (!valid_id(request.command.action_id.value()))
        return error(ErrorCode::InvalidArgument, "invalid action ID");
    return std::nullopt;
}

ActionOutcome MatchSimulation::apply_pickup(const AuthorizedPickup& request) {
    auto& player = players_.at(request.actor_player_id);
    const auto& action = request.command.action_id;
    if (const auto found = player.actions.find(action); found != player.actions.end()) {
        auto duplicate = found->second;
        duplicate.disposition = ActionStatus::Duplicate;
        return duplicate;
    }
    if (player.actions.size() >= config_.max_actions_per_player)
        return {action, ActionStatus::Rejected, {}, error(ErrorCode::QueueFull, "action cache full")};

    auto& attempts = player.attempts;
    while (!attempts.empty() && tick_.value() - attempts.front().value() >= movement_.config().tick_hz)
        attempts.pop_front();

    const auto coin = std::find_if(coins_.begin(), coins_.end(), [&](const CoinState& state) {
        return state.coin_id == request.command.coin_id;
    });
    ActionOutcome outcome{action, ActionStatus::Rejected, {}, {}};
    if (!player.state.connected) {
        outcome.error = error(ErrorCode::InvalidState, "pickup player is offline");
    } else if (attempts.size() >= config_.max_pickups_per_second) {
        outcome.error = error(ErrorCode::RateLimited, "pickup rate exceeded");
    } else {
        attempts.push_back(tick_);
        if (coin == coins_.end() || !coin->active) {
            outcome.error = error(ErrorCode::NotFound, "coin unavailable");
        } else if (!movement_.can_pickup(player.state.move.position, coin->position, config_.pickup_radius)) {
            outcome.error = error(ErrorCode::InvalidArgument, "coin out of range");
        } else {
            outcome.disposition = ActionStatus::Applied;
            outcome.score_after = player.state.score + 1;
        }
    }
    player.actions.emplace(action, outcome);
    if (outcome.score_after) {
        coin->active = false;
        player.state.score = *outcome.score_after;
    }
    return outcome;
}

Result<ActionOutcome> MatchSimulation::pickup(const AuthorizedPickup& request) {
    if (result_) return Result<ActionOutcome>::failure(error(ErrorCode::InvalidState, "match finished"));
    if (auto invalid = validate_pickup(request))
        return Result<ActionOutcome>::failure(std::move(*invalid));
    return Result<ActionOutcome>::success(apply_pickup(request));
}

Result<SimulationFrame> MatchSimulation::step(const std::vector<PlayerMove>& moves,
                                              const std::vector<AuthorizedPickup>& pickups) {
    using Frame = Result<SimulationFrame>;
    if (result_) {
        if (!moves.empty() || !pickups.empty())
            return Frame::failure(error(ErrorCode::InvalidState, "match finished"));
        return Frame::success({view(), {}, {}});
    }
    if (moves.size() > players_.size() || pickups.size() > max_pickups_per_tick)
        return Frame::failure(error(ErrorCode::QueueFull, "frame command limit exceeded"));

    std::map<PlayerId, Vec2> directions;
    for (const auto& move : moves) {
        const auto player = players_.find(move.player_id);
        if (player == players_.end())
            return Frame::failure(error(ErrorCode::NotFound, "unknown moving player"));
        if (!player->second.state.connected)
            return Frame::failure(error(ErrorCode::InvalidState, "moving player is offline"));
        Vec2 direction{};
        if (move.frame) {
            if (move.frame->match_id != config_.match_id)
                return Frame::failure(error(ErrorCode::StaleMatch, "move belongs to another match"));
            const auto normalized = movement_.normalize_intent(move.frame->direction);
            if (!normalized.ok()) return Frame::failure(*normalized.error_if());
            direction = *normalized.value_if();
        }
        if (!directions.emplace(move.player_id, direction).second)
            return Frame::failure(error(ErrorCode::InvalidArgument, "two moves for one player"));
    }

    std::vector<const AuthorizedPickup*> ordered;
    ordered.reserve(pickups.size());
    for (const auto& pickup : pickups) {
        if (auto invalid = validate_pickup(pickup)) return Frame::failure(std::move(*invalid));
        ordered.push_back(&pickup);
    }
    // Coin ID breaks ties for conflicting payloads with identical action keys.
    const auto key = [](const AuthorizedPickup* p) {
        return std::tie(p->actor_player_id, p->command.input_seq, p->command.action_id, p->command.coin_id);
    };
    std::sort(ordered.begin(), ordered.end(), [&](const auto* a, const auto* b) { return key(a) < key(b); });

    // A vacated match settles at the last played tick.
    if (!players_.empty()) tick_ = Tick{tick_.value() + 1};
    for (auto& [id, player] : players_) {
        const auto direction = directions.find(id);
        player.state.move = movement_.step(player.state.move, direction == directions.end() ? Vec2{} : direction->second);
    }

    auto events = std::exchange(pending_removals_, {});
    for (const auto* p : ordered)
        events.emplace_back(PickupResolved{tick_, p->actor_player_id, p->command.coin_id, apply_pickup(*p)});

    if (players_.empty())
        settle(ResultReason::Aborted);
    else if (std::none_of(coins_.begin(), coins_.end(), [](const CoinState& coin) { return coin.active; }))
        settle(ResultReason::CoinsExhausted);
    else if (tick_ == config_.duration_ticks)
        settle(ResultReason::TimeUp);

    if (result_) events.emplace_back(MatchFinished{tick_, *result_});
    return Frame::success({view(), std::move(events), result_});
}

void MatchSimulation::settle(ResultReason reason) {
    MatchResult result{config_.match_id, reason, {}, {}};
    std::uint32_t highest{};
    for (auto& [id, player] : players_) {
        player.state.move.velocity = {};
        const auto score = player.state.score;
        result.scores.push_back({id, score});
        highest = std::max(highest, score);
    }
    for (const auto& score : result.scores) {
        if (score.score == highest) result.winners.push_back(score.player_id);
    }
    result_ = std::move(result);
}

Result<void> MatchSimulation::set_connected(const PlayerId& id, bool connected) {
    const auto player = players_.find(id);
    if (player == players_.end()) return Result<void>::failure(error(ErrorCode::NotFound, "unknown player"));
    if (result_) return Result<void>::failure(error(ErrorCode::InvalidState, "match finished"));
    player->second.state.connected = connected;
    if (!connected) player->second.state.move.velocity = {};
    return Result<void>::success();
}

Result<void> MatchSimulation::remove_player(const PlayerId& id) {
    const auto player = players_.find(id);
    if (player == players_.end()) return Result<void>::success();
    if (result_) return Result<void>::failure(error(ErrorCode::InvalidState, "match finished"));
    pending_removals_.emplace_back(PlayerRemoved{tick_, id, player->second.state.score});
    players_.erase(player);
    return Result<void>::success();
}

WorldView MatchSimulation::view() const {
    WorldView world{config_.match_id, tick_, {}, coins_, Tick{config_.duration_ticks.value() - tick_.value()}, result_};
    for (const auto& [id, player] : players_) world.players.push_back(player.state);
    return world;
}

} // namespace netarena
