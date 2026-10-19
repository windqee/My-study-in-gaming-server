# WP-02 协议、传输与 Godot 互通实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development or executing-plans. Execute tasks with test-driven-development; review spec then code quality before integration.

**Goal:** 在真实 Godot 原生扩展与 C++ 进程之间完成连接、版本握手、可靠消息往返与主动断开，错误版本获得明确错误。

**Architecture:** M03 负责有界 JSON 编解码，M04 用 ENet 实现 ITransport。独立客户端核心借用 ITransport，组合入口拥有适配器；Godot 桥接只展示客户端状态。互通服务端是工程验收程序，不实现房间、授权或游戏规则。

**Tech Stack:** C++20、CMake 3.31.10、ENet 1.3.18、nlohmann/json 3.12.0、godot-cpp godot-4.5-stable 固定提交、Godot 4.5.1。依赖显式获取、哈希锁定，配置不联网。

## 边界和决策

已有需求/接口/路线图及用户要求完成 WP-02 构成实现依据。保留 WP-01 基础测试。公开头不暴露 JSON、ENet 或 Godot 类型；Godot 专用桥接头除外。仅互通程序可签发本次演示临时身份，不提供登录认证、恢复凭证、房间或游戏能力。

JSON 必须先限制字节数及深度，再解析字段；错误不回显原始包/令牌。所有业务 ID 为有界字符串；uint64 序号/nonce/修订号为十进制字符串。字段、方向、枚举、有限数、集合大小、协议版本及阶段相关必填关系由 Codec 验证。认证仍由未来 SessionService 负责。

Control/Input 可靠有序，Snapshot 不可靠有序；发送成功仅本地入队。poll 不阻塞并受事件数/时间预算约束。连接 ID 含代际，过期连接不能重用，close 幂等。依赖未准备时给出明确准备命令，不隐式下载。

## 任务 1：ProtocolCodec

文件：shared/protocol/protocol_codec.hpp、protocol_codec.cpp（按 DTO 分组可拆内部文件），tests/unit/protocol_codec_test.cpp、schemas/protocol-v1.md。

- [x] 先写 API 不存在的红灯：Hello 双向 encode/decode、错误版本 ProtocolMismatch、非法 JSON/方向/非有限浮点/uint64 溢出/超限拒绝。
- [x] 实现 Direction、CodecLimits 与 ProtocolCodec::encode/decode/validate。JSON 依赖仅位于私有实现文件。覆盖已声明的 20 个消息，禁止未知字段、重复键和任意 payload。
- [x] 对恢复 WAITING/RUNNING/FINISHED 必填关系、信封/负载 ID 与 epoch 一致性做测试；单调时间仅在线上以相对毫秒表达，通过注入 IClock 转换。
- [x] 运行 protocol_codec_test 与 WP-01 回归；记录真实红绿结果，完成规格和代码质量检查。

## 任务 2：EnetTransport

文件：adapters/transport/enet_transport.hpp/.cpp、tests/integration/transport_test.cpp；私有 PIMPL 隔离 ENet。

- [x] 先写失败测试，建立两个真实本地端点并期望 Connected 事件。
- [x] 实现配置验证、RAII 初始化/销毁、连接映射、非阻塞 poll、有界发送、通道映射、close/stats；限制 host 最大包大小，失败不泄漏资源。
- [x] 验证三通道实际收发、ByteView 拷贝、关闭双方事件、重建后旧 ID 拒绝、连接超时、错误参数和队列上限。
- [x] 在 CTest 设置有限超时；通过后审查。

## 任务 3：最小客户端与 Godot 原生桥接

文件：client/core/interop_client.hpp/.cpp、apps/interop_server.cpp、client/godot/native/、client/godot/project.godot、client/godot/scripts/、tests/integration/run_godot_interop.py、cmake/Dependencies.cmake、tools/bootstrap_dependencies.py、deps.lock.json。

- [x] 先用客户端状态检查确认异步 connect/握手/ping/disconnect 行为；实例只借用传输，组合入口拥有传输。
- [x] 构建 GDExtension，桥接提供 connect_server/poll/send_ping/disconnect_server 的结构化结果；提供最小可见面板与 headless 自动脚本。
- [x] 运行真实 Godot 进程 → C++ 服务端：Hello/Welcome、Heartbeat/Pong、断开，脚本须断言状态和 nonce；再运行错误版本案例并断言 ProtocolMismatch。总超时强制清理所有子进程。
- [x] 独立 C++ 客户端/服务器集成测试补充引擎外复用证据；不伪造 Godot 成功日志。

## 验证与交付

- [x] Debug/Release CMake 构建及 CTest（含全部旧测试）通过；真实 Godot headless 验收单独记录工具版本与输出。
- [x] 保存官方来源、依赖哈希与许可说明；README 给出从干净检出准备依赖、构建、启动、测试的完整命令。
- [x] 更新 docs/wp02-implementation.md 与路线图实际完成日期，仅有实测证据后标记 WP-02/G0。其他平台尚未验证需明确注明。
- [x] 规格审查、质量审查、修复后最终验证，本地集成，不自动推送 GitHub。

实际完成：2026-10-19。验证与边界见 [WP-02 实施记录](../../wp02-implementation.md)。
