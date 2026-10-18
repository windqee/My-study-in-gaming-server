# WP-01 Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Deliver an offline C++20 build, tested M01 values and minimal M02 contracts, and an honest foundations demonstration.

**Architecture:** Header-only standard-library foundation and contracts have no network, engine, codec, or crypto dependencies. DTOs own their data; runtime adapters remain future work. Tests use an always-active CHECK helper, including Release.

**Tech Stack:** C++20, CMake >=3.25, Unix Makefiles, CTest; Python metadata only.

## File map

- `shared/foundation/result.hpp`: ErrorCode, Error and exclusive Result<T>/Result<void>.
- `shared/foundation/types.hpp`: distinct string IDs, uint64 sequences, local connection identity, byte/vector/token values.
- `shared/foundation/clock.hpp`: monotonic time aliases and injectable clock port.
- `shared/contracts/messages.hpp`: owned DTOs, closed message union, derived kind and basic pure validators.
- `shared/contracts/transport.hpp`: channels, owned transport events, optional statistics and ITransport port.
- `tests/unit/check.hpp`, `foundation_test.cpp`, `contracts_test.cpp`: runtime and compile-time contract checks.
- `apps/foundation_demo.cpp`: explicitly labelled foundation demonstration.
- `CMakeLists.txt`, `CMakePresets.json`: offline Debug/Release builds, tests and individual header compilation.
- `VERSION.txt`: sole authored version source, `0.1.0-dev`; `pyproject.toml`: future Python tool configuration only.
- `.gitignore`, `README.md`, `docs/wp01-implementation.md`: usage, verification, scope and API decisions.
- `docs/superpowers/plans/2026-10-17-netarena-roadmap.md`: mark only WP-01 complete after verification.

## Task 1: M01 foundation

- [x] Write always-active tests before implementation. Public usage:
  ```cpp
  auto value = Result<int>::success(42);
  CHECK(value.ok() && *value.value_if() == 42);
  auto maximum = parse_decimal<InputSeq>("18446744073709551615");
  CHECK(maximum.ok());
  CHECK(!checked_increment(*maximum.value_if()).ok());
  ```
  Cover success/error exclusivity (including Result<Error>), void, move-only extraction, no assignment that can create a valueless variant, ID/sequence separation, decimal invalid/sign/space/overflow rejection, generation identity, exact injected clock advance and monotonic real clock.
- [x] Compile `clang++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -I. tests/unit/foundation_test.cpp -o /private/tmp/netarena-foundation-test`; expect missing public header before implementation.
- [x] Implement `Result<T>::success(T)`, `failure(Error)`, `ok() const noexcept`, const/nonconst `value_if() noexcept`, `error_if() const noexcept`; delete assignment, distinguish alternatives by index. Implement explicit `StringId<Tag>` and `Sequence<Tag>`, `to_decimal`, `parse_decimal<SequenceType>`, `checked_increment`, and IClock.
- [x] Run foundation test; expect exit 0 with explicit check count. Review invariant/exception behavior.

## Task 2: M02 contracts

- [x] Write tests using `Envelope{...}.kind()`, `MessagePayload`, `validate(Vec2/Rect/Endpoint/InputFrame)`, optional transport statistics and owned event bytes. Verify every planned message maps to its kind, no ActorContext in payload, no client dt, and recovery/map/result fields are representable.
- [x] Compile `clang++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -I. tests/unit/contracts_test.cpp -o /private/tmp/netarena-contracts-test`; expect missing public contract header before implementation.
- [x] Implement all DTOs and message variants in existing interface design sections 2.1–2.3. Add named PublicIdentity, MatchRules and ActionStatus for previously schematic fields. Use finite min/max Rect coordinates. Derive kind via variant visitation, never mutable duplicated state.
- [x] Run both test programs; expect exit 0. Compile each public header separately.

## Task 3: Build, demonstration and records

- [x] Create CMake interface library `netarena_shared`, `netarena_foundation_demo`, two test executables, generated standalone-header object target; no dependency downloads. Generate version header from VERSION.txt.
- [x] Add Debug/Release configure/build/test presets with Unix Makefiles and `BUILD_TESTING=ON`; write demonstration that increments InputSeq, samples IClock and constructs Hello without secrets.
- [x] Run `cmake --preset debug && cmake --build --preset debug && ctest --preset debug`; repeat for release. Run both demonstrations and verify clear non-server label.
- [x] Record actual tools, red/green results, API additions, boundary-validation responsibility and deliberate WP-02/WP-07/WP-09 deferrals. No fake GameEvent/TraceRecord/ITraceSink/ITokenProvider placeholders.
- [x] Update only WP-01 completion dated 2026-10-18; leave W01, G0, WP-02 incomplete. Run `git diff --check`, self-review and commit all WP-01 files on `codex/netarena-wp01` without push.
