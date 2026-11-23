#include "server/game/room.hpp"
#include "game_fixtures.hpp"
#include "check.hpp"

using namespace netarena;
using namespace fixtures;
using namespace std::chrono_literals;

namespace {
const PlayerId alice{"a"};
const PlayerId bob{"b"};
const PlayerId carol{"c"};
const MatchId match{"match"};
const MonoTime now{};

RoomConfig config() {
    RoomConfig result;
    result.room_id = RoomId{"room"};
    result.match_id = match;
    result.match.match_id = match;
    result.match.duration_ticks = Tick{2};
    result.match.initial_coins = {{1, {2, 2.5F}, true}, {2, {18, 18}, true}};
    result.map = map();
    return result;
}

void join_pair(Room& room) {
    CHECK(room.join({alice, "Alice"}, Epoch{2}).ok());
    CHECK(room.join({bob, "Bob"}, Epoch{3}).ok());
}

void ready_pair(Room& room) {
    CHECK(room.set_ready(alice, true).ok());
    CHECK(room.set_ready(bob, true).ok());
}

void admission_and_start() {
    Room room(config());
    CHECK(!room.current_frame().world);
    CHECK(room.join({alice, "Alice"}, Epoch{2}).ok());
    CHECK(room.set_ready(alice, true).ok());
    CHECK(room.start(alice, now).error_if()->code == ErrorCode::InvalidState);
    CHECK(room.join({bob, "Bob"}, Epoch{3}).ok());
    CHECK(room.start(alice, now).error_if()->code == ErrorCode::InvalidState);
    CHECK(room.join({carol, "Carol"}, Epoch{4}).ok());
    CHECK(room.join({PlayerId{"d"}, "D"}, Epoch{5}).ok());
    const auto revision = room.view().revision;
    CHECK(room.join({PlayerId{"e"}, "E"}, Epoch{5}).error_if()->code == ErrorCode::RoomFull);
    CHECK(room.join({alice, "Different"}, Epoch{2}).value_if()->revision == revision);
    CHECK(room.join({alice, "Alice"}, Epoch{6}).error_if()->code == ErrorCode::StaleEpoch);
    ready_pair(room);
    CHECK(room.set_ready(carol, true).ok());
    CHECK(room.set_ready(PlayerId{"d"}, true).ok());
    CHECK(room.start(bob, now).error_if()->code == ErrorCode::Unauthorized);
    const auto start = room.start(alice, now);
    CHECK(start.ok());
    CHECK(start.value_if()->phase == Phase::Running);
    CHECK(start.value_if()->phase_changed);
    CHECK(start.value_if()->world->players.size() == 4);
    CHECK(start.value_if()->world->tick == Tick{});
    CHECK(!room.start(alice, now).ok());
    CHECK(!room.join({PlayerId{"z"}, "Z"}, Epoch{1}).ok());
}

void host_transfer_and_reclaim() {
    Room room(config());
    join_pair(room);
    CHECK(room.join({carol, "Carol"}, Epoch{1}).ok());
    CHECK(room.leave(alice).value_if()->members[0].is_host);
    CHECK(room.view().members[0].profile.id == bob);
    CHECK(room.set_ready(bob, true).ok());
    CHECK(room.set_connected(bob, false).ok());
    CHECK(!room.view().members[0].ready);
    CHECK(room.view().members[1].is_host);
    CHECK(room.set_connected(carol, false).value_if()->phase == Phase::Closed);
    CHECK(room.view().members.empty());
    CHECK(!room.join({alice, "Alice"}, Epoch{2}).ok());
}

void validate_batches_and_hold_result() {
    Room room(config());
    join_pair(room);
    ready_pair(room);
    CHECK(room.start(alice, now).ok());
    PlayerMove move{alice, InputFrame{match, Epoch{2}, InputSeq{1}, {1, 0}}};
    const AuthorizedPickup stale{bob, {match, Epoch{2}, ActionId{"bad"}, InputSeq{1}, 1}};
    CHECK(room.step(now, {move}, {stale}).error_if()->code == ErrorCode::StaleEpoch);
    auto other = move;
    other.frame->match_id = MatchId{"old"};
    CHECK(room.step(now, {other}).error_if()->code == ErrorCode::StaleMatch);
    CHECK(room.step(now, {move, move}).error_if()->code == ErrorCode::InvalidArgument);
    CHECK(room.current_frame().world->tick == Tick{});
    CHECK(room.current_frame().world->players[0].move.position.x == 2);
    CHECK(room.step(now, {move}).value_if()->world->players[0].move.position.x > 2);
    CHECK(room.set_connected(alice, false).ok());
    CHECK(room.current_frame().world->players[0].move.velocity.x == 0);
    CHECK(!room.step(now, {move}).ok());
    const auto end = room.step(now + 1s);
    CHECK(end.ok());
    CHECK(end.value_if()->result->reason == ResultReason::TimeUp);
    CHECK(end.value_if()->phase == Phase::Finished);
    CHECK(end.value_if()->phase_changed);
    CHECK(room.step(now + 2s).value_if()->events.empty());
    CHECK(!room.step(now + 2s, {move}).ok());
    CHECK(room.leave(bob).ok());
    CHECK(room.current_frame().world->players.size() == 2);
    CHECK(room.current_frame().world->result->scores.size() == 2);
    CHECK(room.maintain(now + 15s).value_if()->phase == Phase::Finished);
    CHECK(room.maintain(now + 16s).value_if()->phase == Phase::Closed);
    CHECK(room.view().members.empty());
    CHECK(!room.current_frame().world);
}

void last_running_departure_publishes_events_once() {
    Room room(config());
    join_pair(room);
    ready_pair(room);
    CHECK(room.start(alice, now).ok());
    const AuthorizedPickup claim{alice, {match, Epoch{2}, ActionId{"pickup"}, InputSeq{1}, 1}};
    CHECK(room.step(now, {}, {claim}).value_if()->world->players[0].score == 1);
    CHECK(room.leave(alice).value_if()->phase == Phase::Running);
    CHECK(room.leave(bob).value_if()->phase == Phase::Closed);
    CHECK(!room.current_frame().world);
    CHECK(room.current_frame().events.empty());
    const auto closure = room.maintain(now);
    CHECK(closure.value_if()->events.size() == 3);
    const auto& removed = std::get<PlayerRemoved>(closure.value_if()->events[0]);
    CHECK(removed.player_id == alice);
    CHECK(removed.final_score == 1);
    CHECK(closure.value_if()->result->reason == ResultReason::Aborted);
    CHECK(std::get<MatchFinished>(closure.value_if()->events[2]).tick == Tick{1});
    CHECK(room.maintain(now).value_if()->events.empty());
    CHECK(!room.maintain(now).value_if()->result);
}

void early_close_and_deadline_limit() {
    auto settings = config();
    settings.match.duration_ticks = Tick{1};
    Room room(settings);
    join_pair(room);
    ready_pair(room);
    CHECK(room.start(alice, now).ok());
    CHECK(room.step(now).value_if()->phase == Phase::Finished);
    CHECK(room.leave(alice).value_if()->phase == Phase::Finished);
    CHECK(room.leave(bob).value_if()->phase == Phase::Closed);
    CHECK(room.leave(bob).value_if()->phase == Phase::Closed);

    settings.room_id = RoomId{"another-room"};
    settings.match_id = MatchId{"another-match"};
    settings.match.match_id = settings.match_id;
    Room another(settings);
    join_pair(another);
    ready_pair(another);
    CHECK(another.start(alice, MonoTime::max() - 1s).ok());
    CHECK(another.step(MonoTime::max() - 2s).error_if()->code == ErrorCode::InvalidArgument);
    CHECK(another.step(MonoTime::max() - 1s).value_if()->phase == Phase::Finished);
    CHECK(another.maintain(MonoTime::max()).value_if()->phase == Phase::Closed);
}
}

int main() {
    admission_and_start();
    host_transfer_and_reclaim();
    validate_batches_and_hold_result();
    last_running_departure_publishes_events_once();
    early_close_and_deadline_limit();
    std::cout << "room: " << checks << " checks passed\n";
}
