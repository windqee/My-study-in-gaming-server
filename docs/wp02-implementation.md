# WP-02 实施与验证记录

实际完成日期：2026-10-19（Asia/Shanghai）。范围依据：[WP-02 细化计划](superpowers/plans/2026-10-18-wp02-networking.md)、[路线图](superpowers/plans/2026-10-17-netarena-roadmap.md)。

## 1. 交付结果与边界

已实现 20 种共享 DTO 的 JSON 编解码、真实 ENet 适配器、引擎无关的最小客户端、独立互通服务端和 Godot 原生桥接。Godot 实际进程完成连接、Hello/Welcome、Heartbeat/Pong、主动断开；协议版本 2 连接版本 1 服务端得到 ProtocolMismatch。

本轮完成 WP-02 和 G0 的 macOS 工程互通验收。演示服务端仅监听回环地址，签发临时演示身份；不提供认证、恢复凭证、房间、游戏规则或对外部署能力。InteropClient 是 M09/M10 的最小接线，不冒充后续完整 ClientRuntime。协议能够编码某种业务消息不表示对应业务已实现。

## 2. 解耦与依赖

```text
Godot 面板 → NativeLoader（显式加载扩展）→ NetArenaBridge → InteropRuntime 抽象
                               ↓ 组合入口
                         InteropClient → ProtocolCodec → JSON（私有）
                               ↓ ITransport
                         EnetTransport → ENet（私有）

C++ interop_server → ProtocolCodec + EnetTransport
以上模块 → shared/contracts → shared/foundation
```

客户端借用 ITransport，传输必须比客户端活得更久。原生扩展的组合入口拥有两者并保证析构次序；桥接只调用客户端接口。JSON/ENet 不出现在引擎无关的公开头文件中；Godot 类型只用于桥接。依赖均由组合入口装配，不通过全局业务对象访问。

## 3. 主要接口、参数与返回值

完整签名以源码为准，未来设计草案与本表有差异时，本表描述当前实现。

### ProtocolCodec

文件：`shared/protocol/protocol_codec.hpp`；实现拆为协议/信封及内部 Schema 文件。

| 接口 | 参数 | 返回值与语义 |
| --- | --- | --- |
| `ProtocolCodec(limits)` | `CodecLimits`，默认协议版本 1 | 配置非法抛出 `invalid_argument`，属于可信启动配置错误 |
| `ProtocolCodec(clock, limits)` | 借用 `const IClock&`，必须比 Codec 长寿 | 便于恢复剩余时间的可重复校验 |
| `decode(bytes, direction)` | `ByteView`、ClientToServer/ServerToClient | `Result<Envelope>`；输入失败返回结构化错误 |
| `encode(message, direction)` | `const Envelope&`、方向 | `Result<Bytes>`；先验证再序列化 |
| `validate(message, direction)` | 信封及方向 | `Result<void>`；检查字段和跨字段一致性 |

默认限制：包 16,384 字节、名字 48 字节、request_id/业务 ID/令牌各 64 字节、错误文本 256 字节、嵌套深度 32、障碍 32、玩家 4、金币 64、恢复剩余时间 60,000 毫秒。长度按 UTF-8 字节计算。

包长度在 DOM 解析前检查；拒绝重复键、未知字段、非法 UTF-8、错误类型、非有限数、越界集合及不合方向的消息。uint64 使用无前导零的十进制字符串。信封和负载的身份、match、epoch 必须一致；恢复 WAITING/RUNNING/FINISHED 各有明确必填关系。单调时间不跨进程传输，转换为相对毫秒并检查溢出。错误不回显原始包、令牌或解析器文本。完整字段见 [protocol-v1.md](../schemas/protocol-v1.md)。

### EnetTransport

文件：`adapters/transport/enet_transport.hpp/.cpp`，通过 PIMPL 实现 `ITransport`。

配置：`TransportConfig{role, max_connections=8, max_packet_bytes=16384, max_pending_send_bytes_per_connection=262144, connect_timeout_ms=3000}`。单实例限制在同一线程调用；不同实例可位于不同线程。

| 接口 | 参数 | 返回值与语义 |
| --- | --- | --- |
| `listen(local)` | `Endpoint{numeric_ipv4, port}` | `Result<void>`；本适配器允许端口 0 申请临时端口 |
| `connect(remote)` | 非零端口的数字 IPv4 | `Result<ConnectionId>`；异步连接，完成由 poll 报告 |
| `poll(max_events, budget)` | 事件数上限、单调时间预算 | `Result<vector<TransportEvent>>`；不阻塞等待网络，接收字节由事件拥有 |
| `send(id, channel, bytes)` | 连接代际、通道、借用字节 | `Result<SendReceipt>`；拷贝入队，成功不表示对端收到 |
| `close(id, reason)` | 连接 ID、关闭原因 | `Result<void>`；排空已接收的可靠发送后断开，超时强制结束 |
| `stats(id)` | 连接 ID | `optional<TransportStats>`；无连接时缺失，不可知指标保持缺失 |
| `bound_endpoint()` | 无 | `optional<Endpoint>`；可读取服务器实际临时端口 |

Control/Input 对应 ENet 通道 0/1，可靠有序；Snapshot 对应通道 2，使用不可靠序号和分片。为避免 ENet 在不可靠序号耗尽后自动升级可靠发送，该边界返回 InvalidState，需要重连；尚未实现长会话快照序号轮换策略。地址只接受数字 IPv4，不执行同步 DNS。

发送队列按连接有界，空包拒绝；包释放回调归还计数。ConnectionId 包含槽位与代际，旧 ID 不可误发到新连接。全局 ENet 初始化计数受锁保护，实例持有自己的 host/peer；接收包 RAII 释放。关闭前的最终可靠错误消息有专门回归测试。统计中发送量/丢包不可知时为缺失，RTT 需要实际 ACK 样本。

### InteropClient 与 Godot 桥接

文件：`client/core/interop_client.hpp/.cpp`、`client/godot/native/`。

状态：Disconnected → Connecting → Handshaking → Ready → Closing → Disconnected；错误进入 Failed。连接/握手/关闭阶段有 5 秒期限。Ready 状态同一时间只允许一个待回执 ping。

| 接口 | 参数 | 返回值与语义 |
| --- | --- | --- |
| `InteropClient(transport)` | 借用 `ITransport&` | 不拥有传输；禁止复制 |
| `connect(endpoint, display_name, protocol_version=1)` | 端点、显示名、版本 | `Result<void>`；启动异步连接 |
| `update()` | 无 | `Result<void>`；推进网络和协议状态 |
| `send_ping(nonce)` | `uint64_t` | `Result<void>`；只有 Ready 可发送 |
| `disconnect()` | 无 | `Result<void>`；开始关闭 |
| `view()` | 无 | `const ClientView&`；含状态、公开身份、epoch、最近 Pong、错误 |
| `NetArenaBridge.connect_server(host, port, name, protocol_version=1)` | Godot String/int | Dictionary：`ok`、`data`，失败时 `error` |
| `NetArenaBridge.poll()` | 无 | 同上，更新后状态 |
| `NetArenaBridge.send_ping(nonce)` | 十进制 Godot String | 同上；避免跨语言整数精度损失 |
| `NetArenaBridge.disconnect_server()` | 无 | 同上，发起断开后的状态 |

`scripts/native_loader.gd` 的 `ensure_loaded() -> bool` 调用 GDExtensionManager，接受加载成功或已加载状态，并确认 NetArenaBridge 已注册；失败时测试非零退出，面板显示缺库提示并停止轮询。两入口均在实例化前调用。桥接不暴露令牌；ID/epoch/nonce 以字符串传给脚本。互通服务端参数为 `--port`（允许 0）和 `--max-seconds`（默认 60，上限 3600），启动输出实际端口，信号处理负责退出清理。具体启动步骤见 [README](../README.md)。

## 4. 构建与依赖

| 项目 | 实测版本 |
| --- | --- |
| 平台 | macOS 26.3.2，arm64 |
| C++ 编译器 | AppleClang 21.0.0，clang-2100.0.123.102 |
| CMake / Make | 3.31.10 / GNU Make 3.81 |
| Python | 3.14.8；脚本最低要求 3.12 |
| ENet / JSON | 1.3.18 / nlohmann/json 3.12.0 |
| Godot | 4.5.1.stable.official.f62fdbde1 |
| godot-cpp | godot-4.5-stable，提交 e83fd0904c13356ed1d4c3d09f8bb9132bdc6b77 |

`deps.lock.json` 固定官方地址和 SHA-256；显式 bootstrap 下载、校验、解压。CMake 不联网，缺少依赖会说明准备方式。`--offline` 要求已校验缓存。`--godot-runtime` 仅支持 macOS，将官方引擎解压到项目内；`--godot` 只准备绑定。裁剪后的绑定包含 RefCounted 和 OS，后者由官方绑定实现所需。

原生动态库输出到 `client/godot/bin/`，Godot 项目仅配置 macOS；当前验证为开发构建，不是多平台发布包。第三方版权见 [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)。

## 5. 验证证据

按先失败后实现的方式覆盖新 Codec、传输和客户端行为，再进行规格审查和独立质量审查。测试检查不依赖 Release 下禁用的 assert。

| 检查 | 结果及覆盖 |
| --- | --- |
| Debug / Release | 各 7/7 CTest 通过；含原有 3 项及 protocol_codec、transport、interop_client、client_loopback |
| Godot Debug | 8/8 CTest 通过；额外包含真实 godot_interop |
| 无 Godot 导入缓存 | 每次测试均临时复制项目、排除 `.godot`，显式加载后运行真实验收，通过 |
| Codec | 20 种消息往返、方向/版本/长度/类型/深度/重复键、恢复状态、范围与整数边界 |
| ENet | 实际三通道及可靠标志、接收/发送所有权、排队上限、代际、超时、关闭排空、少通道对端 |
| 客户端 | 状态转移、nonce 匹配、断开与重连、错误版本；C++ 回环含 uint64 最大值 |
| 原生扩展 | 真正 Godot 加载动态库，nonce `9007199254740993` 无精度丢失 |
| 内存检查 | Codec/Transport 的 AddressSanitizer、UndefinedBehaviorSanitizer 检查通过；不代表全系统长期稳定性已验证 |
| 代码审查 | 独立规格与质量审查通过，未遗留阻塞项 |

真实引擎验收的关键输出：

```text
INTEROP_OK nonce=9007199254740993
VERSION_REJECTED
GODOT_INTEROP_PASS
```

Python 验收脚本给每个步骤设置超时、检查退出码和标记；服务端输出队列有界且跨线程读取使用锁保护快照，最终清理所有自己启动的子进程。输出内容读取有长度上限，临时输出文件不设磁盘配额。每次运行的完整 CTest 输出见本机 `build/<preset>/Testing/Temporary/LastTest.log`；生成日志不入库。

## 6. Godot 导入退出崩溃与处理

用户提供的 macOS 报告对应验收中启动的 Godot。堆栈位于 `Main::cleanup → CallQueue::flush → EditorHelp::_gen_extensions_docs → DocTools::generate`。

定位过程：空项目和仅脚本项目可退出；仅原生扩展项目可复现；进一步删掉业务方法，使用空 RefCounted 原生类仍在相同路径崩溃。因此证据指向本环境 Godot 4.5.1 的原生扩展文档生成/立即退出路径，而不是 ENet 握手逻辑。

复核发现 `--editor --quit-after 3` 只在已有缓存时通过，空缓存仍崩溃，因此没有保留固定帧数的规避方案。官方源码显示，EditorHelp 在线程读取文档后将扩展文档生成加入延迟队列，而 Main::cleanup 会关闭 GDExtension；结合堆栈，推断为退出阶段与延迟文档任务的生命周期冲突。依据：[EditorHelp](https://github.com/godotengine/godot/blob/4.5.1-stable/editor/doc/editor_help.cpp)、[Main](https://github.com/godotengine/godot/blob/4.5.1-stable/main/main.cpp)。

最终方案使用官方 [GDExtensionManager.load_extension](https://github.com/godotengine/godot/blob/4.5.1-stable/doc/classes/GDExtensionManager.xml) 在运行时显式加载扩展。当前项目没有外部导入素材，无需编辑器发现。面板和测试入口共享加载器；自动脚本每次复制不含 `.godot` 的临时项目，直接运行实际引擎并验证握手。首次运行由原来的“native extension missing”失败转为完整互通通过。

该方案移除了验收流程对编辑器退出路径的依赖，没有修补 Godot 引擎本身；若手动对该版本执行立即导入退出，仍可能复现引擎问题。没有忽略非零退出码，也没有用 C++ 回环替代 Godot 验收。原始系统报告包含机器信息，不提交仓库。

## 7. 下一步

WP-03 实现 MovementModel、MatchSimulation 和 Room，通过纯逻辑测试验证边界移动、争抢、准备、开始和结算。WP-04 再引入最小 SessionService、真正 ServerApp/ClientRuntime 和游戏画面。本轮不提前实现数据库、账号、预测、重连、AI 或性能基准。

G0 的本机互通已验证；Linux 服务端工具链和 Windows 客户端尚待试构建，不能将本轮结果视为跨平台支持证明。原路线图的 10 周日期仍为计划目标，按后续验证修订。
