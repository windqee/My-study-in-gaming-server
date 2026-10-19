#pragma once
#include "shared/protocol/codec_detail.hpp"
namespace netarena::protocol_detail {
template <> struct Schema<Vec2> {
    static void apply(Archive &a, Vec2 &v) {
        a.field("x", v.x);
        a.field("y", v.y);
    }
};

template <> struct Schema<Rect> {
    static void apply(Archive &a, Rect &v) {
        a.field("min", v.min);
        a.field("max", v.max);
        require(v.min.x < v.max.x && v.min.y < v.max.y);
    }
};

template <> struct Schema<PlayerProfile> {
    static void apply(Archive &a, PlayerProfile &v) {
        a.field("id", v.id);
        a.field("display_name", v.display_name, a.context.limits.max_name_bytes, true);
    }
};

template <> struct Schema<InputFrame> {
    static void apply(Archive &a, InputFrame &v) {
        a.field("match_id", v.match_id);
        a.field("epoch", v.epoch);
        a.field("seq", v.seq);
        a.field("direction", v.direction);
    }
};

template <> struct Schema<PickupCommand> {
    static void apply(Archive &a, PickupCommand &v) {
        a.field("match_id", v.match_id);
        a.field("epoch", v.epoch);
        a.field("action_id", v.action_id);
        a.field("input_seq", v.input_seq);
        a.field("coin_id", v.coin_id);
    }
};

template <> struct Schema<MoveState> {
    static void apply(Archive &a, MoveState &v) {
        a.field("position", v.position);
        a.field("velocity", v.velocity);
    }
};

template <> struct Schema<MapDefinition> {
    static void apply(Archive &a, MapDefinition &v) {
        a.field("map_id", v.map_id, a.context.limits.max_id_bytes, true);
        a.field("revision", v.revision);
        a.field("bounds", v.bounds);
        a.field("obstacles", v.obstacles, a.context.limits.max_obstacles);
        a.field("spawn_points", v.spawn_points, a.context.limits.max_players);
        require(!v.spawn_points.empty());
    }
};

template <> struct Schema<MovementConfig> {
    static void apply(Archive &a, MovementConfig &v) {
        a.field("speed", v.speed);
        a.field("player_radius", v.player_radius);
        a.field("tick_hz", v.tick_hz);
        require(v.speed > 0 && v.speed <= 10000 && v.player_radius > 0 &&
                v.player_radius <= 10000 && v.tick_hz > 0 && v.tick_hz <= 1000);
    }
};

template <> struct Schema<PlayerState> {
    static void apply(Archive &a, PlayerState &v) {
        a.field("player_id", v.player_id);
        a.field("move", v.move);
        a.field("score", v.score);
        a.field("connected", v.connected);
    }
};

template <> struct Schema<CoinState> {
    static void apply(Archive &a, CoinState &v) {
        a.field("coin_id", v.coin_id);
        a.field("position", v.position);
        a.field("active", v.active);
    }
};

template <> struct Schema<AckState> {
    static void apply(Archive &a, AckState &v) {
        a.field("epoch", v.epoch);
        a.field("terminal_input_seq", v.terminal_input_seq);
    }
};

template <> struct Schema<MatchSnapshot> {
    static void apply(Archive &a, MatchSnapshot &v) {
        a.field("match_id", v.match_id);
        a.field("tick", v.tick);
        a.field("snapshot_seq", v.snapshot_seq);
        a.field("players", v.players, a.context.limits.max_players);
        a.field("coins", v.coins, a.context.limits.max_coins);
        a.field("remaining_ticks", v.remaining_ticks);
        a.field("phase", v.phase);
        a.optional("self_ack", v.self_ack);
        std::set<PlayerId> ids;
        for (const auto &p : v.players)
            require(ids.insert(p.player_id).second);
        std::set<std::uint32_t> coins;
        for (const auto &coin : v.coins)
            require(coins.insert(coin.coin_id).second);
    }
};

template <> struct Schema<MemberView> {
    static void apply(Archive &a, MemberView &v) {
        a.field("profile", v.profile);
        a.field("ready", v.ready);
        a.field("connected", v.connected);
        a.field("is_host", v.is_host);
        a.field("join_order", v.join_order);
        std::optional<std::uint32_t> remaining;
        const auto now = a.context.now;
        if (!a.context.read && v.reserve_until) {
            if (*v.reserve_until <= now)
                remaining = 0;
            else {
                // Unsigned subtraction gives the exact positive difference even
                // when the signed epoch counts span the full time_point range.
                using Rep = MonoTime::duration::rep;
                using UnsignedRep = std::make_unsigned_t<Rep>;
                const auto delta =
                    static_cast<UnsignedRep>(v.reserve_until->time_since_epoch().count()) -
                    static_cast<UnsignedRep>(now.time_since_epoch().count());
                const auto maximum = std::chrono::duration_cast<MonoTime::duration>(
                    std::chrono::milliseconds(a.context.limits.max_reserve_ms));
                require(delta <= static_cast<UnsignedRep>(maximum.count()));
                remaining =
                    static_cast<std::uint32_t>(std::chrono::ceil<std::chrono::milliseconds>(
                                                   MonoTime::duration{static_cast<Rep>(delta)})
                                                   .count());
            }
        }
        a.optional("reserve_remaining_ms", remaining);
        if (remaining)
            require(*remaining <= a.context.limits.max_reserve_ms);
        if (a.context.read) {
            v.reserve_until.reset();
            if (remaining) {
                const auto d = std::chrono::duration_cast<MonoTime::duration>(
                    std::chrono::milliseconds(*remaining));
                require(now <= MonoTime::max() - d);
                v.reserve_until = now + d;
            }
        }
    }
};

template <> struct Schema<RoomView> {
    static void apply(Archive &a, RoomView &v) {
        a.field("room_id", v.room_id);
        a.field("match_id", v.match_id);
        a.field("phase", v.phase);
        a.field("revision", v.revision);
        a.field("members", v.members, a.context.limits.max_players);
        std::set<PlayerId> ids;
        std::set<std::uint64_t> orders;
        for (const auto &m : v.members) {
            require(ids.insert(m.profile.id).second);
            require(orders.insert(m.join_order).second);
        }
    }
};

template <> struct Schema<PlayerScore> {
    static void apply(Archive &a, PlayerScore &v) {
        a.field("player_id", v.player_id);
        a.field("score", v.score);
    }
};

template <> struct Schema<MatchResult> {
    static void apply(Archive &a, MatchResult &v) {
        a.field("match_id", v.match_id);
        a.field("reason", v.reason);
        a.field("scores", v.scores, a.context.limits.max_players);
        a.field("winners", v.winners, a.context.limits.max_players);
        std::set<PlayerId> ids;
        for (const auto &s : v.scores)
            require(ids.insert(s.player_id).second);
        std::set<PlayerId> winners;
        for (const auto &id : v.winners)
            require(ids.contains(id) && winners.insert(id).second);
    }
};

template <> struct Schema<PublicIdentity> {
    static void apply(Archive &a, PublicIdentity &v) {
        a.field("player_id", v.player_id);
        a.field("session_id", v.session_id);
    }
};

template <> struct Schema<MatchRules> {
    static void apply(Archive &a, MatchRules &v) {
        a.field("movement", v.movement);
        a.field("duration_ticks", v.duration_ticks);
        a.field("pickup_radius", v.pickup_radius);
        require(v.duration_ticks.value() > 0 && v.pickup_radius > 0 && v.pickup_radius <= 10000);
    }
};

template <> struct Schema<Error> {
    static void apply(Archive &a, Error &v) {
        a.field("code", v.code);
        a.field("message", v.message, a.context.limits.max_error_bytes);
        a.optional("field", v.field, a.context.limits.max_id_bytes);
    }
};

template <> struct Schema<Hello> {
    static void apply(Archive &a, Hello &v) {
        a.field("version", v.version);
        a.field("display_name", v.display_name, a.context.limits.max_name_bytes, true);
    }
};

template <> struct Schema<Resume> {
    static void apply(Archive &a, Resume &v) {
        a.field("session_id", v.session_id);
        a.field("token", v.token);
        a.field("match_id", v.match_id);
    }
};

template <> struct Schema<CreateRoom> {
    static void apply(Archive &a, CreateRoom &v) {
        (void)a;
        (void)v;
    }
};

template <> struct Schema<JoinRoom> {
    static void apply(Archive &a, JoinRoom &v) {
        a.field("room_id", v.room_id);
    }
};

template <> struct Schema<SetReady> {
    static void apply(Archive &a, SetReady &v) {
        a.field("ready", v.ready);
    }
};

template <> struct Schema<StartMatch> {
    static void apply(Archive &a, StartMatch &v) {
        (void)a;
        (void)v;
    }
};

template <> struct Schema<LeaveRoom> {
    static void apply(Archive &a, LeaveRoom &v) {
        (void)a;
        (void)v;
    }
};

template <> struct Schema<Move> {
    static void apply(Archive &a, Move &v) {
        a.field("frame", v.frame);
    }
};

template <> struct Schema<Pickup> {
    static void apply(Archive &a, Pickup &v) {
        a.field("command", v.command);
    }
};

template <> struct Schema<Heartbeat> {
    static void apply(Archive &a, Heartbeat &v) {
        a.field("nonce", v.nonce);
    }
};

template <> struct Schema<RequestFullSnapshot> {
    static void apply(Archive &a, RequestFullSnapshot &v) {
        (void)a;
        (void)v;
    }
};

template <> struct Schema<Welcome> {
    static void apply(Archive &a, Welcome &v) {
        a.field("identity", v.identity);
        a.field("epoch", v.epoch);
        a.keys.insert("recovery_token");
        if (a.context.read) {
            v.recovery_token.reset();
            if (a.json.contains("recovery_token"))
                v.recovery_token.emplace(Bytes{});
        }
        if (v.recovery_token)
            value(a.json["recovery_token"], *v.recovery_token, a.context);
    }
};

template <> struct Schema<RoomUpdate> {
    static void apply(Archive &a, RoomUpdate &v) {
        a.field("room", v.room);
    }
};

template <> struct Schema<MatchStarted> {
    static void apply(Archive &a, MatchStarted &v) {
        a.field("map", v.map);
        a.field("rules", v.rules);
        a.field("snapshot", v.snapshot);
        require(v.snapshot.phase == Phase::Running);
    }
};

template <> struct Schema<Snapshot> {
    static void apply(Archive &a, Snapshot &v) {
        a.field("snapshot", v.snapshot);
    }
};

template <> struct Schema<ResumeState> {
    static void apply(Archive &a, ResumeState &v) {
        a.field("room", v.room);
        a.optional("map", v.map);
        a.optional("rules", v.rules);
        a.optional("snapshot", v.snapshot);
        a.optional("result", v.result);
        require(v.room.phase != Phase::Closed);
        if (v.room.phase == Phase::Waiting)
            require(!v.map && !v.rules && !v.snapshot && !v.result);
        else {
            require(v.map && v.rules && v.snapshot);
            require(v.snapshot->match_id == v.room.match_id && v.snapshot->phase == v.room.phase);
            require((v.room.phase == Phase::Finished) == v.result.has_value());
            if (v.result)
                require(v.result->match_id == v.room.match_id);
            std::set<PlayerId> room;
            std::set<PlayerId> players;
            for (const auto &m : v.room.members)
                room.insert(m.profile.id);
            for (const auto &p : v.snapshot->players)
                players.insert(p.player_id);
            require(room == players);
            if (v.result) {
                std::set<PlayerId> scored;
                for (const auto &score : v.result->scores)
                    scored.insert(score.player_id);
                require(scored == players);
            }
        }
    }
};

template <> struct Schema<ActionResult> {
    static void apply(Archive &a, ActionResult &v) {
        a.field("action_id", v.action_id);
        a.field("status", v.status);
        a.optional("score", v.score);
    }
};

template <> struct Schema<MatchEnded> {
    static void apply(Archive &a, MatchEnded &v) {
        a.field("result", v.result);
    }
};

template <> struct Schema<Pong> {
    static void apply(Archive &a, Pong &v) {
        a.field("nonce", v.nonce);
    }
};

template <> struct Schema<ErrorReply> {
    static void apply(Archive &a, ErrorReply &v) {
        a.field("error", v.error);
    }
};

} // namespace netarena::protocol_detail
