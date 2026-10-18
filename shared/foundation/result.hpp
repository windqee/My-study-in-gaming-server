#pragma once

#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace netarena {

enum class ErrorCode {
    InvalidArgument, InvalidState, ProtocolMismatch, MalformedPacket,
    Unauthorized, NotFound, AlreadyExists, RoomFull, StaleMatch, StaleEpoch,
    Expired, SequenceGap, RateLimited, QueueFull, WouldBlock, Timeout,
    Unsupported, IoError, CorruptArtifact, IncompleteEvidence, Cancelled,
    InternalError
};

// Boundary callers must redact and bound messages before logging or encoding.
struct Error {
    ErrorCode code;
    std::string message;
    std::optional<std::string> field;
};

template <typename T>
class Result {
public:
    static Result success(T value) { return Result(std::in_place_index<0>, std::move(value)); }
    static Result failure(Error error) { return Result(std::in_place_index<1>, std::move(error)); }

    Result(const Result&) = default;
    Result(Result&&) = default;
    // No alternative-changing assignment/emplace: an observable Result always
    // has exactly one alternative, even if T's constructor can throw.
    Result& operator=(const Result&) = delete;
    Result& operator=(Result&&) = delete;

    [[nodiscard]] bool ok() const noexcept { return state_.index() == 0; }
    [[nodiscard]] const T* value_if() const noexcept { return std::get_if<0>(&state_); }
    // Enables move-only extraction; the successful value may then be moved-from.
    [[nodiscard]] T* value_if() noexcept { return std::get_if<0>(&state_); }
    [[nodiscard]] const Error* error_if() const noexcept { return std::get_if<1>(&state_); }

private:
    template <std::size_t Index, typename U>
    explicit Result(std::in_place_index_t<Index> tag, U&& value)
        : state_(tag, std::forward<U>(value)) {}
    std::variant<T, Error> state_;
};

template <>
class Result<void> {
public:
    static Result success() { return Result(std::nullopt); }
    static Result failure(Error error) { return Result(std::move(error)); }
    Result(const Result&) = default;
    Result(Result&&) = default;
    Result& operator=(const Result&) = delete;
    Result& operator=(Result&&) = delete;
    [[nodiscard]] bool ok() const noexcept { return !error_; }
    [[nodiscard]] const Error* error_if() const noexcept { return error_ ? &*error_ : nullptr; }
private:
    explicit Result(std::optional<Error> error) : error_(std::move(error)) {}
    std::optional<Error> error_;
};

} // namespace netarena
