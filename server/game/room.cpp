#include "server/game/room.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace netarena {
namespace {

Error error(ErrorCode code, const char* message) {
    return {code, message, std::nullopt};
}

bool valid_text(std::string_view text, std::size_t max_bytes) {
    return !text.empty() && text.size() <= max_bytes;
}

} // namespace

Room::Room(RoomConfig config)
    : config_(std::move(config)), movement_(std::move(config_.map), config_.movement) {
    if (!valid_text(config_.room_id.value(), 64) || config_.match_id != config_.match.match_id ||
        config_.max_players < 2 || config_.max_players > 4 ||
        config_.max_players > movement_.map().spawn_points.size() ||
        config_.finished_hold < std::chrono::seconds::zero() || config_.finished_hold > std::chrono::seconds{15})
        throw std::invalid_argument("invalid room configuration");
    MatchSimulation::validate_configuration(config_.match, movement_);
    members_.reserve(config_.max_players);
}

auto Room::find_member(const PlayerId& player) -> std::vector<Member>::iterator {
    return std::find_if(members_.begin(), members_.end(), [&](const Member& member) {
        return member.view.profile.id == player;
    });
}

void Room::revise() {
    if (revision_ < std::numeric_limits<std::uint64_t>::max()) ++revision_;
}

void Room::close() {
    phase_ = Phase::Closed;
    members_.clear();
    simulation_.reset();
    finished_until_.reset();
}

void Room::choose_host() {
    auto host = std::find_if(members_.begin(), members_.end(), [](const Member& member) {
        return member.view.is_host;
    });
    if (host != members_.end() && host->view.connected) return;
    if (host != members_.end()) host->view.is_host = false;
    // members_ retains insertion order after leave.
    auto first = std::find_if(members_.begin(), members_.end(), [](const Member& member) {
        return member.view.connected;
    });
    if (first != members_.end()) first->view.is_host = true;
    else if (phase_ == Phase::Waiting) close();
}

Result<RoomView> Room::join(PlayerProfile player, Epoch epoch) {
    using View = Result<RoomView>;
    if (phase_ != Phase::Waiting) return View::failure(error(ErrorCode::InvalidState, "room is not waiting"));
    if (!valid_text(player.id.value(), 64) || !valid_text(player.display_name, 48) || epoch == Epoch{})
        return View::failure(error(ErrorCode::InvalidArgument, "invalid player profile or epoch"));
    if (const auto member = find_member(player.id); member != members_.end()) {
        if (member->epoch != epoch) return View::failure(error(ErrorCode::StaleEpoch, "member epoch differs"));
        return View::success(view());
    }
    if (members_.size() >= config_.max_players) return View::failure(error(ErrorCode::RoomFull, "room is full"));
    if (next_join_order_ == std::numeric_limits<std::uint64_t>::max())
        return View::failure(error(ErrorCode::InvalidState, "join order exhausted"));
    const bool is_host = members_.empty();
    members_.push_back({{std::move(player), false, true, is_host, ++next_join_order_, {}}, epoch});
    revise();
    return View::success(view());
}

Result<RoomView> Room::set_ready(const PlayerId& id, bool ready) {
    using View = Result<RoomView>;
    if (phase_ != Phase::Waiting) return View::failure(error(ErrorCode::InvalidState, "room is not waiting"));
    const auto member = find_member(id);
    if (member == members_.end()) return View::failure(error(ErrorCode::NotFound, "unknown room member"));
    if (!member->view.connected) return View::failure(error(ErrorCode::InvalidState, "member is offline"));
    if (member->view.ready != ready) {
        member->view.ready = ready;
        revise();
    }
    return View::success(view());
}

Result<FrameOutcome> Room::start(const PlayerId& requester, MonoTime now) {
    using Frame = Result<FrameOutcome>;
    if (phase_ != Phase::Waiting) return Frame::failure(error(ErrorCode::InvalidState, "room is not waiting"));
    const auto host = find_member(requester);
    if (host == members_.end() || !host->view.is_host)
        return Frame::failure(error(ErrorCode::Unauthorized, "only the host can start"));
    if (members_.size() < 2 || std::any_of(members_.begin(), members_.end(), [](const Member& member) {
            return !member.view.connected || !member.view.ready;
        })) return Frame::failure(error(ErrorCode::InvalidState, "at least two online ready members are required"));
    if (last_time_ && now < *last_time_)
        return Frame::failure(error(ErrorCode::InvalidArgument, "room time moved backward"));

    std::vector<PlayerProfile> players;
    for (const auto& member : members_) players.push_back(member.view.profile);
    simulation_.emplace(config_.match, std::move(players), movement_);
    last_time_ = now;
    phase_ = Phase::Running;
    revise();
    auto frame = current_frame();
    frame.phase_changed = true;
    return Frame::success(std::move(frame));
}

std::optional<Error> Room::check_actor(const PlayerId& id, const MatchId* match, const Epoch* epoch) const {
    if (match && *match != config_.match_id) return error(ErrorCode::StaleMatch, "command belongs to another match");
    const auto member = std::find_if(members_.begin(), members_.end(), [&](const Member& entry) {
        return entry.view.profile.id == id;
    });
    if (member == members_.end()) return error(ErrorCode::NotFound, "unknown room member");
    if (epoch && *epoch != member->epoch) return error(ErrorCode::StaleEpoch, "command epoch differs");
    if (!member->view.connected) return error(ErrorCode::InvalidState, "member is offline");
    return std::nullopt;
}

Result<FrameOutcome> Room::step(MonoTime now, const std::vector<PlayerMove>& moves,
                                const std::vector<AuthorizedPickup>& pickups) {
    using Frame = Result<FrameOutcome>;
    if (phase_ != Phase::Running) {
        if (!moves.empty() || !pickups.empty())
            return Frame::failure(error(ErrorCode::InvalidState, "room is not running"));
        return maintain(now);
    }
    if (last_time_ && now < *last_time_)
        return Frame::failure(error(ErrorCode::InvalidArgument, "room time moved backward"));
    if (moves.size() > config_.max_players || pickups.size() > MatchSimulation::max_pickups_per_tick)
        return Frame::failure(error(ErrorCode::QueueFull, "frame command limit exceeded"));

    for (const auto& move : moves) {
        const auto* input = move.frame ? &*move.frame : nullptr;
        if (auto invalid = check_actor(move.player_id, input ? &input->match_id : nullptr,
                                       input ? &input->epoch : nullptr))
            return Frame::failure(std::move(*invalid));
    }
    for (const auto& pickup : pickups) {
        if (auto invalid = check_actor(pickup.actor_player_id, &pickup.command.match_id, &pickup.command.epoch))
            return Frame::failure(std::move(*invalid));
    }

    auto simulated = simulation_->step(moves, pickups);
    if (!simulated.ok()) return Frame::failure(*simulated.error_if());
    auto& frame = *simulated.value_if();
    last_time_ = now;
    const bool finished = frame.result.has_value();
    if (finished) {
        phase_ = Phase::Finished;
        const auto hold = std::chrono::duration_cast<MonoTime::duration>(config_.finished_hold);
        finished_until_ = now > MonoTime::max() - hold ? MonoTime::max() : now + hold;
        revise();
    }
    return Frame::success({std::move(frame.world), std::move(frame.events), std::move(frame.result), phase_, finished});
}

Result<RoomView> Room::leave(const PlayerId& id) {
    using View = Result<RoomView>;
    const auto member = find_member(id);
    if (member == members_.end()) return View::success(view());
    if (phase_ == Phase::Running) {
        const auto removed = simulation_->remove_player(id);
        if (!removed.ok()) return View::failure(*removed.error_if());
        if (members_.size() == 1) {
            // Drain queued departures and settle before close releases simulation.
            auto closure = simulation_->step({}, {});
            if (!closure.ok()) return View::failure(*closure.error_if());
            auto& frame = *closure.value_if();
            pending_closure_ = FrameOutcome{
                {}, std::move(frame.events), std::move(frame.result), Phase::Closed, true};
        }
    }
    members_.erase(member);
    if (members_.empty()) close();
    else choose_host();
    revise();
    return View::success(view());
}

Result<RoomView> Room::set_connected(const PlayerId& id, bool connected) {
    using View = Result<RoomView>;
    if (phase_ == Phase::Closed) return View::failure(error(ErrorCode::InvalidState, "room is closed"));
    const auto member = find_member(id);
    if (member == members_.end()) return View::failure(error(ErrorCode::NotFound, "unknown room member"));
    if (member->view.connected == connected) return View::success(view());
    if (phase_ == Phase::Running) {
        const auto updated = simulation_->set_connected(id, connected);
        if (!updated.ok()) return View::failure(*updated.error_if());
    }
    member->view.connected = connected;
    if (!connected) member->view.ready = false;
    choose_host();
    revise();
    return View::success(view());
}

Result<FrameOutcome> Room::maintain(MonoTime now) {
    using Frame = Result<FrameOutcome>;
    if (last_time_ && now < *last_time_)
        return Frame::failure(error(ErrorCode::InvalidArgument, "room time moved backward"));
    last_time_ = now;
    if (auto closure = std::exchange(pending_closure_, std::nullopt))
        return Frame::success(std::move(*closure));

    const bool expired = finished_until_ && now >= *finished_until_;
    if (expired) {
        close();
        revise();
    }
    auto frame = current_frame();
    frame.phase_changed = expired;
    return Frame::success(std::move(frame));
}

RoomView Room::view() const {
    RoomView view{config_.room_id, config_.match_id, phase_, revision_, {}};
    for (const auto& member : members_) view.members.push_back(member.view);
    return view;
}

FrameOutcome Room::current_frame() const {
    return {simulation_ ? std::optional{simulation_->view()} : std::nullopt, {}, {}, phase_, false};
}

} // namespace netarena
