#include "client/godot/native/netarena_bridge.hpp"
#include <godot_cpp/core/class_db.hpp>
namespace netarena {
using namespace godot;
namespace {
String text(const std::string& value) { return String::utf8(value.data(), static_cast<std::int64_t>(value.size())); }
Result<void> invalid() { return Result<void>::failure({ErrorCode::InvalidArgument, "invalid argument", std::nullopt}); }
}
NetArenaBridge::NetArenaBridge() : runtime_(make_interop_runtime()) {}
NetArenaBridge::~NetArenaBridge() = default;
void NetArenaBridge::_bind_methods() {
    ClassDB::bind_method(D_METHOD("connect_server", "host", "port", "name", "protocol_version"), &NetArenaBridge::connect_server, DEFVAL(1));
    ClassDB::bind_method(D_METHOD("poll"), &NetArenaBridge::poll);
    ClassDB::bind_method(D_METHOD("send_ping", "nonce"), &NetArenaBridge::send_ping);
    ClassDB::bind_method(D_METHOD("disconnect_server"), &NetArenaBridge::disconnect_server);
}
Dictionary NetArenaBridge::result(const Result<void>& operation) const {
    const auto& view = runtime_->client().view();
    Dictionary data; data["state"] = client_state_name(view.state);
    if (view.identity) {
        data["player_id"] = text(view.identity->player_id.value());
        data["session_id"] = text(view.identity->session_id.value());
    }
    if (view.epoch) data["epoch"] = text(to_decimal(*view.epoch));
    if (view.last_pong) data["last_pong"] = text(std::to_string(*view.last_pong));
    Dictionary response; response["ok"] = operation.ok(); response["data"] = data;
    if (const auto* error = operation.error_if()) {
        Dictionary detail; detail["code"] = error_code_name(error->code);
        detail["message"] = error_code_name(error->code); response["error"] = detail;
    }
    return response;
}
Dictionary NetArenaBridge::connect_server(const String& host, std::int64_t port, const String& name, std::int64_t version) {
    if (port < 1 || port > 65535 || version < 1 || version > 65535 || host.length() > 15 || name.length() > 48)
        return result(invalid());
    const auto host_bytes = host.utf8(); const auto name_bytes = name.utf8();
    return result(runtime_->client().connect({std::string(host_bytes.get_data(), host_bytes.length()), static_cast<std::uint16_t>(port)},
        std::string(name_bytes.get_data(), name_bytes.length()), static_cast<std::uint16_t>(version)));
}
Dictionary NetArenaBridge::poll() { return result(runtime_->client().update()); }
Dictionary NetArenaBridge::send_ping(const String& nonce) {
    if (nonce.length() < 1 || nonce.length() > 20) return result(invalid());
    const auto bytes = nonce.utf8(); const auto parsed = parse_decimal<InputSeq>(std::string_view(bytes.get_data(), bytes.length()));
    if (!parsed.ok()) return result(invalid());
    return result(runtime_->client().send_ping(parsed.value_if()->value()));
}
Dictionary NetArenaBridge::disconnect_server() { return result(runtime_->client().disconnect()); }
}
