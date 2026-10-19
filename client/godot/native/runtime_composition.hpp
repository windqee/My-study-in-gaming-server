#pragma once
#include "client/core/interop_client.hpp"
#include <memory>
namespace netarena {
// Keeps transport construction and third-party dependencies out of the bridge.
class InteropRuntime {
public:
    virtual ~InteropRuntime() = default;
    virtual InteropClient& client() noexcept = 0;
};
std::unique_ptr<InteropRuntime> make_interop_runtime();
}
