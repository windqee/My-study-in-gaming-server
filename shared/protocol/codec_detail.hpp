#pragma once
// Private implementation: no JSON dependency leaks through protocol_codec.hpp.
#include "shared/protocol/protocol_codec.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string_view>
namespace netarena::protocol_detail {
using Json = nlohmann::json;
struct Invalid {};
inline void require(bool condition) {
    if (!condition)
        throw Invalid{};
}
struct Context {
    const CodecLimits &limits;
    MonoTime now;
    bool read;
};
template <class T> struct Schema;
template <class T> struct EnumNames;
template <class T> struct IsVector : std::false_type {};
template <class T> struct IsVector<std::vector<T>> : std::true_type {};
template <class T> struct IsId : std::false_type {};
template <class T> struct IsId<StringId<T>> : std::true_type {};
template <class T>
void value(Json &j, T &v, Context &c, std::size_t maximum = 0, bool nonempty = false);
struct Archive {
    Json &json;
    Context &context;
    std::set<std::string> keys;
    Archive(Json &j, Context &c) : json(j), context(c) {
        if (c.read)
            require(j.is_object());
        else
            j = Json::object();
    }
    template <class T>
    void field(const char *key, T &v, std::size_t maximum = 0, bool nonempty = false) {
        keys.insert(key);
        if (context.read)
            require(json.contains(key));
        value(json[key], v, context, maximum, nonempty);
    }
    template <class T>
    void optional(const char *key, std::optional<T> &v, std::size_t maximum = 0) {
        keys.insert(key);
        if (context.read) {
            if (!json.contains(key)) {
                v.reset();
                return;
            }
            v.emplace();
        }
        if (v)
            value(json[key], *v, context, maximum, false);
    }
    void finish() {
        if (context.read)
            for (auto it = json.begin(); it != json.end(); ++it)
                require(keys.contains(it.key()));
    }
};
inline std::uint64_t decimal(const Json &j) {
    require(j.is_string());
    const auto &s = j.get_ref<const std::string &>();
    require(!s.empty() && s.size() <= 20 && (s.size() == 1 || s.front() != '0'));
    std::uint64_t n{};
    const auto r = std::from_chars(s.data(), s.data() + s.size(), n);
    require(s.front() >= '0' && s.front() <= '9' && r.ec == std::errc{} &&
            r.ptr == s.data() + s.size());
    return n;
}
template <class T> void value(Json &j, T &v, Context &c, std::size_t maximum, bool nonempty) {
    if constexpr (std::same_as<T, std::string>) {
        if (c.read) {
            require(j.is_string());
            v = j.get<std::string>();
        }
        require(v.size() <= maximum && (!nonempty || !v.empty()));
        if (!c.read)
            j = v;
    } else if constexpr (IsId<T>::value) {
        auto s = v.value();
        value(j, s, c, c.limits.max_id_bytes, true);
        if (c.read)
            v = T{std::move(s)};
    } else if constexpr (SequenceType<T>) {
        if (c.read)
            v = T{decimal(j)};
        else
            j = std::to_string(v.value());
    } else if constexpr (std::same_as<T, std::uint64_t>) {
        if (c.read)
            v = decimal(j);
        else
            j = std::to_string(v);
    } else if constexpr (std::same_as<T, bool>) {
        if (c.read) {
            require(j.is_boolean());
            v = j.get<bool>();
        } else
            j = v;
    } else if constexpr (std::is_integral_v<T>) {
        if (c.read) {
            require(j.is_number_unsigned());
            auto n = j.get<std::uint64_t>();
            require(n <= std::numeric_limits<T>::max());
            v = static_cast<T>(n);
        } else
            j = v;
    } else if constexpr (std::same_as<T, float>) {
        if (c.read) {
            require(j.is_number());
            const double n = j.get<double>();
            require(std::isfinite(n) && std::abs(n) <= std::numeric_limits<float>::max());
            v = static_cast<float>(n);
        }
        require(std::isfinite(v));
        if (!c.read)
            j = v;
    } else if constexpr (std::is_enum_v<T>) {
        const auto &names = EnumNames<T>::values;
        if (c.read) {
            require(j.is_string());
            const auto s = j.get<std::string>();
            auto it = std::find(names.begin(), names.end(), s);
            require(it != names.end());
            v = static_cast<T>(it - names.begin());
        }
        const auto i = static_cast<std::size_t>(v);
        require(i < names.size());
        if (!c.read)
            j = names[i];
    } else if constexpr (IsVector<T>::value) {
        if (c.read) {
            require(j.is_array() && j.size() <= maximum);
            v.resize(j.size());
        } else {
            require(v.size() <= maximum);
            j = Json::array();
        }
        for (std::size_t i = 0; i < v.size(); ++i)
            value(j[i], v[i], c);
    } else if constexpr (std::same_as<T, SecretToken>) {
        if (c.read) {
            require(j.is_string());
            const auto s = j.get<std::string>();
            require(!s.empty() && s.size() % 2 == 0 && s.size() / 2 <= c.limits.max_token_bytes);
            auto nibble = [](char x) -> unsigned {
                if (x >= '0' && x <= '9')
                    return static_cast<unsigned>(x - '0');
                if (x >= 'a' && x <= 'f')
                    return static_cast<unsigned>(x - 'a' + 10);
                throw Invalid{};
            };
            Bytes bytes;
            bytes.reserve(s.size() / 2);
            for (std::size_t i = 0; i < s.size(); i += 2)
                bytes.push_back(static_cast<std::byte>((nibble(s[i]) << 4U) | nibble(s[i + 1])));
            v = SecretToken{std::move(bytes)};
        } else {
            const auto bytes = v.bytes();
            require(!bytes.empty() && bytes.size() <= c.limits.max_token_bytes);
            std::string s;
            s.reserve(bytes.size() * 2);
            for (auto b : bytes) {
                auto n = std::to_integer<unsigned>(b);
                s.push_back("0123456789abcdef"[n >> 4U]);
                s.push_back("0123456789abcdef"[n & 15U]);
            }
            j = s;
        }
    } else {
        Archive a(j, c);
        Schema<T>::apply(a, v);
        a.finish();
    }
}
template <> struct EnumNames<Phase> {
    static constexpr std::array values{"WAITING", "RUNNING", "FINISHED", "CLOSED"};
};
template <> struct EnumNames<ResultReason> {
    static constexpr std::array values{"TimeUp", "CoinsExhausted", "Aborted"};
};
template <> struct EnumNames<ActionStatus> {
    static constexpr std::array values{"Applied", "Duplicate", "Rejected"};
};
template <> struct EnumNames<MessageKind> {
    static constexpr std::array values{"Hello",
                                       "Resume",
                                       "CreateRoom",
                                       "JoinRoom",
                                       "SetReady",
                                       "StartMatch",
                                       "LeaveRoom",
                                       "Move",
                                       "Pickup",
                                       "Heartbeat",
                                       "RequestFullSnapshot",
                                       "Welcome",
                                       "RoomUpdate",
                                       "MatchStarted",
                                       "Snapshot",
                                       "ResumeState",
                                       "ActionResult",
                                       "MatchEnded",
                                       "Pong",
                                       "ErrorReply"};
};
template <> struct EnumNames<ErrorCode> {
    static constexpr std::array values{
        "InvalidArgument", "InvalidState", "ProtocolMismatch", "MalformedPacket",
        "Unauthorized",    "NotFound",     "AlreadyExists",    "RoomFull",
        "StaleMatch",      "StaleEpoch",   "Expired",          "SequenceGap",
        "RateLimited",     "QueueFull",    "WouldBlock",       "Timeout",
        "Unsupported",     "IoError",      "CorruptArtifact",  "IncompleteEvidence",
        "Cancelled",       "InternalError"};
};
Json write_envelope(Envelope &e, Context &c, Direction direction);
Envelope read_envelope(Json &j, Context &c, Direction direction);
void preflight(std::string_view packet, const CodecLimits &limits);
} // namespace netarena::protocol_detail
