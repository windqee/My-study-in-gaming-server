#include "client/godot/native/runtime_composition.hpp"
#include "adapters/transport/enet_transport.hpp"
namespace netarena {
namespace {
class NativeRuntime final : public InteropRuntime {
public:
    InteropClient& client() noexcept override { return client_; }
private:
    EnetTransport transport_{};
    InteropClient client_{transport_};
};
}
std::unique_ptr<InteropRuntime> make_interop_runtime() { return std::make_unique<NativeRuntime>(); }
}
