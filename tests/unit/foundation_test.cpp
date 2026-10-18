#include "shared/foundation/result.hpp"
#include "shared/foundation/types.hpp"
#include "shared/foundation/clock.hpp"
#include "check.hpp"
#include <limits>
#include <memory>
#include <type_traits>
using namespace netarena;
static_assert(!std::is_convertible_v<Result<int>, bool>);
static_assert(!std::is_assignable_v<Result<int>&, Result<int>>);
static_assert(!std::is_convertible_v<PlayerId, SessionId>);
static_assert(!std::is_convertible_v<RoomId, MatchId>);
static_assert(!std::is_convertible_v<ActionId, RunId>);
static_assert(!std::is_convertible_v<std::string, PlayerId>);
static_assert(!std::is_convertible_v<InputSeq, SnapshotSeq>);
static_assert(!std::is_convertible_v<Tick, Epoch>);
static_assert(!std::is_convertible_v<std::uint64_t, InputSeq>);
static_assert(!std::is_convertible_v<ConnectionId, PlayerId>);
static_assert(!std::is_convertible_v<SecretToken, Bytes>);
static_assert(!std::is_convertible_v<SecretToken, TokenDigest>);
class ManualClock final : public IClock {
public:
    MonoTime now() const noexcept override { return time_; }
    void advance(Duration amount) { time_ += amount; }
private:
    MonoTime time_{};
};
int main() {
    const auto value = Result<int>::success(42);
    CHECK(value.ok()); CHECK(*value.value_if() == 42); CHECK(value.error_if() == nullptr);
    const Error error{ErrorCode::InvalidArgument, "invalid", "field"};
    const auto failure = Result<int>::failure(error);
    CHECK(!failure.ok()); CHECK(failure.value_if() == nullptr);
    CHECK(failure.error_if()->code == ErrorCode::InvalidArgument);
    CHECK(failure.error_if()->field == "field");
    const auto error_value = Result<Error>::success(error);
    CHECK(error_value.ok()); CHECK(error_value.error_if() == nullptr);
    CHECK(error_value.value_if()->message == "invalid");
    const auto error_failure = Result<Error>::failure(error);
    CHECK(!error_failure.ok()); CHECK(error_failure.value_if() == nullptr);
    const auto done = Result<void>::success();
    CHECK(done.ok()); CHECK(done.error_if() == nullptr);
    const auto rejected = Result<void>::failure(error);
    CHECK(!rejected.ok()); CHECK(rejected.error_if()->code == error.code);
    auto owned = Result<std::unique_ptr<int>>::success(std::make_unique<int>(7));
    auto extracted = std::move(*owned.value_if());
    CHECK(*extracted == 7); CHECK(owned.ok()); CHECK(!*owned.value_if());
    const auto max = std::numeric_limits<std::uint64_t>::max();
    for (auto n : {std::uint64_t{0}, std::uint64_t{9007199254740993ULL}, max}) {
        const auto parsed = parse_decimal<InputSeq>(to_decimal(InputSeq{n}));
        CHECK(parsed.ok()); CHECK(parsed.value_if()->value() == n);
    }
    CHECK(to_decimal(InputSeq{max}) == "18446744073709551615");
    for (auto text : {"", "-1", "+1", " 1", "1 ", "1.0", "1x", "18446744073709551616"}) {
        const auto parsed = parse_decimal<InputSeq>(text);
        CHECK(!parsed.ok()); CHECK(parsed.error_if()->code == ErrorCode::InvalidArgument);
    }
    CHECK(parse_decimal<Epoch>("0002").value_if()->value() == 2);
    CHECK(!parse_decimal<Tick>(std::string_view{"1\0", 2}).ok());
    CHECK(checked_increment(InputSeq{41}).value_if()->value() == 42);
    CHECK(!checked_increment(InputSeq{max}).ok());
    CHECK(!checked_increment(SnapshotSeq{max}).ok());
    CHECK(!checked_increment(Tick{max}).ok());
    CHECK(!checked_increment(Epoch{max}).ok());
    CHECK((ConnectionId{3, 1} != ConnectionId{3, 2}));
    CHECK((ConnectionId{3, 1} == ConnectionId{3, 1}));
    CHECK(PlayerId{"p1"}.value() == "p1");
    const SecretToken token{Bytes{std::byte{1}, std::byte{2}}};
    CHECK(token.bytes().size() == 2);
    const TokenDigest digest{Bytes{std::byte{3}}};
    CHECK(digest.bytes()[0] == std::byte{3});
    ManualClock manual;
    const auto before = manual.now(); manual.advance(Duration{123456789});
    CHECK(manual.now() - before == Duration{123456789});
    SteadyClock real; const auto first = real.now(); CHECK(real.now() >= first);
    std::cout << "foundation: " << checks << " checks passed\n";
}
