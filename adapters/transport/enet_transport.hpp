#pragma once
#include "shared/contracts/transport.hpp"
#include <memory>

namespace netarena {
enum class TransportRole { Server, Client };
struct TransportConfig {
    TransportRole role{TransportRole::Client};
    std::size_t max_connections{8};
    std::size_t max_packet_bytes{16384};
    std::size_t max_pending_send_bytes_per_connection{262144};
    std::uint32_t connect_timeout_ms{3000};
};

// Each instance is confined to one thread; separate instances may use separate
// threads. Addresses are numeric IPv4 only (no synchronous DNS in this API).
class EnetTransport final : public ITransport {
  public:
    explicit EnetTransport(TransportConfig config = {});
    ~EnetTransport() override;
    EnetTransport(const EnetTransport &) = delete;
    EnetTransport &operator=(const EnetTransport &) = delete;
    // Specialization: port zero requests an ephemeral bind, unlike validate(Endpoint).
    Result<void> listen(const Endpoint &local) override;
    Result<ConnectionId> connect(const Endpoint &remote) override;
    Result<std::vector<TransportEvent>> poll(std::size_t max_events, Duration budget) override;
    // Control/Input are reliable ordered (wire channels 0/1); Snapshot uses
    // unreliable sequencing and fragmentation (wire channel 2). Empty packets
    // are rejected. Snapshot sequence exhaustion requires a new connection;
    // ENet's implicit promotion to reliable delivery is deliberately disabled.
    Result<SendReceipt> send(ConnectionId id, Channel channel, ByteView bytes) override;
    // Drain accepted reliable sends before disconnecting, bounded by the
    // configured connection timeout. Closing peers no longer accept new sends.
    Result<void> close(ConnectionId id, CloseReason reason) override;
    [[nodiscard]] std::optional<TransportStats> stats(ConnectionId id) const override;
    [[nodiscard]] std::optional<Endpoint> bound_endpoint() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace netarena
