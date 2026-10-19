#include "client/core/interop_client.hpp"
#include <chrono>
namespace netarena {
const char* client_state_name(ClientState state) noexcept {
    switch (state) {
#define STATE(x) case ClientState::x: return #x;
    STATE(Disconnected) STATE(Connecting) STATE(Handshaking) STATE(Ready) STATE(Closing) STATE(Failed)
#undef STATE
    }
    return "Failed";
}
const char* error_code_name(ErrorCode code) noexcept {
    switch (code) {
#define ERROR_NAME(x) case ErrorCode::x: return #x;
    ERROR_NAME(InvalidArgument) ERROR_NAME(InvalidState) ERROR_NAME(ProtocolMismatch)
    ERROR_NAME(MalformedPacket) ERROR_NAME(Unauthorized) ERROR_NAME(NotFound)
    ERROR_NAME(AlreadyExists) ERROR_NAME(RoomFull) ERROR_NAME(StaleMatch) ERROR_NAME(StaleEpoch)
    ERROR_NAME(Expired) ERROR_NAME(SequenceGap) ERROR_NAME(RateLimited) ERROR_NAME(QueueFull)
    ERROR_NAME(WouldBlock) ERROR_NAME(Timeout) ERROR_NAME(Unsupported) ERROR_NAME(IoError)
    ERROR_NAME(CorruptArtifact) ERROR_NAME(IncompleteEvidence) ERROR_NAME(Cancelled) ERROR_NAME(InternalError)
#undef ERROR_NAME
    }
    return "InternalError";
}
namespace {
Result<void> rejected(ErrorCode code) { return Result<void>::failure({code, error_code_name(code), std::nullopt}); }
}
InteropClient::~InteropClient() { if (connection_) (void)transport_.close(*connection_, CloseReason::Requested); }
Result<void> InteropClient::fail(ErrorCode code) {
    view_.state = ClientState::Failed;
    view_.identity.reset(); view_.epoch.reset(); pending_ping_.reset();
    view_.error = Error{code, error_code_name(code), std::nullopt};
    if (connection_) (void)transport_.close(*connection_, CloseReason::ProtocolError);
    return rejected(code);
}
Result<void> InteropClient::connect(const Endpoint& endpoint, std::string name, std::uint16_t version) {
    if (view_.state != ClientState::Disconnected && view_.state != ClientState::Failed) return rejected(ErrorCode::InvalidState);
    if (!validate(endpoint).ok() || endpoint.host.size() > 15 || name.empty() || name.size() > 48 || version == 0)
        return rejected(ErrorCode::InvalidArgument);
    CodecLimits limits; limits.protocol_version = version;
    ProtocolCodec proposed{limits};
    Envelope hello; hello.protocol_version = version; hello.payload = Hello{version, name};
    auto valid = proposed.validate(hello, Direction::ClientToServer);
    if (!valid.ok()) return rejected(valid.error_if()->code);
    if (connection_) { (void)transport_.close(*connection_, CloseReason::Requested); connection_.reset(); }
    auto connected = transport_.connect(endpoint);
    if (!connected.ok()) return fail(connected.error_if()->code);
    codec_ = proposed; version_ = version; display_name_ = std::move(name);
    connection_ = *connected.value_if(); pending_ping_.reset(); view_ = {};
    view_.state = ClientState::Connecting;
    deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    return Result<void>::success();
}
Result<void> InteropClient::send(Envelope envelope) {
    envelope.protocol_version = version_;
    auto bytes = codec_.encode(envelope, Direction::ClientToServer);
    if (!bytes.ok()) return rejected(bytes.error_if()->code);
    auto queued = transport_.send(*connection_, Channel::Control, *bytes.value_if());
    if (!queued.ok()) return rejected(queued.error_if()->code);
    return Result<void>::success();
}
Result<void> InteropClient::send_ping(std::uint64_t nonce) {
    if (view_.state != ClientState::Ready) return rejected(ErrorCode::InvalidState);
    if (pending_ping_) return rejected(ErrorCode::WouldBlock);
    Envelope envelope; envelope.session_id = view_.identity->session_id; envelope.epoch = view_.epoch;
    envelope.payload = Heartbeat{nonce};
    auto sent = send(std::move(envelope)); if (sent.ok()) pending_ping_ = nonce;
    return sent;
}
Result<void> InteropClient::disconnect() {
    if (!connection_ || view_.state == ClientState::Disconnected) return Result<void>::success();
    auto closed = transport_.close(*connection_, CloseReason::Requested);
    if (!closed.ok()) return fail(closed.error_if()->code);
    view_.state = ClientState::Closing; pending_ping_.reset();
    deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    return Result<void>::success();
}
Result<void> InteropClient::update() {
    auto events = transport_.poll(32, std::chrono::milliseconds(2));
    if (!events.ok()) return fail(events.error_if()->code);
    for (const auto& event : *events.value_if()) {
        const auto id = std::visit([](const auto& item) { return item.id; }, event);
        if (!connection_ || id != *connection_) continue;
        if (std::holds_alternative<Disconnected>(event)) {
            connection_.reset(); view_.identity.reset(); view_.epoch.reset(); pending_ping_.reset();
            if (view_.state != ClientState::Failed) view_.state = ClientState::Disconnected;
        } else if (const auto* failed = std::get_if<ConnectFailed>(&event)) {
            connection_.reset(); return fail(failed->error.code);
        } else if (std::holds_alternative<Connected>(event)) {
            if (view_.state != ClientState::Connecting) continue;
            Envelope hello; hello.payload = Hello{version_, display_name_};
            auto sent = send(std::move(hello)); if (!sent.ok()) return fail(sent.error_if()->code);
            view_.state = ClientState::Handshaking;
        } else if (const auto* received = std::get_if<Received>(&event)) {
            if (view_.state == ClientState::Closing || view_.state == ClientState::Failed) continue;
            if (received->channel != Channel::Control) return fail(ErrorCode::MalformedPacket);
            auto decoded = codec_.decode(received->bytes, Direction::ServerToClient);
            // A rejection from the supported-version server can itself carry a
            // different envelope version. Preserve that specific mismatch.
            if (!decoded.ok()) return fail(decoded.error_if()->code);
            const auto& envelope = *decoded.value_if();
            if (const auto* error = std::get_if<ErrorReply>(&envelope.payload)) return fail(error->error.code);
            if (const auto* welcome = std::get_if<Welcome>(&envelope.payload)) {
                if (view_.state != ClientState::Handshaking) return fail(ErrorCode::InvalidState);
                view_.identity = welcome->identity; view_.epoch = welcome->epoch; view_.state = ClientState::Ready;
            } else if (const auto* pong = std::get_if<Pong>(&envelope.payload)) {
                if (view_.state != ClientState::Ready || !pending_ping_ || *pending_ping_ != pong->nonce)
                    return fail(ErrorCode::MalformedPacket);
                view_.last_pong = pong->nonce; pending_ping_.reset();
            } else return fail(ErrorCode::Unsupported);
        }
    }
    if ((view_.state == ClientState::Connecting || view_.state == ClientState::Handshaking || view_.state == ClientState::Closing) &&
        std::chrono::steady_clock::now() >= deadline_) return fail(ErrorCode::Timeout);
    if (view_.state == ClientState::Failed) return rejected(view_.error->code);
    return Result<void>::success();
}
} // namespace netarena
