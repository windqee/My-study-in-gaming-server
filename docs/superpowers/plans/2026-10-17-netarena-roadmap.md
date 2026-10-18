# NetArena 实现排期与开发路线图（Implementation Plan）

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. 本文件是交付排期；进入某一工作包的编码前，再依据接口文档细化其代码修改与验证步骤。当前请求只编写计划，不启动实现。

**Goal:** 在 2026-12-27 前形成可玩的 C++ 联机游戏实验场 v0.1，具备客户端、自动测试、状态回放和 AI 场景生成能力。

**Architecture:** 以 C++ 权威房间服为主，Godot 原生桥接共享客户端通信与移动逻辑。Python 负责编排测试、故障实验和 AI；游戏规则与网络适配、文件记录、表现层和模型服务保持独立。

**Tech Stack:** C++20、Godot 4.x、Python 3、拟选 ENet、CMake、JSON/NDJSON。具体依赖小版本在第一周验证并锁定。

---

## 1. 排期基线

| 项目 | 安排 |
| --- | --- |
| 编写日期 | 2026-10-17 |
| 时区 | Asia/Shanghai（北京时间） |
| 假定开始日期 | 2026-10-19，周一 |
| 目标完成日期 | 2026-12-27，周日 |
| 总周期 | 10 周，70 个自然日 |
| 投入假设 | 单人，每周约 20–25 小时；计划容量按 22 小时/周计算 |
| 工时分配 | 每周约 18 小时安排开发与验证，4 小时用于复盘、修复与风险缓冲；合计 180＋40＝220 小时 |
| 已完成 | 项目需求、功能需求、模块解耦、主要接口设计文档 |
| 尚未开始 | 工程搭建、功能编码、互通验证、性能测量及发布 |
| 日期性质 | 计划目标，不是完成承诺；按实际可投入时间和关卡结果调整 |

本文回答“什么时候做、往什么方向做、做到什么程度”。日期与工时均为估计，第一周结束后必须根据实际互通、构建和学习成本重新评估。

关联文档：

- [项目需求说明书](../../项目需求说明书.md)
- [功能需求说明书](../../功能需求说明书.md)
- [模块解耦与依赖设计](../../模块解耦与依赖设计.md)
- [服务端与客户端接口设计](../../服务端与客户端接口设计.md)
- [测试回放与 AI 接口设计](../../测试回放与AI接口设计.md)

## 2. 实现方向与优先顺序

### 2.1 第一主线：游戏服务端能力

先完成一个可靠的单房间游戏闭环：协议与连接 → 临时身份 → 房间生命周期 → 固定 Tick → 权威移动与拾取 → 结算 → 重连。代码重点放在明确的状态、输入边界、幂等和资源生命周期。

首版保留单逻辑线程和一个异步记录线程。多房间、多进程、数据库和分布式调度不进入本轮 10 周计划。

### 2.2 第二主线：可观察的客户端同步

先让角色能联网移动，再增加预测、校正和插值。图形客户端与机器人共用 ClientRuntime，客户端与服务器共用 MovementModel；用可视化位置差异和输入确认状态解释同步行为。

首版采用一张 2D 地图和简单素材。每一项客户端工作都应支持“能玩、看得清同步过程、能演示错误”中的至少一项。

### 2.3 第三主线：可验证的测试和回放

先建立事件格式与可靠记录，再做机器人、场景、断言和故障注入，最后把失败证据接入状态回放和报告。没有证据完整性检查，就不能宣称测试通过或错误已复现。

回放首版查看历史状态；固定输入重演和跨平台确定性另列后续版本。

### 2.4 第四主线：受约束的 AI 辅助

场景 JSON 和纯校验器稳定后，接入自然语言生成。AI 输出有限动作及断言，经过校验后保存，由用户显式运行；测试执行器不依赖模型服务。

### 2.5 第五主线：求职与开源交付

从第 2 周开始保留演示材料，从第 6 周开始积累失败案例，第 9–10 周整理实测和发布包。面试材料重点解释实现决策、实际错误、定位证据及修复效果。

## 3. 里程碑与计划日期

| 里程碑 | 计划完成日期 | 届时能够展示的结果 | 未达标处理 |
| --- | --- | --- | --- |
| G0 工程与互通关卡 | 2026-10-25 | C++ 服务端与 Godot 原生桥接完成连接、编解码及断开；基础检查能运行 | 优先解决构建/传输问题，重排后续日期 |
| G1 首个可玩版本 | 2026-11-01 | 两人进入房间、移动、争抢金币并结算 | 不进入预测优化，先修复游戏闭环 |
| G2 同步与恢复版本 | 2026-11-15 | 预测、校正、插值、断线重连及调试视图可演示 | 继续修正状态与连接边界 |
| G3 自动测试版本 | 2026-11-29 | 机器人、弱网、断言和结构化产物形成闭环 | 优先修复测试可靠性，不用 AI 掩盖缺口 |
| G4 首版功能完整 | 2026-12-13 | 失败报告、状态回放、AI 场景生成和固定评测集完成 | 发布预览时明确缺项，完整 v0.1 顺延 |
| G5 候选发布版本 | 2026-12-20 | 稳定性、性能、关键回归通过，有候选发布包 | 只修阻塞问题，不追加玩法 |
| G6 开源交付就绪 | 2026-12-27 | 验证过的发布包、文档、演示视频及求职材料齐备 | 不以删掉验收项的方式满足日期 |

这里的“版本”是计划中的交付关卡。真正完成后才记录实际日期并标记通过。

## 4. 每周实现内容

每周默认 18 小时计划工作＋4 小时复盘/修复缓冲。每周结束须留下一份可运行结果或可复核证据，不以新增文件数量作为进度。

| 周次 | 开始日期 | 结束日期 | 实现方向与内容 | 模块 | 当周交付与验收 |
| --- | --- | --- | --- | --- | --- |
| W01 | 2026-10-19 | 2026-10-25 | 工程入口、依赖锁定、基础类型、协议最小集、ENet 与 Godot 原生桥接互通；尽早试构建目标客户端 | M01–M04，M09/M10 最小接线 | 连接、握手、收发、断开成功；非法版本被拒绝；保存工具链和依赖版本 |
| W02 | 2026-10-26 | 2026-11-01 | 最小会话、房间状态机、移动计算、权威拾取、计分和结算；把快照画到客户端 | M05–M10 基础部分 | 两客户端完整玩一局；单人开始和第五人加入被拒绝；金币只计一次 |
| W03 | 2026-11-02 | 2026-11-08 | 固定步长输入、输入队列与终结水位、快照序号、预测、校正及插值 | M06–M10 | 权威/预测/显示位置可比较；旧快照不覆盖新状态；输入积压有明确处理 |
| W04 | 2026-11-09 | 2026-11-15 | 连接 epoch、超时、10 秒恢复窗口、完整状态恢复、旧连接隔离、拾取幂等；完善调试视图 | M05、M07–M10 | 恢复后实体唯一且得分保持；过期不能恢复旧局；旧连接消息无效 |
| W05 | 2026-11-16 | 2026-11-22 | 事件/快照 Schema、有界异步记录、来源完整性、场景纯校验器、机器人本地控制接口 | M02、M11、M13 | 两个机器人通过相同协议完成一局；产物离线可读；非法场景在启动前拒绝 |
| W06 | 2026-11-23 | 2026-11-29 | 进程编排、动作依赖、程序断言、UDP 代理延迟/丢包、业务故障夹具、取消和超时清理 | M11、M12、M13 | 至少 6 个内置场景可重复运行；区分 FAIL/ERROR/TIMEOUT；测试结束无残留进程 |
| W07 | 2026-11-30 | 2026-12-06 | 归档索引、状态时间线、失败时刻跳转、报告和版本对比；整理三类故障案例 | M13、M14、M15、M10 回放展示 | 从失败报告进入对应历史状态；缺失证据不判 PASS；修复前后用同一案例验证 |
| W08 | 2026-12-07 | 2026-12-13 | 单一模型接入、自然语言转场景、最多两次修正、摘要与保存、20 条固定意图评测 | M16、M11 场景契约 | 有效场景可保存后独立运行；非法/不支持请求明确反馈；无模型核心流程通过 |
| W09 | 2026-12-14 | 2026-12-20 | 资源/生命周期检查、四人多局稳定性、性能测量、核心回归、候选包和快速开始试跑 | 全模块集成 | NFR 稳定性与基准要求有证据；性能报告说明环境；形成候选发布版本 |
| W10 | 2026-12-21 | 2026-12-27 | Windows/macOS 客户端及 Linux 服务端最终验证、中英文文档、许可清单、演示视频、发布和求职材料 | 交付工程 | 干净环境试跑；发布支持列表真实；1–3 分钟视频和可复核案例齐备 |

W01 的 M09/M10 仅为验证桥接和消息的最小接线，不提前开发完整界面。W02 可玩版本只要求基础权威同步，预测和插值在 W03 完成。这样可以尽早发现跨语言构建问题，同时控制每周范围。

## 5. 工作包、前置依赖与检查清单

工作包以周为承诺粒度，不把尚未验证的任务精确到每天。开发时每个工作包按“选定验收行为 → 补充针对性检查 → 实现最小功能 → 验证 → 更新文档和记录”推进。

| 工作包 | 计划区间 | 主要实现方向 | 前置条件 | 完成判据 |
| --- | --- | --- | --- | --- |
| WP-01 | W01 | 工程入口、构建配置、版本记录、Result/ID/时间类型与共享契约 | 已有需求和接口设计 | 可构建最小程序，基础契约检查可运行 |
| WP-02 | W01 | ProtocolCodec、EnetTransport、原生扩展最小桥接 | WP-01 的最小契约 | C++ 与 Godot 收发及断开通过；协议版本不符有明确错误 |
| WP-03 | W02 | MovementModel、MatchSimulation、房间基本状态机 | WP-01 | 无网络情况下验证移动边界、争抢、准备、开始、结算 |
| WP-04 | W02 | 最小 SessionService、ServerApp、基础 ClientRuntime 与游戏画面 | WP-02、WP-03 | 双人完整一局和四人容量限制通过 |
| WP-05 | W03 | InputBuffer、TickScheduler、SnapshotPublisher、水位与过期保护 | WP-04 | 重复输入不执行两次；旧快照不回退；队列有界 |
| WP-06 | W03 | PredictionController、SnapshotBuffer、在线调试位置视图 | WP-05 的输入/快照契约 | 可开关预测和插值；校正原因及距离可观察 |
| WP-07 | W04 | 会话 epoch、恢复期限、替换连接、恢复快照与动作幂等 | WP-04、WP-05 | 有效恢复、过期恢复、旧连接、重复拾取案例通过 |
| WP-08 | W04 | 完整调试指标、房间回收、重复多局检查 | WP-06、WP-07 | 快照/输入/连接状态可对应；关房后能创建下一局 |
| WP-09 | W05 | AsyncTraceRecorder、TraceRecord 格式、来源清单与 Python 证据读取 | WP-08 | 磁盘/队列异常传播完整性状态；凭证不出现在产物 |
| WP-10 | W05 | ScenarioValidator、BotAgent、受限本地控制消息 | WP-08；纯校验可独立开始 | 场景字段错误明确；机器人使用真人协议完成一局 |
| WP-11 | W06 | ProcessSupervisor、ScenarioRunner、AssertionEngine | WP-09、WP-10 | 成功、业务失败、环境失败、总超时均有正确分类和清理 |
| WP-12 | W06 | UdpFaultProxy、MessageFaultFixture、6 个回归场景 | WP-11 的最小执行链 | 额外延迟/丢包与实际测量分开；不改变全局网络；故障可被断言发现 |
| WP-13 | W07 | TraceArchiveReader、ReplayController、Godot 回放展示 | WP-09 | 暂停、定位、跳转失败事件可用；原始快照与插值区分 |
| WP-14 | W07 | ReportBuilder、证据链接、修复前后对比和故障案例 | WP-11–WP-13 | 报告指向真实来源；缺数据输出明确错误；三类案例有证据 |
| WP-15 | W08 | IModelClient 适配器、ScenarioGenerator、修正上限与独立保存 | WP-10 的稳定场景契约 | 不执行模型生成代码；最多三次请求；超时不影响游戏 |
| WP-16 | W08 | GenerationEvaluator、20 条意图、独立运行证据和语义复核 | WP-15、WP-14 | 指标分母与失败样例齐全；没有证据的指标保持缺失 |
| WP-17 | W09 | 内存/资源检查、断连循环、四人稳定性及性能基准 | WP-08、WP-12、WP-14 | 三次基准和连续多局结果可复测，阻塞缺陷修复 |
| WP-18 | W09 | CI 全流程、无模型回归、候选包、首次体验检查 | WP-16、WP-17 | 干净环境启动成功，候选发布检查表齐备 |
| WP-19 | W10 | 目标平台最终打包验证、许可证、第三方清单、中英文快速开始 | WP-18 | 发布包和说明一致，未验证平台不标为支持 |
| WP-20 | W10 | 演示视频、架构及故障说明、实测材料、GitHub 发布准备 | WP-19 | 可供他人下载试跑和面试演示，全部 P0 有验收记录 |

- [x] WP-01：工程基础与共享契约完成（2026-10-18）；Debug/Release 构建及各 3 项 CTest 通过，详见[实施与验证记录](../../wp01-implementation.md)。W01、WP-02 仍未完成。
- [ ] G0：WP-01～WP-02 完成，2026-10-25 复核。
- [ ] G1：WP-03～WP-04 完成，2026-11-01 复核。
- [ ] G2：WP-05～WP-08 完成，2026-11-15 复核。
- [ ] G3：WP-09～WP-12 完成，2026-11-29 复核。
- [ ] G4：WP-13～WP-16 完成，2026-12-13 复核。
- [ ] G5：WP-17～WP-18 完成，2026-12-20 复核。
- [ ] G6：WP-19～WP-20 完成，2026-12-27 复核。

## 6. 计划代码与验证文件落点

以下为拟创建路径，不表示目前已有代码。完整类和函数签名以两份接口设计为准；本轮不重复粘贴接口，也不生成实现代码。

| 工作包 | 主要计划文件 | 对应验证文件 |
| --- | --- | --- |
| WP-01 | `CMakeLists.txt`、`CMakePresets.json`、`pyproject.toml`、`shared/foundation/result.hpp`、`shared/foundation/clock.hpp`、`shared/contracts/messages.hpp` | `tests/unit/foundation_test.cpp`、`tests/unit/contracts_test.cpp` |
| WP-02 | `shared/protocol/protocol_codec.cpp`、`adapters/transport/enet_transport.cpp`、`client/godot/native/netarena_bridge.cpp` | `tests/unit/protocol_codec_test.cpp`、`tests/integration/transport_smoke_test.cpp` |
| WP-03 | `shared/movement/movement_model.cpp`、`server/game/match_simulation.cpp`、`server/game/room.cpp` | `tests/unit/movement_model_test.cpp`、`tests/unit/match_simulation_test.cpp`、`tests/unit/room_test.cpp` |
| WP-04 | `server/session/session_service.cpp`、`server/app/server_app.cpp`、`apps/server/server_main.cpp`、`client/core/client_runtime.cpp`、`client/godot/scenes/arena.tscn` | `tests/integration/two_player_match_test.cpp` |
| WP-05～06 | `server/game/input_buffer.cpp`、`server/app/tick_scheduler.cpp`、`server/app/snapshot_publisher.cpp`、`client/core/prediction_controller.cpp`、`client/core/snapshot_buffer.cpp` | `tests/unit/input_buffer_test.cpp`、`tests/unit/prediction_controller_test.cpp`、`tests/unit/snapshot_buffer_test.cpp` |
| WP-07～08 | `server/session/session_service.cpp`、`client/godot/scripts/debug_presenter.gd` | `tests/unit/session_service_test.cpp`、`tests/integration/reconnect_test.cpp`、`tests/integration/repeated_matches_test.cpp` |
| WP-09～10 | `adapters/trace/async_trace_recorder.cpp`、`schemas/trace.schema.json`、`schemas/scenario.schema.json`、`tools/testing/scenario_contract/validator.py`、`apps/bot/bot_agent.cpp` | `tests/unit/trace_recorder_test.cpp`、`tests/python/test_scenario_validator.py`、`tests/integration/bot_match_test.cpp` |
| WP-11～12 | `tools/testing/runner.py`、`tools/testing/process_supervisor.py`、`tools/testing/assertions.py`、`tools/faults/udp_fault_proxy.py`、`tests/fixtures/message_fault_fixture.cpp` | `tests/python/test_runner_cleanup.py`、`tests/python/test_assertions.py`、`tests/python/test_udp_fault_proxy.py`、`scenarios/regression/normal_match.json` |
| WP-13～14 | `adapters/trace/trace_archive_reader.cpp`、`client/replay/replay_controller.cpp`、`client/godot/scripts/replay_presenter.gd`、`tools/reporting/report_builder.py` | `tests/unit/trace_archive_reader_test.cpp`、`tests/unit/replay_controller_test.cpp`、`tests/python/test_report_builder.py` |
| WP-15～16 | `tools/ai/model_client.py`、`tools/ai/scenario_generator.py`、`tools/ai/generation_evaluator.py`、`evaluations/generation_cases.json` | `tests/python/test_scenario_generator.py`、`tests/python/test_generation_evaluator.py` |
| WP-17～18 | `benchmarks/four_player_baseline.json`、`.github/workflows/ci.yml`、`docs/validation/release-checklist.md` | `docs/validation/stability-report.md`、`docs/validation/performance-report.md` |
| WP-19～20 | `README.md`、`README.zh-CN.md`、`LICENSE`、`THIRD_PARTY_NOTICES.md`、`docs/demo-walkthrough.md` | `docs/validation/platform-matrix.md`、`docs/validation/first-run-report.md` |

表中 `.cpp` 对应的公开头文件与接口设计一致，按模块同目录建立。实际代码开始前，应为当前工作包补齐确切构建目标、最小验证命令和必要的测试输入；不把尚不存在的命令标为已通过。

## 7. 需求与排期对应

| 需求 | 首次完整实现目标 | 主要工作包 | 验收依据 |
| --- | --- | --- | --- |
| FR-01 | W01 | WP-01、02 | 协议版本、非法消息、连接状态 |
| FR-02 | W04 | WP-04、07 | 身份和旧连接隔离；凭证不泄漏 |
| FR-03、FR-04 | W02 | WP-03、04 | AC-01、AC-02；结束后能再次建房 |
| FR-05 | W04 | WP-03、07 | AC-03、AC-04；跨重连幂等 |
| FR-06、FR-07、FR-08 | W03 | WP-05、06 | 输入有界、过期快照、预测/校正/插值 |
| FR-09、FR-10 | W04 | WP-07、08 | AC-05、AC-06；来源清晰的调试指标 |
| FR-11 | W05 | WP-10 | 机器人使用相同业务协议完成游戏 |
| FR-12、FR-13、FR-14 | W06 | WP-10～12 | AC-07、AC-08；超时、取消与清理 |
| FR-15 | W05 | WP-09 | 结构化记录、大小上限、完整性与脱敏 |
| FR-16、FR-17 | W07 | WP-13、14 | AC-09、AC-10；证据可定位 |
| FR-18、FR-19、FR-20 | W08 | WP-15、16 | AC-11、AC-12；20 条意图评测 |
| NFR-01、NFR-05、NFR-10 | W01 起持续执行，W09 复核 | WP-01、02、18 | 构建、自动化及依赖边界 |
| NFR-02、NFR-03、NFR-04 | W09 正式验收 | WP-17 | 多局稳定性、三次性能基准、资源有界 |
| NFR-06、NFR-08、NFR-09 | W05～W07 实现，W09 复核 | WP-09～14、18 | 证据关联、凭证隔离、故障构建隔离 |
| NFR-07 | W09 试跑，W10 复核 | WP-18、19 | 前置条件满足后约 10 分钟启动体验 |

AC-13 的协议/场景/回放非法输入验证分别跟随相关模块完成，W09 再统一回归。所有 P0 验收项必须在 G6 前具有记录；早期通过不能替代发布候选版本的最终复核。

## 8. 每周工作与复盘方式

建议采用固定节奏，而不是临近周末集中补所有验证：

1. 周初确认本周两个工作包、依赖状态和最小演示目标。
2. 每个开发时段结束前保留可构建状态，记录未解决的具体问题。
3. 功能完成时立即运行对应行为和异常路径检查，记录结果。
4. 周末使用缓冲时间修复问题、录制短演示、更新实际工时与下一周计划。

周记录建议保存在 `docs/progress/YYYY-Www.md`，内容包含：计划工作包、实际完成项、验证证据、实际投入、阻碍、范围变更、下周目标。实际记录生成后才能填写完成状态。

## 9. 风险缓冲与延期规则

### 9.1 首周必须消除的风险

- ENet、C++ 客户端核心和 Godot 原生扩展是否能在目标开发环境构建。
- 最小客户端是否能与独立 C++ 服务端收发消息，协议版本错误是否可解释。
- Linux 服务端与至少一个目标客户端平台的基本构建路径是否可行；另一客户端平台尽早安排试构建。

2026-10-25 前若上述问题仍未解决，更新 G1 及后续日期，不继续按原日期叠加功能。

### 9.2 调整方式

- 某周超出 4 小时缓冲，先估计剩余工作，再移动依赖它的工作包；不假设下一周凭空多出时间。
- 如果实际每周只能投入约 10–12 小时，应把总周期调整为约 18–22 周，而不是保留 10 周日期降低完成质量。
- 首版范围稳定后，新想法先进入 P1/P2 清单；不加入本轮承诺工作量。
- 若临近求职需要提前展示，可在 G1/G2 发布清楚标注范围的预览版；完整 v0.1 的 AI、测试和回放要求仍保留。
- 性能、稳定性和失败案例不能通过编造数字或隐藏失败完成验收。

### 9.3 本轮明确后置的内容

多房间扩展、分布式架构、数据库持久化、匹配系统、账号商城、复杂战斗、3D 美术、Unity/Unreal 适配、强化学习、通用 NPC 对话、跨平台确定性重演均不进入本次排期。

## 10. 求职与开源材料的积累日期

| 日期 | 计划保留的材料 | 用途 |
| --- | --- | --- |
| 2026-11-01 | 第一段双客户端可玩演示 | 尽早证明项目已形成运行闭环 |
| 2026-11-15 | 同步机制图、弱网现象说明、重连案例 | 解释游戏服务器与客户端技术 |
| 2026-11-29 | 场景配置、断言结果、失败产物 | 展示工程验证能力 |
| 2026-12-06 | 三类故障案例及修复前后证据 | 支撑深入技术问答与开源教程 |
| 2026-12-13 | AI 生成流程及固定评测集结果 | 说明 AI 的实际用途和局限 |
| 2026-12-20 | 可复测性能报告、资源与稳定性记录 | 支撑简历量化描述，指标均来自实测 |
| 2026-12-27 | 完整演示视频、中英文 README、验证过的发布包 | GitHub 展示、投递与面试演示 |

Star 仍是观察指标。首批更有价值的反馈是能否成功运行、是否理解演示、愿意复用哪些模块，以及遇到哪些安装或使用阻碍。

## 11. 当前状态与下一步

当前完成项是需求和设计文档及本排期；20 个实现工作包均为未开始。

WP-01 已于 2026-10-18 完成。下一步进入 WP-02：锁定传输依赖并验证 C++ 与 Godot 互通。先完成 G0，再沿依赖推进。进入实现时为本周工作包补充可直接执行的细化计划，不一次性展开所有未来模块的代码。

## 12. 修订记录

| 版本 | 日期 | 变更 |
| --- | --- | --- |
| v0.1 | 2026-10-17 | 建立 2026-10-19 至 2026-12-27 的 10 周排期、20 个工作包、里程碑与需求对应关系 |
