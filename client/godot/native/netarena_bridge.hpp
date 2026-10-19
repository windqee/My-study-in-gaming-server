#pragma once
#include "client/godot/native/runtime_composition.hpp"
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
namespace netarena {
class NetArenaBridge : public godot::RefCounted {
    GDCLASS(NetArenaBridge, godot::RefCounted)
public:
    NetArenaBridge();
    ~NetArenaBridge() override;
    godot::Dictionary connect_server(const godot::String& host, std::int64_t port,
        const godot::String& name, std::int64_t protocol_version = 1);
    godot::Dictionary poll();
    godot::Dictionary send_ping(const godot::String& nonce);
    godot::Dictionary disconnect_server();
protected:
    static void _bind_methods();
private:
    godot::Dictionary result(const Result<void>& operation) const;
    std::unique_ptr<InteropRuntime> runtime_;
};
}
