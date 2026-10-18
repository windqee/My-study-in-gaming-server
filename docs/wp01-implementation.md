# WP-01 实施与验证记录

完成日期：2026-10-18。版本由仓库唯一手写来源 [`VERSION.txt`](../VERSION.txt) 提供；本次为初始开发版。工作包范围见[路线图](superpowers/plans/2026-10-17-netarena-roadmap.md)及[细化计划](superpowers/plans/2026-10-18-wp01-foundation.md)。

## 设计与实际接口

对照[接口设计第 1–2 节](服务端与客户端接口设计.md)：

| 设计内容 | 实际落点与说明 |
| --- | --- |
| M01 Result/Error | `shared/foundation/result.hpp`；完整 ErrorCode，工厂构造，成功/错误按 variant 索引区分，支持 Result<Error> 和 void；不提供隐式 bool |
| 强类型与序号 | `shared/foundation/types.hpp`；业务 ID 为独立字符串类型，Tick/InputSeq/SnapshotSeq/Epoch 为 uint64；严格完整十进制解析，允许前导零，拒绝符号/空白/溢出；递增到最大值后失败 |
| 时间 | `shared/foundation/clock.hpp`；IClock/SteadyClock，MonoTime 不代表 UTC，Duration 单位纳秒 |
| 核心 DTO 与全部 20 个消息 | `shared/contracts/messages.hpp`；所有字符串、容器和消息拥有数据，ActorContext 不属于 payload；Envelope.kind() 从 variant 推导 |
| ITransport | `shared/contracts/transport.hpp`；listen/connect/poll/send/close/stats 与设计签名一致，事件拥有数据，统计缺失保持 optional，SendReceipt 仅表示本地入队 |

具体化的 API 决定：

- `StringId<Tag>::value()` 显式读取字符串；`Sequence<Tag>::value()` 显式读取 uint64。业务 ID 的空值合法性由未来边界校验器处理。
- `parse_decimal<S>`、`to_decimal`、`checked_increment` 只接受四种序号类型。ConnectionId 由 slot/generation 组成。
- Result 删除拷贝/移动赋值，不开放 variant/emplace，避免可观察对象进入无有效 alternative 状态；构造抛出则不产生对象。非 const `value_if()` 支持取出 move-only 值，取出后 Result 仍是成功，但值可处于 moved-from 状态。
- Rect 采用 `min/max` 世界坐标，有限且两轴严格递增；零面积矩形被拒绝。
- 增加 `PublicIdentity{player_id,session_id}`、`MatchRules{movement,duration_ticks,pickup_radius}`、`ActionStatus{Applied,Duplicate,Rejected}`，具体化设计中 identity/rules/status。
- CloseReason 提供 Requested/RemoteClosed/Timeout/ProtocolError/TransportError；TransportStats 的 rtt、sent_bytes、received_bytes、queued_bytes、packet_loss_rate 均为 optional。
- SecretToken/TokenDigest 仅提供显式字节构造及 `bytes()` 借用读取；没有打印、隐式转换、自动序列化、密码学或内存擦除保证。SecretToken 只出现于 Resume/Welcome 握手字段。

Error 自身不保证消息已脱敏。边界调用者需限制消息长度、移除敏感信息。DTO 是可表示数据，不是已验证的协议包；WP-02 需要完整限制长度、合法枚举、方向、信封/负载一致性，以及 ResumeState 各阶段的必填字段。MemberView.reserve_until 是本机时间点，未来编码器应转换为相对毫秒。

## 实际验证

- TDD 红灯：先写 foundation_test，再直接 Clang 编译，因 `shared/foundation/result.hpp` 尚未实现而失败；实现后 56 个运行时 CHECK 通过。contracts_test 同样先编译，因 `shared/contracts/messages.hpp` 缺失而失败；实现后 53 个运行时 CHECK 通过。这两次红灯是缺失 API 的编译失败，不是运行时断言失败。
- CMake Debug 和 Release 均配置/构建成功；每个配置 CTest **3/3** 通过：两个单元测试程序及一个演示程序冒烟检查。109 个 CHECK 是两个程序内的检查数，不是 109 个独立测试用例。
- Debug/Release 均独立编译全部五个公开头文件，启用 `-Wall -Wextra -Wpedantic -Werror`。
- 覆盖 Result 的互斥、void、move-only，类型隔离，超过 JavaScript 精确整数范围和 uint64 最大值的无损往返，格式/溢出拒绝，四种序号无回绕，连接代际身份，手动时钟精确推进，真实时钟单调，20 个消息 kind 映射，owned 数据独立性，缺失 RTT 等统计，可选恢复/地图/结果，有限值校验及端口 0 拒绝。
- 两个配置的演示均输出 `foundations demonstration (no network server)`，InputSeq=42、protocol=1、Hello=1 与本机耗时；不输出令牌。
- 首轮 CMake 构建发现 macOS 大小写不敏感文件系统把根目录 `VERSION` 当作标准库 `<version>`。已改为 `VERSION.txt`，双配置完整重跑通过。

复现命令见 [README](../README.md)。CMake 配置不使用 FetchContent、下载或第三方运行时库。

## 验证环境

| 工具 | 本次实测 |
| --- | --- |
| 系统 | macOS 26.3.2（25D2150），arm64 |
| C++ | AppleClang 21.0.0（clang-2100.0.123.102；CMake compiler ID 21.0.0.21000099） |
| CMake / CTest | 3.31.10 |
| Make | GNU Make 3.81 |
| Python | 3.14.8，仅用于准备工具和编辑；程序运行无需 Python |

CMake 临时安装于 `/private/tmp/netarena-wp01-tools/runtime`，没有全局安装。工具包为 PyPI `cmake-3.31.10-py3-none-macosx_10_10_universal2.whl`，SHA-256 `ad697643a00d9ba85179590a383c4f7401169b55ebf4b8b2938daf28c6bdeb6d`。该工具路径不写入项目构建配置。未验证其他操作系统或编译器，不宣称跨平台验收完成。

## 有意延后

WP-02 接手 ProtocolCodec、真实传输实现、ENet/Godot 最小互通及完整协议校验。WP-07 再定义 ITokenProvider 的完整契约及受信任密码学适配器。WP-09 再定义 GameEvent、TraceRecord、ITraceSink 及证据格式，本次不放空壳占位。Python 包、场景执行器、游戏模拟、恢复逻辑与安全认证均未实现。

路线图只标记 WP-01；W01、G0、WP-02 继续未完成。
