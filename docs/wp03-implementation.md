# WP-03 移动模型、比赛模拟与房间状态机

2026-11-23 开始实施，2026-11-24 完成（北京时间）。[实施计划](superpowers/plans/2026-11-23-wp03-game-domain.md)

## 交付范围

WP-03 将 FR-03～05 的核心规则实现为不依赖网络的 C++ 领域模块。客户端和服务器可共用移动模型，比赛和房间权威逻辑仅属于服务端。当前 Godot 面板仍展示 WP-02 的连接与心跳；多人游戏画面、会话路由在 WP-04 接入。

本轮没有实现 WP-05 的 InputBuffer/TickScheduler/SnapshotPublisher，或者 WP-07 的恢复凭证与过期处理。Room 直接接受调用方提供的单帧命令。原接口设计包含未来完整形态，本文说明已落地的边界。

## 依赖与数据流

```text
调用方（未来 ServerApp）
  └─ Room：成员、房主、准备、比赛阶段、传入的 epoch 校验
       └─ MatchSimulation：Tick、位置、金币、得分、动作结果、最终结果
            └─ MovementModel：固定步长、地图边界、静态碰撞和距离
                 └─ shared/contracts + shared/foundation
```

`netarena_movement` 可由客户端独立链接；`netarena_game` 依赖移动库。两者不依赖 ProtocolCodec、ENet、Godot 或 JSON，也不读取真实系统时间。网络序列化及认证位于未来应用层，领域调用参数中的身份必须由该层绑定。

## 规则约定

### 移动与碰撞

每次更新使用 `1/tick_hz` 秒；默认速度 4、半径 0.3、频率 30 Hz。方向向量长度超过 1 时限幅，NaN/Infinity 拒绝。没有输入时停止移动。

角色采用以半径为半边长的轴对齐方形占位，障碍按半径扩张；先沿 X 再沿 Y 做扫掠，可贴墙滑动，不能通过大步长跨越薄墙。这是保守的静态碰撞模型，角点不等同于精确圆形碰撞，不承诺跨架构位级确定性。客户端接入时应使用同一实现。地图内边界向内取整，障碍扩张边界向外取整。大坐标下单帧位移若小于可表示的坐标间隔，该轴这一帧保持原位。

地图、参数和初始状态在创建领域对象时校验。配置错误抛出 `invalid_argument`；运行时不可信命令返回 `Result` 错误。单帧批次先校验，再应用，避免一条错误命令留下半帧状态。

### 拾取、去重与结算

同一 Tick 先移动，再按 PlayerId、input_seq、ActionId 排序处理拾取。若同一批次有完全相同的排序键却指定不同金币，以较小的金币 ID 为先。业务规则检查玩家在线、距离、金币有效性和速率。相同玩家的 ActionId 命中首次结果，不能通过更换请求内容重新生效；换新 ActionId 也不能领取已经消失的金币。每人默认缓存 1,024 个动作结果，容量用完后拒绝新键。每人默认在最近一秒的逻辑 Tick 内最多尝试 10 次。重复请求沿用首次结果，不占用新的速率配额。单帧最多接收 64 条拾取命令。

比赛结束后得分、Tick 和最终结果冻结；查询可重复读取，结束事件只产生一次。退出玩家的最后得分保留在移除事件中；比赛结束时的成绩由当时仍在场的参与者决定。比赛世界和最终结果按玩家 ID 排列，出生点按传入的名单顺序分配。DomainEvent 记录拾取结果、移除和结束，持久化事件格式在 WP-09 定义。

### 房间生命周期

WAITING 允许加入和准备，人数为 2–4，所有成员在线且准备后由房主开始；第五人和非房主开始均拒绝。房主按加入顺序移交给最早的在线成员，没有在线等待成员时关闭。

Room 一生只对应一个 MatchId。运行阶段离线不新建实体；主动退出释放实体。FINISHED 展示最多 15 秒，或者所有成员提前退出时关闭。截止时间由调用方提供的单调时间计算。时间倒退时返回 InvalidArgument，期限超出时钟范围时使用该类型的最大时间点。结算期间可退出房间，最终世界和成绩保留结算时的名单，供界面展示。最后一名成员在运行阶段主动离开时，房间立即关闭，比赛在最后一个已执行的 Tick 中止。调用方通过下一次 `maintain(now)` 或空命令 `step(now)` 读取离场得分和中止事件，事件只交付一次。

## 与后续工作包的接口边界

- `step` 接收完整的一帧命令，不是网络收包回调；调度器决定何时调用。
- Room 校验当前成员和 epoch，但不签发身份、认证凭证或恢复令牌。
- 本轮没有输入确认水位、排队去重、快照序号或延迟输入策略；重复移动批次被拒绝，后续 InputBuffer 负责跨帧接纳。
- 领域状态和网络快照分开，WP-04/WP-05 负责映射和发布。
- 单个 Room 生命周期结束后，通过新 Room/MatchId 开始下一局。

## 主要接口

| 类和方法 | 参数 | 返回值及语义 |
| --- | --- | --- |
| `MovementModel(map, config)` | `MapDefinition`、`MovementConfig` | 持有地图和移动配置，构造时校验地图及出生点 |
| `normalize_intent(direction)` | `Vec2` | `Result<Vec2>`，有限且长度至多约 1 的方向 |
| `step(state, direction)` | 当前 `MoveState`、已限幅方向 | `MoveState`，新位置和按位移计算的速度。大坐标处向当前位置舍入，避免超出本帧位移 |
| `valid_position(position)`、`can_pickup(player, coin, radius)` | 点、角色及金币位置、距离阈值 | `bool`，位置合法性或欧氏距离判定 |
| `MatchSimulation(config, players, movement)` | `MatchConfig`、2–4 个唯一玩家、移动模型 | 创建权威比赛，默认 1,800 Tick，拾取半径 0.6 |
| `MatchSimulation::step(moves, pickups)` | `PlayerMove` 和 `AuthorizedPickup` 批次 | `Result<SimulationFrame>`，返回状态副本、这一帧的事件和首次结算结果 |
| `pickup(request)` | 授权的玩家与 `PickupCommand` | `Result<ActionOutcome>`。重复动作保留首次得分或错误，直接调用只返回结果，事件和结算走 `step` |
| `set_connected(player, connected)`、`remove_player(player)` | 玩家 ID、在线状态 | `Result<void>`。离线清零速度，比赛结束前移除实体并记录最后得分 |
| `MatchSimulation::view()` | 无 | `WorldView`，包含 Tick、位置、金币、剩余 Tick 和可选最终结果 |
| `Room(config)` | `RoomConfig`，含房间/比赛 ID、地图、移动配置、人数上限和结算保留时间 | 创建等待中的单场房间，开始时创建比赛实例 |
| `join(profile, epoch)`、`set_ready(player, ready)`、`leave(player)` | 成员信息和会话代际、玩家和准备状态、离开者 | `Result<RoomView>`，返回成员和阶段的快照 |
| `Room::set_connected(player, connected)` | 玩家 ID、可信调用方提供的在线状态 | `Result<RoomView>`，供本地状态切换使用 |
| `start(requester, now)`、`step(now, moves, pickups)` | 房主 ID、单调时间点、可选单帧命令批次 | `Result<FrameOutcome>`，含阶段、可选世界状态、局内事件与首次结算 |
| `maintain(now)` | 单调时间点 | `Result<FrameOutcome>`，进行到期检查而不推进游戏 Tick |
| `Room::view()`、`current_frame()` | 无 | `RoomView` 或 `FrameOutcome` 的副本，历史结算可从 `world.result` 查询 |

## 验证

- Debug 与 Release 各有 10 项 CTest 通过，覆盖 WP-01/02 回归和三项新增领域测试。
- Godot Debug 有 11 项 CTest 通过，包含缓存为空时启动真实 Godot 并与 C++ 服务端互通。
- 三个新测试程序均通过 AddressSanitizer 和 UndefinedBehaviorSanitizer 检查。
- 新增回归用例复现并验证了大坐标超速、碰撞半径不足和最后成员离场丢失事件。
