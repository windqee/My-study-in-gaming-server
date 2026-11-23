# NetArena

面向游戏服务器求职的 C++ 联机游戏实验场：以权威服务器、客户端同步和可复现测试证据为主线，后续接入 AI 场景生成。完整目标见[开发路线图](docs/superpowers/plans/2026-10-17-netarena-roadmap.md)。

**当前完成 WP-01 至 WP-03。** Godot 客户端可以连接独立 C++ 服务端并收发心跳。C++ 游戏领域已支持固定步长移动、障碍碰撞、争抢金币、房间准备和结算，可独立运行测试。WP-04 将房间和比赛规则接到客户端，形成首个联机可玩版本。验证平台为 macOS arm64。

## 快速开始：C++ 服务端与测试

需要 C++20 编译器、CMake 3.25+、Make、Python 3.12+。首次准备依赖需要联网；后续 CMake 配置和构建不访问网络。

在仓库根目录执行：

```sh
python3 tools/bootstrap_dependencies.py
cmake --preset debug
cmake --build --preset debug -j 6
ctest --preset debug
./build/debug/netarena_interop_server --port 9000 --max-seconds 300
```

服务端仅监听本机 IPv4；启动后输出 `READY port=9000`。最后一条命令会占用当前终端，300 秒后退出，也可用 Ctrl+C 结束。未传 `--max-seconds` 时默认运行 60 秒。

如果缺少 CMake，可先安装仓库本地工具：

```sh
python3 -m venv .venv
.venv/bin/python -m pip install 'cmake==3.31.10'
export PATH="$PWD/.venv/bin:$PATH"
```

Release 检查：

```sh
cmake --preset release
cmake --build --preset release -j 6
ctest --preset release
```

Debug 和 Release 各有 10 项 CTest，覆盖协议、ENet 回环、C++ 客户端握手及移动、比赛和房间规则。运行时检查在 Release 中仍启用。构建还会独立编译 12 个引擎无关的公开头文件。

## Godot 客户端与真实互通验收（macOS）

已锁定 ENet 1.3.18、nlohmann/json 3.12.0、godot-cpp 固定提交和 Godot 4.5.1。以下命令将引擎放在仓库的 `.deps/` 中，不修改系统安装：

```sh
python3 tools/bootstrap_dependencies.py --godot-runtime
export GODOT_BIN="$PWD/.deps/godot-runtime/Godot.app/Contents/MacOS/Godot"
cmake --preset godot-debug -DGODOT_EXECUTABLE="$GODOT_BIN"
cmake --build --preset godot-debug -j 6
ctest --preset godot-debug
```

该配置有 **11 项 CTest**，其中 `godot_interop` 启动真正的 Godot 和独立 C++ 服务端，检查握手、超过 JavaScript 精确整数范围的 nonce 往返、主动断开，以及错误协议版本被拒绝。设置 `GODOT_EXECUTABLE` 后才会注册引擎测试。

手动演示：一个终端执行服务端，另一个终端打开客户端，然后点击 **Connect → Ping → Disconnect**：

```sh
# 终端一（仓库根目录）
./build/godot-debug/netarena_interop_server --port 9000 --max-seconds 300

# 终端二（仓库根目录）
export GODOT_BIN="$PWD/.deps/godot-runtime/Godot.app/Contents/MacOS/Godot"
"$GODOT_BIN" --path client/godot
```

客户端与验收脚本会显式加载原生扩展，无需先用编辑器扫描；当前项目没有需要导入的外部素材。每次引擎验收都使用不含 `.godot` 缓存的临时副本。本环境 Godot 4.5.1 的立即导入退出存在崩溃，直接运行流程避开该路径，详情见[WP-02 记录](docs/wp02-implementation.md)。

如果已有匹配的引擎，`--godot` 只准备绑定源码，再将 `GODOT_EXECUTABLE` 指向自己的引擎。当前 `.gdextension` 仅配置 macOS 动态库；Linux/Windows 客户端打包仍待实现和验证。

## 依赖与模块

下载地址、版本和 SHA-256 在 [deps.lock.json](deps.lock.json)。`--offline` 只使用已校验的 `.deps/downloads/` 缓存；缺少缓存或哈希不匹配会失败。源码归档解压标记用于避免重复解压，不代表每次构建都会逐文件检查源码修改。依赖缓存、构建产物、Godot 导入缓存不入库。许可见[第三方说明](THIRD_PARTY_NOTICES.md)。

| 路径 | 职责 |
| --- | --- |
| `shared/foundation/`、`shared/contracts/` | 强类型 ID、Result、时钟、20 种消息及 ITransport |
| `shared/protocol/`、`schemas/` | 有界 JSON 编解码和协议规范 |
| `shared/movement/` | 客户端和服务器共享的固定步长移动与静态碰撞 |
| `server/game/` | 权威比赛、金币拾取、得分、结算与房间生命周期 |
| `adapters/transport/` | ENet 生命周期、通道、队列、超时与连接代际 |
| `client/core/` | 引擎无关的最小握手与心跳状态机 |
| `client/godot/` | 原生桥接、组合入口、可见面板和引擎测试脚本 |
| `apps/interop_server.cpp` | 本机互通验收服务端 |
| `tests/` | 单元测试、真实回环和 Godot 进程验收 |
| `tools/`、`cmake/` | 显式依赖准备及离线构建接线 |

协议将 uint64 序号/nonce 编码为十进制字符串，严格拒绝未知字段、重复键、超限和方向错误。Control/Input 可靠有序，Snapshot 使用不可靠有序通道。发送成功表示本地入队；连接代际防止旧连接标识复用。当前演示身份不具备登录认证能力。

下一工作包 WP-04 将访客会话、服务器应用和 Godot 游戏画面接入已有规则。

- [WP-03 实施与验证记录](docs/wp03-implementation.md)
- [WP-02 实施与验证记录](docs/wp02-implementation.md)
- [协议 v1 线格式](schemas/protocol-v1.md)
- [WP-01 实施与验证记录](docs/wp01-implementation.md)
- [模块解耦与依赖设计](docs/模块解耦与依赖设计.md)
- [服务端与客户端接口设计](docs/服务端与客户端接口设计.md)
- [测试回放与 AI 接口设计](docs/测试回放与AI接口设计.md)
