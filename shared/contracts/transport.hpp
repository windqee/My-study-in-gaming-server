#pragma once

#include "shared/contracts/messages.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace netarena {

enum class Channel { Control, Input, Snapshot };
enum class CloseReason { Requested, RemoteClosed, Timeout, ProtocolError, TransportError };
struct Connected { ConnectionId id; };
struct Received { ConnectionId id; Channel channel; Bytes bytes; };
struct Disconnected { ConnectionId id; CloseReason reason; };
struct ConnectFailed { ConnectionId id; Error error; };
using TransportEvent = std::variant<Connected, Received, Disconnected, ConnectFailed>;

// Only confirms acceptance into the local send queue, never remote delivery.
struct SendReceipt { std::size_t queued_bytes{}; };
struct TransportStats {
    std::optional<Duration> rtt;
    std::optional<std::uint64_t> sent_bytes;
    std::optional<std::uint64_t> received_bytes;
    std::optional<std::size_t> queued_bytes;
    std::optional<double> packet_loss_rate;
};

class ITransport {
public:
    virtual ~ITransport() = default;
    virtual Result<void> listen(const Endpoint& local) = 0;
    // Returns a local ID; actual connection completion arrives through poll().
    virtual Result<ConnectionId> connect(const Endpoint& remote) = 0;
    // Nonblocking; constrained by event count and elapsed time budget.
    virtual Result<std::vector<TransportEvent>> poll(std::size_t max_events, Duration budget) = 0;
    // Copy bytes before returning; ByteView cannot outlive the call.
    virtual Result<SendReceipt> send(ConnectionId id, Channel channel, ByteView bytes) = 0;
    // Idempotent local request; does not acknowledge remote closure.
    virtual Result<void> close(ConnectionId id, CloseReason reason) = 0;
    [[nodiscard]] virtual std::optional<TransportStats> stats(ConnectionId id) const = 0;
};

} // namespace netarena
