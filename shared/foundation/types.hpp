#pragma once

#include "shared/foundation/result.hpp"

#include <charconv>
#include <cmath>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace netarena {

template <typename Tag>
class StringId {
public:
    StringId() = default;
    explicit StringId(std::string value) : value_(std::move(value)) {}
    [[nodiscard]] const std::string& value() const noexcept { return value_; }
    auto operator<=>(const StringId&) const = default;
private:
    std::string value_;
};

using PlayerId = StringId<struct PlayerIdTag>;
using SessionId = StringId<struct SessionIdTag>;
using RoomId = StringId<struct RoomIdTag>;
using MatchId = StringId<struct MatchIdTag>;
using ActionId = StringId<struct ActionIdTag>;
using RunId = StringId<struct RunIdTag>;

template <typename Tag>
class Sequence {
public:
    Sequence() = default;
    explicit constexpr Sequence(std::uint64_t value) : value_(value) {}
    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }
    auto operator<=>(const Sequence&) const = default;
private:
    std::uint64_t value_{};
};

using Tick = Sequence<struct TickTag>;
using InputSeq = Sequence<struct InputSeqTag>;
using SnapshotSeq = Sequence<struct SnapshotSeqTag>;
using Epoch = Sequence<struct EpochTag>;

template <typename S>
concept SequenceType = std::same_as<S, Tick> || std::same_as<S, InputSeq> ||
                       std::same_as<S, SnapshotSeq> || std::same_as<S, Epoch>;

template <SequenceType S>
[[nodiscard]] Result<S> checked_increment(S current) {
    if (current.value() == std::numeric_limits<std::uint64_t>::max()) {
        return Result<S>::failure({ErrorCode::InvalidArgument, "sequence exhausted", std::nullopt});
    }
    return Result<S>::success(S{current.value() + 1});
}

template <SequenceType S>
[[nodiscard]] std::string to_decimal(S value) { return std::to_string(value.value()); }

template <SequenceType S>
[[nodiscard]] Result<S> parse_decimal(std::string_view text) {
    // Leading zeroes are accepted; signs, whitespace and partial parses are not.
    if (text.empty() || text.front() < '0' || text.front() > '9') {
        return Result<S>::failure({ErrorCode::InvalidArgument, "expected unsigned decimal", std::nullopt});
    }
    std::uint64_t value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, 10);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        return Result<S>::failure({ErrorCode::InvalidArgument, "invalid or overflowing decimal", std::nullopt});
    }
    return Result<S>::success(S{value});
}

struct ConnectionId {
    std::uint64_t slot{};
    std::uint64_t generation{};
    auto operator<=>(const ConnectionId&) const = default;
};

using Bytes = std::vector<std::byte>;
using ByteView = std::span<const std::byte>;

struct Vec2 {
    float x{};
    float y{};
    [[nodiscard]] bool finite() const noexcept { return std::isfinite(x) && std::isfinite(y); }
};

// Opaque, explicitly accessed storage only. No cryptography, serialization,
// printing or secure-erasure guarantees are supplied by these value wrappers.
template <typename Tag>
class OpaqueBytes {
public:
    explicit OpaqueBytes(Bytes bytes) : bytes_(std::move(bytes)) {}
    [[nodiscard]] ByteView bytes() const noexcept { return bytes_; }
private:
    Bytes bytes_;
};
using SecretToken = OpaqueBytes<struct SecretTokenTag>;
using TokenDigest = OpaqueBytes<struct TokenDigestTag>;

} // namespace netarena
