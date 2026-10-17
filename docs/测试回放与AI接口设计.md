# NetArena 测试、回放与 AI 接口设计

| 项目 | 内容 |
| --- | --- |
| 版本 / 日期 | v0.1 / 2026-10-17 |
| 状态 | 接口设计草案，未实现、未测量 |
| 架构依据 | [模块解耦与依赖设计](./模块解耦与依赖设计.md) |
| 共享类型 | [服务端与客户端接口设计](./服务端与客户端接口设计.md) |

本文覆盖 M11–M16，说明 Python 编排与 C++ 核心的边界。C++ 使用前文的 Result 和 DTO；Python 使用语义相同的 `Result[T]` 数据类：`value` 与 `error` 互斥，`ok` 明确标志状态。`Result[None]` 表示无数据的成功或错误。正常参数错误和外部服务失败均进入 Result，不靠解析异常字符串判断。

## 1. 跨语言与进程约定

| 边界 | 传递方式 | 约束 |
| --- | --- | --- |
| Python runner → C++ bot | 子进程 stdin/stdout 的版本化逐行 JSON | 仅白名单控制命令；stdout 专用于回复，诊断走 stderr；不传 shell 文本 |
| C++ 客户端 / bot → 服务器 | 与真人相同的游戏协议 | 测试身份不绕过业务规则 |
| runner → UDP 故障代理 | Python 对象方法或其受限本地控制管道 | 只控制本次创建的代理和端口 |
| server/client/bot → 记录器 | 进程内 ITraceSink，之后落到各自的产物目录 | 各进程写不同文件，不并发追加同一 NDJSON |
| runner / 报告 / 回放 → 记录 | 版本化 JSON/NDJSON | 共用 Schema 和黄金样例；语言实现可不同，语义必须一致 |
| AI → runner | 保存后的场景文件 | AI 没有 runner 引用；执行是单独用户操作 |

每次运行使用 `RunId` 和独立目录。`source_id` 标识 server、bot-1、client-1、proxy-1 等来源；`source_seq` 在单来源内严格递增。不同进程的单调时间原点不能直接相减，因果关联优先使用比赛 ID、Tick、请求 ID 和来源序号；展示排序不能宣称是跨进程真实全序。

## 2. M11 场景契约与校验

### 2.1 数据模型

场景契约放在 `scenario_contract` 子包，只含数据类、解析和纯校验，不导入模型 SDK、执行器或进程管理库。

| 类型 | 主要字段及含义 |
| --- | --- |
| `Scenario` | `schema_version:int`、`name:str`、`seed:int`、`match:MatchSetup`、`players:list[PlayerSpec]`、`actions:list[ActionSpec]`、`network:list[FaultSpec]`、`assertions:list[AssertionSpec]`、`timeout_s:int=120` |
| `MatchSetup` | `map_fixture:str`、`duration_ticks:int=1800`、`coin_positions:list[Point]`、`spawn_positions:list[Point]`；Point 为有限 x/y 数值；map_fixture 只能引用项目预置地图 |
| `PlayerSpec` | `alias:str`、`kind:bot`、`display_name:str`；首版自动化参与者固定为 bot；alias 唯一，真实 PlayerId 由服务器分配；真人图形客户端另作冒烟验收 |
| `ActionSpec` | `id:str`、`actor:str`、`trigger:Trigger`、`kind:ActionKind`、`args:ActionArgs`、`timeout_ms:int` |
| `Trigger` | `phase:connected/lobby/running`、`after_action_id:str?`、`observed_tick:int?`；依赖关系不得有环 |
| `ActionKind` | connect/create_room/join/ready/start/move/pickup/wait/disconnect/resume/retry_pickup/leave |
| `ActionArgs` | 按 kind 的封闭联合：move 用 direction 与 duration_ticks；pickup 用 coin_id；retry_pickup 引用此前动作 ID；不得包含代码 |
| `FaultSpec` | `id:str`、`actor:str`、`direction:c2s/s2c/both`、`delay_ms:int`、`loss_rate:float`、`seed:int`、`activate_after_action:str?`、`duration_ms:int?` |
| `AssertionSpec` | `id:str`、`kind:AssertionKind`、`subject:SubjectRef`、`expected:ExpectedValue`、`window:ObservationWindow`、`required_sources:list[str]` |
| `AssertionKind` | coin_claim_at_most_once / score_equals / entity_count_equals / resumed_snapshot_received / snapshot_seq_monotonic / state_reached |
| `SubjectRef` | 玩家 alias、金币 ID、比赛引用中与断言匹配的字段；解析成运行时真实 ID 后再判定 |
| `ExpectedValue` | 根据 kind 限定为数量、得分、连接/比赛状态或完整恢复标志；不允许任意表达式 |
| `ObservationWindow` | `start_trigger`、`end_trigger` 或 `deadline_ms`；明确断言的观察区间 |
| `ValidationIssue` | `path:str`、`code:str`、`message:str`，例如 `actions[2].actor` |
| `ValidatedScenario` | 校验后的不可变 Scenario、规范化摘要、Schema 版本及内容哈希；仅由校验器创建 |
| `CompiledScenario` | ValidatedScenario、动作依赖图、玩家绑定占位、预置资源引用；不包含任意可执行命令 |

首版默认上限：4 个参与者、256 个动作、64 个断言、64 个金币、最长 300 秒的显式总超时、延迟 0–1000 ms、丢包率 0–1。正常默认超时仍为 120 秒。测试超出局内玩家数、资源范围或网络参数范围时直接拒绝。

`observed_tick` 表示机器人观察到该 Tick 后发出动作，不保证服务器在该 Tick 接受。执行记录必须区分计划、发送和实际接受 Tick。AC-03 的严格同 Tick 排序由领域单元测试和受控输入夹具验证；普通联网测试若未满足同 Tick 前提，应报告前提未满足，不能伪称已覆盖该条件。

### 2.2 `ScenarioValidator`

初始化：`ScenarioValidator(catalog: CapabilityCatalog, limits: ScenarioLimits)`。catalog 包含合法动作、断言、地图及测试夹具名称；是静态数据，不包含函数执行回调。

| 函数签名 | 参数与行为 | 返回值 |
| --- | --- | --- |
| `parse(text: str) -> Result[Scenario]` | 检查字节上限后解析；不读取模型指定路径 | 结构对象；格式错误附字段路径 |
| `validate(scenario: Scenario) -> list[ValidationIssue]` | Schema、数值、别名引用、动作顺序、依赖环和断言语义检查 | 空列表表示有效；不启动任何进程 |
| `compile(scenario: Scenario) -> Result[CompiledScenario]` | 完整校验并规范化，生成不可变执行计划 | 合法计划；不支持能力返回 Unsupported |
| `summarize(scenario: ValidatedScenario) -> ScenarioSummary` | 提取人数、步骤、网络、断言和最长耗时 | 可供用户确认的结构化摘要 |

`ScenarioSummary` 含 `name,players,steps,network_conditions,assertions,total_timeout_s,warnings`。所有执行入口重复校验实际文件内容，不因文件之前经过 AI 生成或用户编辑就跳过校验。

## 3. M11 机器人与执行编排

### 3.1 `BotAgent`（C++）

构造：`BotAgent(ClientRuntime& client, BotConfig config)`。`BotConfig{alias:string,control_message_limit_bytes=16384,control_queue_capacity=64}`。借用客户端核心，拥有动作状态表和当前移动方向，不含服务器状态修改接口。

| 函数签名 | 参数与行为 | 返回值 |
| --- | --- | --- |
| `Result<BotCommandReceipt> submit(const BotCommand& command)` | 校验白名单、动作参数及当前阶段，排入控制队列 | `command_id` 与 Queued；满队列为 QueueFull |
| `Result<vector<BotEvent>> update(MonoTime now)` | 调用 ClientRuntime.update，推进等待/移动/恢复等动作 | 动作进展和客户端事件；不保证 submit 后立即完成 |
| `BotState state() const` | 无 | 当前阶段、游戏 Tick、公开身份和动作状态的副本 |
| `Result<void> stop(Duration deadline)` | 取消动作并关闭本进程连接 | 幂等成功或 Timeout |

`BotCommand{command_id,kind,args,deadline}`；kind/args 来自场景白名单。`BotCommandReceipt{command_id,status}`。`BotEvent{command_id?,kind,client_event?,observed_tick?,error?}`。`BotState` 不包含恢复凭证。

本地控制消息只用于组织客户端行为。异常网络和畸形协议案例使用专门测试夹具，不能给普通机器人增加“直接改分数”的控制命令。

### 3.2 `ProcessSupervisor`（Python，runner 内部辅助类）

构造参数：允许启动的可执行文件清单、运行目录、终止宽限时间。清单来自用户安装配置，不能由 Scenario 或模型输出修改。

| 函数签名 | 参数与行为 | 返回值 |
| --- | --- | --- |
| `start(spec: ProcessSpec) -> Result[ProcessHandle]` | 使用参数数组启动允许的程序，不使用 shell 字符串 | 受管理的进程句柄；启动失败为 IoError |
| `wait_ready(handle: ProcessHandle, timeout_s: float) -> Result[ReadyInfo]` | 等待有界握手，不把进程存在等同于准备完成 | 实际端口、版本和 source_id |
| `stop_all(grace_s: float) -> CleanupResult` | 先正常终止，超时后只终止本次创建且仍匹配的进程 | 成功/失败的资源列表；可重复调用 |

`ProcessSpec{executable_key,args:list[str],cwd:Path,env_overrides:dict}`；禁止可执行路径来自场景。`ProcessHandle` 保留子进程对象和运行标识，不能只凭可复用的裸 PID 杀进程。

### 3.3 `ScenarioRunner`

构造：`ScenarioRunner(supervisor: ProcessSupervisor, bot_factory: BotFactory, fault_factory: FaultFactory, evidence: EvidenceReader, assertions: AssertionEngine)`。factory 仅创建本节受控适配器，不接受模型给出的类名或模块路径。

主要成员：运行状态、参与者 alias 到真实身份/进程的绑定、动作依赖图、有界控制队列、取消标志和资源清单。

| 函数签名 | 参数与行为 | 返回值 |
| --- | --- | --- |
| `prepare(plan: CompiledScenario, workspace: RunWorkspace) -> Result[PreparedRun]` | 分配运行 ID、独立目录、受限配置及可用端口；保存实际配置 | 已准备上下文；不执行游戏动作 |
| `async run(prepared: PreparedRun, cancel: CancellationToken) -> RunResult` | 启动/就绪/建房/动作/断言/收尾；所有异常进入结果，finally 清理 | 完整结果，不因失败遗漏清理 |
| `async rerun(artifact_dir: Path, workspace: RunWorkspace, cancel: CancellationToken) -> RunResult` | 从保存场景重新校验，建立新运行目录，记录与原环境差异 | 新 RunResult；不调用 AI |

辅助类型：

- `RunWorkspace{root:Path,allowed_executables,artifact_size_limit,platform_capabilities}` 为可信启动配置。
- `PreparedRun{run_id,plan,paths,process_specs,source_registry}` 不暴露到模型上下文。
- `RunResult{run_id,status,reason,assertions,action_results,artifact_dir,source_completeness,cleanup,metrics}`。
- `status` 固定 PASS/FAIL/ERROR/TIMEOUT。用户取消表现为 ERROR，reason=Cancelled；整体超时为 TIMEOUT。
- 多种问题同时存在时按 TIMEOUT、ERROR、FAIL、PASS 的优先级给出主状态，同时保存全部诊断。环境/证据错误不能被一个通过的业务断言掩盖。

运行清理不删除用户项目文件，也不关闭由用户自行启动的服务器。绑定端口失败应重新分配受控端口或返回错误，不终止占用端口的其他程序。

## 4. M11 断言引擎

### `AssertionEngine`

构造：`AssertionEngine(specs: list[AssertionSpec], bindings: RuntimeBindings)`。bindings 为 alias 到 PlayerId/MatchId/source_id 的只读映射。

主要成员：各断言的观察窗口、所需来源水位、累计事件、当前结果和失败证据引用。

| 函数签名 | 参数与行为 | 返回值 |
| --- | --- | --- |
| `observe(record: TraceRecord) -> list[AssertionUpdate]` | 按来源验证序号与字段，更新有限状态；不执行字符串表达式 | Pending / Failed / Error 等阶段变化 |
| `advance(now: float, observed_tick: Optional[int]) -> list[AssertionUpdate]` | 检查真实时间或 Tick 截止条件；now 是 runner 的单调秒数 | 期限到达后的变化 |
| `finish(completeness: SourceCompleteness) -> list[AssertionResult]` | 关闭观察窗口，核对来源和证据完整性 | PASS/FAIL/ERROR，最终结果不含 Pending |

`AssertionResult{id,status,expected,actual,evidence_refs,message}`；`EvidenceRef{source_id,source_seq,match_id?,tick?,artifact_path,line?}` 只能指向运行目录内记录。

判定规则：

- “最多领取一次”属于区间不变式，观察到一次成功不能提前宣告整段区间通过；第二次成功立即失败。
- “指定时间内得分达到 X”在完整观察窗口内未达到时为 FAIL；无法取得必要来源时为 ERROR。
- 客户端快照断言只能使用客户端报告的已应用序号，不能拿服务器已发送序号代替。
- 业务条件到期未满足与整个测试进程超时分开：前者是断言失败，后者是运行 TIMEOUT。
- 比较快照只比较明确相同比赛、来源和语义的字段；客户端显示插值位置不要求逐帧等于权威位置。

## 5. M12 故障实验

### 5.1 `UdpFaultProxy`（Python）

该代理转发实际 UDP 数据报，位于传输库可靠性处理之外。它不解析或修改 ENet 内部协议，不宣称应用消息丢弃等同于网络丢包。

构造：`UdpFaultProxy(config: ProxyConfig)`。

`ProxyConfig{listen_host:str='127.0.0.1',listen_port:int,upstream:Endpoint,max_queued_datagrams:int=4096,max_queued_bytes:int=8388608,seed:int}`。每个测试客户端使用独立监听端口和上游 Socket，避免不同客户端在服务器端被错误合并为同一地址。

| 函数签名 | 参数与行为 | 返回值 |
| --- | --- | --- |
| `async start() -> Result[ProxyEndpoint]` | 只绑定本次运行端口，创建收发任务 | 客户端应连接的地址；端口占用为 IoError |
| `set_profile(profile: NetworkProfile) -> Result[None]` | 设置方向、延迟、丢包率和生效区间，校验限制 | 成功或 InvalidArgument |
| `stats() -> ProxyStats` | 返回注入测量和队列状态 | 收到/转发/策略丢弃/队列溢出分别计数，不能混为丢包率 |
| `drain_events(limit: int) -> list[FaultEvent]` | 非阻塞读取有界故障事件 | 事件副本，空列表表示暂无事件 |
| `async stop(grace_s: float) -> CleanupResult` | 停止任务，关闭自身 Socket，处理未发数据报 | 清理结果及未投递数 |

`NetworkProfile{direction,delay_ms,loss_rate,activate_at_monotonic?,duration_ms?}`；`ProxyEndpoint{host,port,source_id}`；`FaultEvent{direction,datagram_seq,decision,scheduled_delay_ms,actual_delay_ms?,bytes}`。延迟是单向额外延迟，双方各 75 ms 不应标成额外 RTT 75 ms。

代理队列溢出属于测试基础设施错误，必须与配置中的随机丢包区分并使相关测试 ERROR；不能偷偷丢弃后声称成功模拟目标条件。

### 5.2 `MessageFaultFixture`（测试专用）

用途是在自有客户端消息应用边界或测试组合入口复现旧快照、重复请求等业务故障。公开类只用于测试目标，不进入普通发行构建。

| 函数签名（C++） | 参数与行为 | 返回值 |
| --- | --- | --- |
| `Result<vector<Envelope>> transform(const Envelope& message, const MessageFaultRule& rule)` | 按白名单重复、暂存或反序投递指定消息；不接受任意脚本 | 有限消息列表；不支持的组合为 Unsupported |
| `void reset()` | 清除暂存消息与计数 | 无返回值 |

`MessageFaultRule{kind:Duplicate/DelayOldSnapshot/ReverseSnapshots,match_filter?,max_held_messages=16}`。它不修改游戏领域私有状态。

AC-09 的“重复加分”“重连残留实体”等故意错误使用单独的测试构建实现或故障版本，并通过 `fixture_id` 记录。不能让普通服务器加载任意故障函数，也不能以仅修改报告数据的方式冒充真实业务失败。领域测试可直接调用 Room/MatchSimulation 的公开 API，不属于生产模块依赖。

## 6. M13 记录与证据读取

### 6.1 `AsyncTraceRecorder : ITraceSink`（C++）

构造：`AsyncTraceRecorder(RecorderConfig config)`。`RecorderConfig{source_dir:Path,run_id,source_id,queue_capacity=4096,max_file_bytes=67108864,flush_interval=250ms}`。拥有一个有界队列、一个写线程和独立于队列的完整性状态。

| 函数签名 | 参数与行为 | 返回值 |
| --- | --- | --- |
| `Result<void> start(const SourceManifest& manifest)` | 新建本来源目录；先写 `complete=false`；拒绝覆盖已有来源 | 成功或 AlreadyExists / IoError |
| `EnqueueStatus try_append(TraceRecord record)` | 非阻塞入队，检查记录已脱敏；保持来源序号 | Accepted / Full / Closed；Full 设置不可丢失的不完整标志 |
| `RecorderStatus status() const` | 读取原子统计，不访问游戏对象 | 写入数、排队数、拒绝数、完整性、最后错误 |
| `Result<RecorderSummary> close(Duration deadline)` | 停止接收、限时刷新并写完成清单；正常完成才设 complete=true | 总计与完整性；失败保留不完整清单 |

`SourceManifest{schema_version,run_id,source_id,build_id,protocol_version,config_digest,clock_description}`；`RecorderSummary{records_written,records_dropped,bytes_written,complete,errors}`。

同一运行的每个来源写 `sources/<source_id>/events.ndjson` 和 `snapshots.ndjson`。runner 在来源完成后归集为需求文档建议的根目录产物；归集保留原始来源标识和文件，不伪造统一时间顺序。所有关键来源完整后，根 manifest 才能标为 complete。

文件大小超限、磁盘错误或进程崩溃时，不能保证成功追加错误日志，因此完整性状态还应通过本地控制状态返回；缺少正常关闭标记默认视为不完整。

### 6.2 `TraceArchiveReader`（C++，回放使用）

构造：`TraceArchiveReader(ReaderLimits limits)`，limits 包括支持的 Schema、单行最大字节、总文件上限和索引上限。首版只读取已关闭产物，避免回放边写边读的复杂度。

| 函数签名 | 参数与行为 | 返回值 |
| --- | --- | --- |
| `Result<ArchiveInfo> open(const filesystem::path& run_dir)` | 验证路径、清单、版本和记录格式，建立有界索引 | 来源、比赛、Tick 范围、完整性；损坏返回 CorruptArtifact |
| `Result<SnapshotWindow> snapshots(MatchId match, Tick tick, string source_id) const` | 查询该来源目标 Tick 前后的原始快照 | `before?`、`after?`；无样本为空，不合成证据 |
| `Result<QueryPage<TraceRecord>> events(MatchId match, Tick begin, Tick end, size_t limit) const` | 有界查询，返回原始记录和后续游标 | 第一页；区间非法为 InvalidArgument |
| `Result<QueryPage<TraceRecord>> events_next(string cursor, size_t limit) const` | 继续相同查询；游标绑定当前归档，不能作为文件路径使用 | 后续页；无下一页时 next_cursor 为空，非法游标为 InvalidArgument |
| `void close()` | 释放文件和索引 | 幂等无返回值 |

`QueryPage<T>{items:vector<T>,next_cursor:optional<string>}`。查询达到 limit 时必须通过 next_cursor 表达尚有数据，不能返回一个被静默截断且看似完整的集合。

### 6.3 `EvidenceReader`（Python，测试和报告使用）

与 C++ reader 共用记录 Schema 和黄金样例。它只解释记录，不计算游戏规则。

| 函数签名 | 参数与行为 | 返回值 |
| --- | --- | --- |
| `attach(run_dir: Path) -> Result[EvidenceCursor]` | 校验当前运行各来源目录，登记来源 | 游标对象；不同来源独立偏移 |
| `poll(cursor: EvidenceCursor, limit: int) -> Result[list[TraceRecord]]` | 只读取完整换行记录；文件尾半行等待下次读取 | 新记录；完整行损坏为 CorruptArtifact |
| `completeness() -> SourceCompleteness` | 汇总关闭标记、来源缺失和序号缺口 | 来源到状态的映射，含原因 |
| `finalize(run_dir: Path) -> Result[EvidenceIndex]` | 核对所有来源，建立合并索引并生成根清单 | 能读取部分数据时仍返回带 complete=false 和缺失原因的索引；目录完全不可读才返回 IoError |

`EvidenceCursor{source_offsets,source_watermarks}`；`SourceCompleteness` 区分 complete/incomplete/missing；`EvidenceIndex` 包含受控相对路径、来源、定位信息、complete 和 warnings。归集失败时 runner 仍保留 RunResult，并尝试生成不含证据索引的最小失败报告。

## 7. M14 状态回放

### `ReplayController`（C++）

构造：`ReplayController(TraceArchiveReader& reader)`。只借用归档读取器，持有当前比赛、Tick、速度、播放状态和所选来源；不持有 ITransport 或 ClientRuntime。

| 函数签名 | 参数与行为 | 返回值 |
| --- | --- | --- |
| `Result<ReplayOverview> load(const filesystem::path& run_dir)` | 打开产物并选择可用比赛；有磁盘 I/O | 来源、Tick 范围、警告、失败证据入口 |
| `Result<ReplayFrame> seek(MatchId match, double tick)` | 定位原始快照并为画面插值 | 画面状态、原始端点、插值标志、附近事件 |
| `Result<void> set_speed(double multiplier)` | 限定 0.25–4.0 倍 | 超范围 InvalidArgument |
| `void set_playing(bool playing)` | 设置播放/暂停 | 无返回值 |
| `Result<ReplayFrame> advance(Duration elapsed)` | 按记录的 Tick 频率推进，结束时自动暂停 | 当前帧；无加载状态为 InvalidState |
| `Result<ReplayFrame> jump_to(const EvidenceRef& reference)` | 校验来源和记录引用后跳转 | 原始证据关联的帧；缺失为 NotFound |

`ReplayFrame{match_id,display_tick,server_state?,client_state?,interpolation_endpoints,is_interpolated,events,warnings}`。缺失客户端数据保持空，不能用服务器副本伪造客户端历史。

Godot 的 `ReplayPresenter` 通过回放桥接调用上述接口。load/index 构建在专用加载任务执行，完成后将结果交回主线程；加载期间不能并发调用 seek，reader 和 controller 的所有权通过明确的加载/就绪状态交接。普通在线桥接仍保持非阻塞。

回放不调用 MatchSimulation.step，不作确定性重演承诺。未来加入输入重演应新增独立控制器，而不是改变 seek 的语义。

## 8. M15 报告与结果对比

### `ReportBuilder`（Python）

构造：`ReportBuilder(format_version: int, evidence_reader: EvidenceReader)`。持有格式配置和只读证据接口，不拥有测试进程。

| 函数签名 | 参数与行为 | 返回值 |
| --- | --- | --- |
| `build(result: RunResult, index: Optional[EvidenceIndex]) -> Result[RunReport]` | 将已判定断言和证据引用合并；验证路径和引用存在 | 缺索引时输出最小失败报告：已有 TIMEOUT 保持，其余主状态至少为 ERROR；不编造回放入口 |
| `write(report: RunReport, output_dir: Path) -> Result[ReportPaths]` | 原子写入 JSON 和可阅读报告，不覆盖其他运行 | summary.json 与报告路径；IoError |
| `compare(before: RunReport, after: RunReport) -> ComparisonReport` | 对照版本、场景哈希、配置、平台及测量窗口 | 可比较指标、不可比较原因和断言变化 |

`RunReport{run_id,status,artifact_dir,scenario_digest,environment,assertions,metrics,failures,evidence_links,warnings}`；`ReportPaths{summary_json,readable_report}`；`ComparisonReport{compatible_dimensions,differences,assertion_changes,metric_deltas,warnings}`。

报告不能将缺失指标填零，也不能把不同机器的耗时差自动描述为优化收益。ReportBuilder 不重新运行断言，业务结果的来源是 AssertionEngine。

## 9. M16 模型接口与场景生成

### 9.1 `IModelClient` / `HttpModelClient`（Python）

`IModelClient` 使用结构化请求和结果，不暴露供应商 SDK 对象。`HttpModelClient` 是首版拟实现的具体适配器，只保证经过验证的一种服务配置。

构造参数 `ModelConfig{endpoint:str,model:str,credential_ref:str,request_timeout_s:float=20,max_output_chars:int=32768}`。endpoint 和 credential_ref 来自可信用户配置，不能由模型输出改写。适配器负责读取凭证，不将其写入场景。

| 函数签名 | 参数与行为 | 返回值 |
| --- | --- | --- |
| `async generate(request: ModelRequest, budget: CallBudget) -> Result[ModelResponse]` | 只生成文本，不提供工具执行权限；请求受剩余总预算约束 | 响应或 Timeout / Unauthorized / RateLimited / IoError |

`ModelRequest{instruction:str,user_intent:str,schema:dict,validation_feedback:list[ValidationIssue]}`；`CallBudget{remaining_s,max_output_chars}`；`ModelResponse{text,model,provider_request_id?,usage:TokenUsage?,latency_ms}`。TokenUsage 缺失时保留空值，不估算为供应商实测。

### 9.2 `ScenarioGenerator`

构造：`ScenarioGenerator(model: IModelClient, validator: ScenarioValidator, policy: GenerationPolicy)`。

`GenerationPolicy{max_repairs=2,total_timeout_s=45,max_intent_chars=4000}`。首次生成加最多两次修正，总计最多三次请求；每次超时不能突破剩余总预算。

| 函数签名 | 参数与行为 | 返回值 |
| --- | --- | --- |
| `async generate(intent: str, context: GenerationContext) -> Result[GenerationResult]` | 根据白名单能力生成，解析/校验，必要时带字段错误请求修正 | 有效草稿或明确拒绝/错误；绝不启动测试 |
| `save(result: GenerationResult, destination: Path) -> Result[Path]` | 只保存有效场景及脱敏生成元数据；路径来自调用者 | 实际场景路径；拒绝失败候选作为可执行场景保存 |

`GenerationContext{capability_catalog,scenario_schema_version,default_match_config}` 不包含恢复凭证或任意服务器连接权限。

`GenerationResult{status:Generated/Unsupported,scenario:ValidatedScenario?,summary:ScenarioSummary?,attempts,validation_history,usage?,model,elapsed_ms,limitations}`。模型正常表达“不支持”属于 Unsupported 结果；无效 JSON 经修正仍失败返回 Error，不能伪装为有效空场景。

校验器能证明结构、引用和允许操作合法，不能保证自然语言意图被完全理解。返回摘要供用户确认；若模型替换了目标或缺少关键动作，不可宣称成功满足用户需求。

### 9.3 `GenerationEvaluator`

构造：`GenerationEvaluator(schema_version: int)`；不依赖 ScenarioRunner，也不自动执行模型生成的场景。

| 函数签名 | 参数与行为 | 返回值 |
| --- | --- | --- |
| `evaluate(cases: list[EvaluationCase], generated: list[GenerationAttempt], executions: list[ExecutionEvidence], reviews: list[SemanticReview]) -> EvaluationReport` | 汇总生成、独立运行证据及语义复核 | 指标、分子/分母、失败样例、未复核项 |
| `write(report: EvaluationReport, output_dir: Path) -> Result[Path]` | 写入版本化评测报告 | 报告路径；IoError |

`EvaluationCase{id,intent,expected_support,required_actions,required_assertions}`；`GenerationAttempt{case_id,attempt_index,parse_ok,validation_ok,result?,error?}`。

`ExecutionEvidence{case_id,scenario_digest,run_id,status,artifact_ref}` 来自用户独立启动的测试；没有该证据就不统计真实可执行率。`SemanticReview{case_id,status:Match/Mismatch/Unreviewed,notes}` 来自明确的复核记录；未复核不能计入语义成功。

`EvaluationReport` 至少包含：首次结构通过率、修正后结构通过率、有运行证据的可执行率、语义复核符合率、不支持意图的正确拒绝率、各指标分母及缺失证据数量。不同指标不得共享一个含糊的“成功率”。

## 10. 代表性端到端接口调用

下面是调用顺序说明，不是可直接运行的实现代码。

```text
生成阶段：
CLI.generate(intent)
  → ScenarioGenerator.generate(intent, context)
  → IModelClient.generate(request, budget)
  → ScenarioValidator.parse / compile / summarize
  → 展示摘要并保存 scenario.json

用户显式运行阶段：
CLI.run(scenario_path)
  → ScenarioValidator.parse / compile
  → ScenarioRunner.prepare / run
  → ProcessSupervisor.start / wait_ready
  → UdpFaultProxy.start / set_profile
  → BotAgent.submit / update → ClientRuntime → 实际游戏协议
  → EvidenceReader.poll → AssertionEngine.observe / finish
  → 清理进程、关闭来源记录、EvidenceReader.finalize
  → ReportBuilder.build / write

离线查看阶段：
ReplayController.load(report.artifact_dir)
  → TraceArchiveReader.open
  → ReplayController.jump_to(failure.evidence_ref)
  → ReplayPresenter.render(frame)
```

## 11. 隔离验证与替换点

| 模块 | 测试替身 / 固定数据 | 必须保持的行为 |
| --- | --- | --- |
| 场景校验 | 手写合法、非法、环形依赖、未知动作场景 | 无进程副作用，错误定位到字段 |
| 执行器 | FakeProcessSupervisor、固定 bot 回复 | 启动失败/取消/超时仍清理本次资源 |
| 断言 | 固定 TraceRecord 序列 | 来源缺失不能 PASS，不变式不能过早 PASS |
| 故障代理 | 本地 UDP 回显与计数 | 区分注入丢包、队列溢出及实际延迟 |
| 记录读取 | 版本化黄金样例、截断与损坏文件 | 不完整状态传播，跨语言读取一致 |
| 回放 | 不同来源稀疏快照 | 显示插值与原始证据分开 |
| 报告 | 已知 RunResult 与 EvidenceIndex | 保留失败分类，不能把空值改成零 |
| AI | FakeModelClient 返回有效、非法、超时、拒绝响应 | 修正次数和总时长有上限，不自动执行 |

只替换接口对应实现，不修改上层业务：传输替换在 ITransport，记录替换在 ITraceSink，模型替换在 IModelClient。首版不为了“可扩展”增加未验证的多个网络后端、引擎后端或模型供应商。
