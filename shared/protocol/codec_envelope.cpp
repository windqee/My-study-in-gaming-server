#include "shared/protocol/codec_schema.hpp"
namespace netarena::protocol_detail {
namespace {
void scope(const Envelope &e, Direction direction, const CodecLimits &limits) {
    const auto kind = e.kind();
    require(direction == Direction::ClientToServer || direction == Direction::ServerToClient);
    require((kind <= MessageKind::RequestFullSnapshot) == (direction == Direction::ClientToServer));
    if (kind == MessageKind::Hello) {
        require(!e.session_id && !e.epoch && !e.match_id);
        require(std::get<Hello>(e.payload).version == limits.protocol_version);
    } else if (kind == MessageKind::Resume) {
        const auto &p = std::get<Resume>(e.payload);
        require(!e.epoch);
        require(!e.session_id || *e.session_id == p.session_id);
        require(!e.match_id || *e.match_id == p.match_id);
    } else if (direction == Direction::ClientToServer) {
        require(e.session_id && e.epoch);
        if (kind == MessageKind::Move) {
            const auto &f = std::get<Move>(e.payload).frame;
            require(e.match_id && *e.match_id == f.match_id && *e.epoch == f.epoch);
        }
        if (kind == MessageKind::Pickup) {
            const auto &p = std::get<Pickup>(e.payload).command;
            require(e.match_id && *e.match_id == p.match_id && *e.epoch == p.epoch);
        }
    }
    auto match = [&](const MatchId &id) { require(!e.match_id || *e.match_id == id); };
    auto snapshot = [&](const MatchSnapshot &s) {
        match(s.match_id);
        if (e.epoch && s.self_ack)
            require(*e.epoch == s.self_ack->epoch);
    };
    std::visit(
        [&](const auto &p) {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::same_as<T, Welcome>) {
                require(!e.session_id || *e.session_id == p.identity.session_id);
                require(!e.epoch || *e.epoch == p.epoch);
            } else if constexpr (std::same_as<T, RoomUpdate>)
                match(p.room.match_id);
            else if constexpr (std::same_as<T, MatchStarted> || std::same_as<T, Snapshot>)
                snapshot(p.snapshot);
            else if constexpr (std::same_as<T, MatchEnded>)
                match(p.result.match_id);
            else if constexpr (std::same_as<T, ResumeState>) {
                match(p.room.match_id);
                if (p.snapshot)
                    snapshot(*p.snapshot);
            }
        },
        e.payload);
}
template <std::size_t I = 0> MessagePayload payload(MessageKind kind, Json &j, Context &c) {
    if constexpr (I < std::variant_size_v<MessagePayload>) {
        using T = std::variant_alternative_t<I, MessagePayload>;
        if (kind == T::kind) {
            T v = [] {
                if constexpr (std::same_as<T, Resume>)
                    return Resume{SessionId{}, SecretToken{Bytes{}}, MatchId{}};
                else
                    return T{};
            }();
            value(j, v, c);
            return v;
        }
        return payload<I + 1>(kind, j, c);
    } else
        throw Invalid{};
}
void fields(Archive &a, Envelope &e, Direction direction) {
    a.field("protocol_version", e.protocol_version);
    a.field("request_id", e.request_id, a.context.limits.max_request_id_bytes);
    a.optional("session_id", e.session_id);
    a.optional("epoch", e.epoch);
    a.optional("match_id", e.match_id);
    auto kind = e.kind();
    a.field("kind", kind);
    a.keys.insert("payload");
    if (a.context.read) {
        require(a.json.contains("payload"));
        e.payload = payload(kind, a.json["payload"], a.context);
    } else
        std::visit([&](auto &p) { value(a.json["payload"], p, a.context); }, e.payload);
    scope(e, direction, a.context.limits);
    a.finish();
}
} // namespace
Json write_envelope(Envelope &e, Context &c, Direction d) {
    Json j;
    Archive a(j, c);
    fields(a, e, d);
    return j;
}
Envelope read_envelope(Json &j, Context &c, Direction d) {
    Envelope e;
    Archive a(j, c);
    fields(a, e, d);
    return e;
}
void preflight(std::string_view packet, const CodecLimits &limits) {
    require(!packet.empty() && packet.size() <= limits.max_packet_bytes);
    std::size_t depth = 0;
    bool quoted = false, escaped = false;
    for (char ch : packet) {
        if (quoted) {
            if (escaped)
                escaped = false;
            else if (ch == '\\')
                escaped = true;
            else if (ch == '"')
                quoted = false;
            continue;
        }
        if (ch == '"')
            quoted = true;
        else if (ch == '{' || ch == '[') {
            ++depth;
            require(depth <= limits.max_depth);
        } else if (ch == '}' || ch == ']') {
            require(depth > 0);
            --depth;
        }
    }
    require(depth == 0 && !quoted);
}
} // namespace netarena::protocol_detail
