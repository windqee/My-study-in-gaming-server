#pragma once
#include "shared/contracts/messages.hpp"
#include <cstddef>
namespace netarena {
enum class Direction { ClientToServer, ServerToClient };
struct CodecLimits {
    std::uint16_t protocol_version{1};
    std::size_t max_packet_bytes{16384};
    std::size_t max_name_bytes{48};
    std::size_t max_request_id_bytes{64};
    std::size_t max_obstacles{32};
    std::size_t max_players{4};
    std::size_t max_coins{64};
    std::size_t max_id_bytes{64};
    std::size_t max_token_bytes{64};
    std::size_t max_depth{32};
    std::size_t max_error_bytes{256};
    std::uint32_t max_reserve_ms{60000};
};
class ProtocolCodec {
  public:
    explicit ProtocolCodec(CodecLimits limits = {});
    explicit ProtocolCodec(const IClock &clock, CodecLimits limits = {});
    [[nodiscard]] Result<Envelope> decode(ByteView bytes, Direction direction) const;
    [[nodiscard]] Result<Bytes> encode(const Envelope &message, Direction direction) const;
    [[nodiscard]] Result<void> validate(const Envelope &message, Direction direction) const;

  private:
    CodecLimits limits_;
    const IClock *clock_;
};
} // namespace netarena
