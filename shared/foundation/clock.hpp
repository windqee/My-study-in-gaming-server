#pragma once

#include <chrono>

namespace netarena {
using MonoTime = std::chrono::steady_clock::time_point;
using Duration = std::chrono::nanoseconds;

class IClock {
public:
    virtual ~IClock() = default;
    [[nodiscard]] virtual MonoTime now() const noexcept = 0;
};

class SteadyClock final : public IClock {
public:
    [[nodiscard]] MonoTime now() const noexcept override { return std::chrono::steady_clock::now(); }
};
} // namespace netarena
