#pragma once
#include "shared/contracts/transport.hpp"
#include "shared/protocol/protocol_codec.hpp"
namespace netarena {
enum class ClientState { Disconnected, Connecting, Handshaking, Ready, Closing, Failed };
struct ClientView {
    ClientState state{ClientState::Disconnected};
    std::optional<PublicIdentity> identity;
    std::optional<Epoch> epoch;
    std::optional<std::uint64_t> last_pong;
    std::optional<Error> error;
};
[[nodiscard]] const char* client_state_name(ClientState state) noexcept;
[[nodiscard]] const char* error_code_name(ErrorCode code) noexcept;
// Owns protocol state; transport must outlive this client. No authentication or
// game/session service is implemented by this interoperability demonstration.
class InteropClient {
public:
    explicit InteropClient(ITransport& transport) : transport_(transport) {}
    ~InteropClient();
    InteropClient(const InteropClient&) = delete;
    InteropClient& operator=(const InteropClient&) = delete;
    Result<void> connect(const Endpoint&, std::string display_name, std::uint16_t protocol_version = 1);
    Result<void> update();
    Result<void> send_ping(std::uint64_t nonce);
    Result<void> disconnect();
    [[nodiscard]] const ClientView& view() const noexcept { return view_; }
private:
    Result<void> send(Envelope envelope);
    Result<void> fail(ErrorCode code);
    ITransport& transport_;
    ProtocolCodec codec_;
    ClientView view_;
    std::optional<ConnectionId> connection_;
    std::optional<std::uint64_t> pending_ping_;
    std::string display_name_;
    std::uint16_t version_{1};
    MonoTime deadline_{};
};
} // namespace netarena
