#include "shared/protocol/codec_detail.hpp"
#include <cstring>
namespace netarena {
namespace {
using namespace protocol_detail;
const SteadyClock default_clock;
Error invalid(ErrorCode code) {
    return {code, "protocol validation failed", std::nullopt};
}
void check_limits(const CodecLimits &l) {
    if (l.protocol_version == 0 || l.max_packet_bytes == 0 || l.max_packet_bytes > 1024 * 1024 ||
        l.max_name_bytes == 0 || l.max_name_bytes > 4096 || l.max_request_id_bytes == 0 ||
        l.max_request_id_bytes > 4096 || l.max_obstacles > 4096 || l.max_players == 0 ||
        l.max_players > 256 || l.max_coins > 65536 || l.max_id_bytes == 0 ||
        l.max_id_bytes > 4096 || l.max_token_bytes == 0 || l.max_token_bytes > 4096 ||
        l.max_depth == 0 || l.max_depth > 64 || l.max_error_bytes > 4096 ||
        l.max_reserve_ms > 60000)
        throw std::invalid_argument("invalid codec limits");
}
Result<Bytes> encode_at(const Envelope &e, Direction d, const CodecLimits &limits, MonoTime now) {
    if (e.protocol_version != limits.protocol_version ||
        (e.kind() == MessageKind::Hello &&
         std::get<Hello>(e.payload).version != limits.protocol_version))
        return Result<Bytes>::failure(invalid(ErrorCode::ProtocolMismatch));
    try {
        Context context{limits, now, false};
        auto copy = e;
        auto json = write_envelope(copy, context, d);
        const auto packet = json.dump();
        preflight(packet, limits);
        Bytes bytes(packet.size());
        std::memcpy(bytes.data(), packet.data(), packet.size());
        return Result<Bytes>::success(std::move(bytes));
    } catch (const Invalid &) {
        return Result<Bytes>::failure(invalid(ErrorCode::InvalidArgument));
    } catch (const Json::exception &) {
        return Result<Bytes>::failure(invalid(ErrorCode::InvalidArgument));
    }
}
} // namespace
ProtocolCodec::ProtocolCodec(CodecLimits limits) : ProtocolCodec(default_clock, limits) {}
ProtocolCodec::ProtocolCodec(const IClock &clock, CodecLimits limits)
    : limits_(limits), clock_(&clock) {
    check_limits(limits);
}
Result<Bytes> ProtocolCodec::encode(const Envelope &e, Direction d) const {
    return encode_at(e, d, limits_, clock_->now());
}
Result<void> ProtocolCodec::validate(const Envelope &e, Direction d) const {
    const auto encoded = encode_at(e, d, limits_, clock_->now());
    if (!encoded.ok())
        return Result<void>::failure(*encoded.error_if());
    return Result<void>::success();
}
Result<Envelope> ProtocolCodec::decode(ByteView bytes, Direction d) const {
    const auto now = clock_->now();
    try {
        require(!bytes.empty() && bytes.size() <= limits_.max_packet_bytes);
        const std::string_view packet(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        preflight(packet, limits_);
        std::vector<std::set<std::string>> objects;
        auto callback = [&](int, Json::parse_event_t event, Json &parsed) {
            if (event == Json::parse_event_t::object_start)
                objects.emplace_back();
            else if (event == Json::parse_event_t::key) {
                require(!objects.empty());
                require(objects.back().insert(parsed.get<std::string>()).second);
            } else if (event == Json::parse_event_t::object_end)
                objects.pop_back();
            return true;
        };
        auto json = Json::parse(packet, callback);
        require(json.is_object() && json.contains("protocol_version"));
        Context context{limits_, now, true};
        std::uint16_t version{};
        value(json["protocol_version"], version, context);
        if (version != limits_.protocol_version)
            return Result<Envelope>::failure(invalid(ErrorCode::ProtocolMismatch));
        // Version disagreement in Hello is a compatibility error, not a generic malformed packet.
        if (json.contains("kind") && json["kind"] == "Hello" && json.contains("payload") &&
            json["payload"].is_object() && json["payload"].contains("version")) {
            std::uint16_t hello_version{};
            value(json["payload"]["version"], hello_version, context);
            if (hello_version != limits_.protocol_version)
                return Result<Envelope>::failure(invalid(ErrorCode::ProtocolMismatch));
        }
        return Result<Envelope>::success(read_envelope(json, context, d));
    } catch (const Invalid &) {
        return Result<Envelope>::failure(invalid(ErrorCode::MalformedPacket));
    } catch (const Json::exception &) {
        return Result<Envelope>::failure(invalid(ErrorCode::MalformedPacket));
    }
}
} // namespace netarena
