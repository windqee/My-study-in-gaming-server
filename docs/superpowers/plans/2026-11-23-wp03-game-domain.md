# WP-03 移动模型与游戏领域实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development or executing-plans. 依据已确认的需求/接口/路线图实施，先失败测试、再实现，规格与质量审查后集成。

**Goal:** 在没有网络和引擎的情况下验证移动边界、稳定争抢、房间准备/开始及一次性结算，并提交到用户现有 GitHub 仓库。

**Architecture:** MovementModel 只依赖共享 DTO，MatchSimulation 拥有比赛规则与状态，Room 拥有成员及单场比赛。领域层不链接 JSON、ENet、Godot、文件或真实时钟。时间由调用方传入，网络输入排队及恢复服务按原计划留给 WP-05/WP-07。

**Tech Stack:** C++20、现有 Result/强类型 ID/DTO、CMake/CTest；不增加第三方依赖。

## 已确认范围与本轮具体决策

- 使用既有 FR-03～05 与接口文档第 6、7 节；WP-03 完成纯逻辑部分，WP-04 才接通多人游戏画面。
- 移动固定 `1/tick_hz`，方向按长度限幅。角色碰撞采用保守的半径扩张轴对齐矩形，先 X 后 Y 扫掠，避免单帧跨过薄墙；与圆形精确碰撞的差异必须写入实施记录。
- 配置是可信启动输入，非法配置抛出 `invalid_argument`。运行时命令使用 Result，不允许非有限输入或旧比赛改变状态。批次必须先整体验证再改变状态，重复玩家移动在一个 Tick 中拒绝。
- 每 Tick 先移动，再按 PlayerId/input_seq/ActionId 顺序处理拾取，最后判断金币耗尽或时间结束。没有输入就是中性方向，不保留上一帧移动。
- ActionId 缓存保存首次业务结果，重复返回该结果且不重复发成功事件；新的 ActionId 不能重新领取失效金币。每玩家缓存和每秒请求数有界，窗口由逻辑 Tick 定义。
- Room 只允许 WAITING 加入和准备；2–4 人全部在线且准备才可由房主开始；首位加入为房主，离开后移交最早在线成员。一个 Room 只运行一个 MatchId。结算后 15 秒或全部离开时关闭。
- 本轮 Room 的 step 接收一帧已由应用层绑定身份的 moves/pickups，验证成员、match、epoch；每帧最多一条移动/玩家，拾取批次有上限。没有实现 InputBuffer，也不声称实现跨重连水位/恢复期限。与未来完整接口差异明确记录。
- 世界状态查询返回副本；结束之后重复 step 不产生新得分、Tick 或结算事件。最终结果只发布一次，但可以持续查询。
- 用局部封闭 DomainEvent 表达拾取结果、移除和结束等规则事件；不提前定义 WP-09 的持久化 GameEvent/TraceRecord。

## Task 1：共享移动模型

文件：`shared/movement/movement_model.hpp/.cpp`，`tests/unit/movement_model_test.cpp`。

- [x] 编写最小失败用例：地图 [0,10]，速度 3、30 Hz，(1,1) 沿 X 一帧到 (1.1,1)，原状态不变；证明测试先失败。
- [x] 实现 `MovementModel(MapDefinition, MovementConfig)`、`normalize_intent(Vec2)->Result<Vec2>`、`step(MoveState,Vec2)->MoveState`、`valid_position(Vec2)->bool`、`can_pickup(Vec2,Vec2,float)->bool` 和 const 地图/配置访问器。
- [x] 用具体行为测试验证对角限速、NaN/Inf、边界半径、墙体、角点滑动、跨薄墙、非法地图/出生点/配置、拾取临界距离；计算使用 double 中间值避免有限大 float 平方溢出。
- [x] 独立编译与测试通过，记录红绿结果。

## Task 2：权威比赛模拟

文件：`server/game/domain_types.hpp`、`match_simulation.hpp/.cpp`，`tests/unit/match_simulation_test.cpp`。

- [x] 先写两玩家反向提交同一金币，较小 PlayerId 获得唯一 1 分的失败用例；交换输入排列必须结果相同。
- [x] 定义 MatchConfig、PlayerMove、AuthorizedPickup、WorldView、ActionOutcome、DomainEvent 和 SimulationFrame；默认时长 1800 Tick、拾取半径 0.6、动作缓存 1024、每秒拾取上限 10。
- [x] 实现 step、pickup、set_connected、remove_player、view；运行时校验失败不产生部分帧效果。非法配置在构造期拒绝。
- [x] 覆盖先移动后拾取、离线/距离/金币不存在、重复 ActionId（含改内容）、拒绝结果重试、缓存满、速率窗口、重复移动、旧局/非法方向、时间到、金币耗尽、平局、结束幂等、离开后得分处理。
- [x] 查询副本修改不影响内部状态；记录事件只在新效果时生成，查询不重复发事件；确定移除事件与最终结果的成员口径并写入文档。

## Task 3：房间基本状态机

文件：`server/game/room.hpp/.cpp`，`tests/unit/room_test.cpp`。

- [x] 先写单人开始失败、第五人加入失败、非房主开始失败和两人全部准备后开始成功的测试。
- [x] 实现 RoomConfig、FrameOutcome，join、set_ready、start、step、leave、view、current_frame、maintain，以及最小在线状态更新；明确不是恢复服务。
- [x] step 作为 WP-03 直接帧入口，拒绝旧比赛、旧 epoch、未知/离线成员、重复移动及超限拾取；不得提前推进 Tick 或应用其他有效命令。
- [x] 覆盖重复加入/准备、房主移交、无在线等待成员关闭、重复开始、结算只发生一次、15 秒期限边界、全部成员离开、关闭后请求拒绝和新 Room 可重新开始。
- [x] 处理期限加法与计数溢出；不得在等待态创建仿真或在查询时更新游戏。

## Task 4：构建、审查与 GitHub 交付

文件：`CMakeLists.txt`、`README.md`、`docs/wp03-implementation.md`、路线图。

- [x] 加入独立 `netarena_movement`、`netarena_game` 静态库和 3 个单元测试目标；公开头检查覆盖新接口，引擎无关依赖不发生反向链接。
- [x] 规格审查、质量审查，修复发现的问题并验证相应回归。
- [x] 运行 `cmake --preset debug` / `release`、构建、`ctest --preset debug` / `release`；运行已有 Godot 配置，确认 WP-01/02 回归通过。对新领域测试运行地址/未定义行为检查。
- [x] 文档写明实际类、参数、函数、返回值、依赖、规则边界及验证结果，实际完成日期使用 2026-11-24。保留原排期作为基线，明确旧日期已过，G1 尚需 WP-04。
- 发布方式为在现有 GitHub 仓库的 main 上快进更新。GitHub 提交记录用于核对发布状态。
