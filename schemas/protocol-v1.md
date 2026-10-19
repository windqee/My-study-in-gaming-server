# NetArena protocol v1

M03 exchanges UTF-8 JSON over an already framed transport. `ProtocolCodec` owns
no socket, engine state, authentication state, or logging. Its public header is
`shared/protocol/protocol_codec.hpp`; JSON is a private implementation dependency.

## Envelope and scalar representation

Every packet is one object with required `protocol_version`, `kind`, `request_id`,
and `payload`. `session_id`, `epoch`, and `match_id` are optional and **omitted**
when absent; JSON `null` is never an optional value. `payload` is always an
object. All objects have closed key sets: missing required keys, unknown keys,
and duplicate keys (including nested duplicates) are rejected.

```json
{"protocol_version":1,"kind":"Move","request_id":"move-1","session_id":"s","epoch":"2","match_id":"m","payload":{"frame":{"match_id":"m","epoch":"2","seq":"1","direction":{"x":1,"y":0}}}}
```

* `protocol_version` and `Hello.version` are unsigned 16-bit JSON integers.
* All unsigned 64-bit values are **canonical decimal strings**: `0` or a
  nonzero digit followed by digits, at most `18446744073709551615`. This includes
  epoch, tick, snapshot/input sequence, remaining/duration ticks, nonce,
  `RoomView.revision`, and join order. Leading zeroes, signs, fractions,
  whitespace, exponent notation, and JSON numeric values are rejected. Zero
  sequences are structurally valid; application policy determines admission.
* Unsigned 32-bit values are JSON integers in `0..4294967295`, including
  `MapDefinition.revision`, coin IDs, scores, and tick rate. Integer fields do
  not accept floats such as `1.0`, negative numbers, or booleans.
* Vectors use `{ "x": number, "y": number }`. Coordinates and other floats must
  be finite and representable as C++ `float`. Booleans are JSON booleans.
* Business IDs are nonempty UTF-8 strings. Names are nonempty UTF-8 strings.
  Lengths count encoded UTF-8 bytes. `request_id` may be empty for unsolicited
  messages. Error messages and optional error fields may be empty.
* Tokens are nonempty, even-length **lowercase hexadecimal** strings, with two
  characters per byte. They occur only in `Resume.token` and optional
  `Welcome.recovery_token`. No generic credential serialization is provided.

`kind` is exactly one of the names in the message table. `phase` is `WAITING`,
`RUNNING`, `FINISHED`, or `CLOSED`; result reason is `TimeUp`, `CoinsExhausted`,
or `Aborted`; action status is `Applied`, `Duplicate`, or `Rejected`.
`Error.code` uses the exact `ErrorCode` enumerator name from
`shared/foundation/result.hpp`, not an ordinal or free-form string.

## Message payloads

Field names below are exact; `?` denotes an omitted optional field.

| Direction | Kind | Payload fields |
| --- | --- | --- |
| Client → server | Hello | version, display_name |
| Client → server | Resume | session_id, token, match_id |
| Client → server | CreateRoom | none |
| Client → server | JoinRoom | room_id |
| Client → server | SetReady | ready |
| Client → server | StartMatch | none |
| Client → server | LeaveRoom | none |
| Client → server | Move | frame: InputFrame |
| Client → server | Pickup | command: PickupCommand |
| Client → server | Heartbeat | nonce |
| Client → server | RequestFullSnapshot | none |
| Server → client | Welcome | identity: PublicIdentity, epoch, recovery_token? |
| Server → client | RoomUpdate | room: RoomView |
| Server → client | MatchStarted | map: MapDefinition, rules: MatchRules, snapshot: MatchSnapshot |
| Server → client | Snapshot | snapshot: MatchSnapshot |
| Server → client | ResumeState | room: RoomView, map?, rules?, snapshot?, result? |
| Server → client | ActionResult | action_id, status, score? |
| Server → client | MatchEnded | result: MatchResult |
| Server → client | Pong | nonce |
| Server → client | ErrorReply | error: Error |

## Nested object fields

| Object | Exact fields |
| --- | --- |
| PlayerProfile | id, display_name |
| InputFrame | match_id, epoch, seq, direction: Vec2 |
| PickupCommand | match_id, epoch, action_id, input_seq, coin_id |
| MoveState | position: Vec2, velocity: Vec2 |
| Rect | min: Vec2, max: Vec2 |
| MapDefinition | map_id, revision, bounds: Rect, obstacles: Rect[], spawn_points: Vec2[] |
| MovementConfig | speed, player_radius, tick_hz |
| PlayerState | player_id, move: MoveState, score, connected |
| CoinState | coin_id, position: Vec2, active |
| AckState | epoch, terminal_input_seq |
| MatchSnapshot | match_id, tick, snapshot_seq, players: PlayerState[], coins: CoinState[], remaining_ticks, phase, self_ack?: AckState |
| MemberView | profile: PlayerProfile, ready, connected, is_host, join_order, reserve_remaining_ms? |
| RoomView | room_id, match_id, phase, revision, members: MemberView[] |
| PlayerScore | player_id, score |
| MatchResult | match_id, reason, scores: PlayerScore[], winners: PlayerId[] |
| PublicIdentity | player_id, session_id |
| MatchRules | movement: MovementConfig, duration_ticks, pickup_radius |
| Error | code, message, field? |

Rectangles require `min.x < max.x` and `min.y < max.y`. Maps require at least
one spawn point. Speed, player radius, and pickup radius are in `(0,10000]`;
tick rate is in `1..1000`; duration ticks are positive. Other finite coordinates
and unsigned values use their representation ranges. These structural bounds
do not determine whether a spawn, movement, or pickup is legal in the game.

Snapshot player IDs and coin IDs are unique. Room player IDs and join orders
are unique. Result score IDs and winner IDs are unique, and every winner has a
score entry. No winner/tie calculation is performed by the codec.

## Scope and recovery consistency

* `Hello` forbids envelope session, epoch, and match. Its version must equal the
  envelope version and the configured protocol version.
* `Resume` payload session/match is authoritative. Envelope session/match may
  be omitted or agree; envelope epoch is forbidden.
* All other client messages require envelope session and epoch. `Move` and
  `Pickup` additionally require envelope match; payload match and epoch must
  equal the corresponding envelope values.
* `Welcome` envelope session and epoch may be omitted or must agree with the
  identity and epoch in its payload. Other server messages may omit session
  and epoch, including unauthenticated `ErrorReply`.
* Whenever an envelope match is supplied, room, snapshot, recovery, and result
  match IDs must agree with it. A snapshot's optional self acknowledgment must
  agree with the envelope epoch if both exist.
* `MatchStarted` requires map, rules, and a `RUNNING` snapshot.
* `ResumeState` in `WAITING` carries only room. In `RUNNING` it requires map,
  rules, and snapshot, and forbids result. In `FINISHED` it additionally requires
  result. `CLOSED` recovery is rejected. Snapshot phase/match agree with room;
  result match agrees with room; room, snapshot, and any result describe the
  same player-ID set.

These checks do not authenticate a session or token, authorize an action,
check room membership against server state, or enforce gameplay/sequence rules.

## Limits and local time

Default `CodecLimits`:

| Limit | Default |
| --- | ---: |
| packet UTF-8 bytes | 16384 |
| name bytes | 48 |
| request ID bytes | 64 |
| business ID / error field bytes | 64 |
| token bytes (before hex encoding) | 64 |
| obstacles | 32 |
| players / members / scores / winners / spawn points | 4 |
| coins | 64 |
| JSON container nesting depth | 32 |
| error message bytes | 256 |
| reserve remaining milliseconds | 60000 |

Packet size and nesting are checked before recursive JSON DOM parsing. Duplicate
keys are rejected during parsing. Arrays and strings are bounded during typed
conversion. Encoding and validation apply the same limits, including total
packet size and depth. Malformed UTF-8 is rejected in both directions.

`MemberView.reserve_until` is a local monotonic deadline, never a wire timestamp.
The codec samples its injected `IClock` exactly once per public operation.
Encoding emits `reserve_remaining_ms = ceil(max(deadline-now, 0) / 1ms)`; a
positive duration above the configured maximum is rejected. Decoding adds the
bounded millisecond duration to that sample, rejecting time-point overflow.
Expired deadlines serialize as zero. Absence stays absent. The default clock
is process-local `SteadyClock`; an injected clock must outlive its codec.

Invalid constructor configuration throws `std::invalid_argument`; it is a
trusted startup configuration error, not a network-packet exception. Absolute
configuration ceilings are 1 MiB packet, 4096 name/request/ID/error/token bytes,
4096 obstacles, 256 players, 65536 coins, 64 nesting levels, and 60000 reserve
milliseconds. Packet/name/request/ID/token/depth/player limits must be positive.
Zero obstacle/coin/error/reserve limits intentionally disable those capacities.

`CodecLimits.protocol_version` defaults to **1** and cannot be zero. A different
value supports build/compatibility-test peers, including a version-2 test client;
it does not declare a second protocol schema. The deployed v1 peer rejects that
client with `ProtocolMismatch`. All version comparisons use the configured value.

## Errors and security boundary

`decode` returns `MalformedPacket` for syntax, type, key, range, direction, size,
UTF-8, and consistency failures, and `ProtocolMismatch` for unsupported numeric
versions. `encode` and `validate` return `InvalidArgument` for invalid DTOs and
`ProtocolMismatch` for version disagreements. Their errors use a fixed generic
message and never contain packet bytes, tokens, or parser exception text.
Allocation failure remains an unrecoverable exception for the process boundary.

The caller remains responsible for redacting user-supplied text in an outgoing
`ErrorReply` and for stripping credentials before tracing. Encoding an explicit
handshake is the only place the codec writes token bytes; it never logs packets.
