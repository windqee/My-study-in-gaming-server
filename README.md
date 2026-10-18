# My-study-in-gaming-server

用来求职的游戏服务器项目。项目名 **NetArena**：以权威游戏服务器、客户端同步和可复现测试证据为目标，按[开发路线图](docs/superpowers/plans/2026-10-17-netarena-roadmap.md)逐步实现。

当前完成 **WP-01：工程基础与共享契约**。可运行程序是基础类型演示，不是网络服务器；尚不能联机或游玩。

## 构建与运行

需要 C++20 编译器、CMake 3.25 或更新版本及 Make。当前实际验证平台为 macOS arm64；其他平台尚未验证。配置和构建只使用 C++ 标准库，不下载任何依赖。

在仓库根目录执行：

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
./build/debug/netarena_foundation_demo

cmake --preset release
cmake --build --preset release
ctest --preset release
./build/release/netarena_foundation_demo
```

测试使用始终启用的运行时检查，不依赖 Release 下会禁用的 `assert`。每个配置包含 foundation、contracts 两个单元测试程序和 foundation_demo 冒烟检查；构建时还会独立编译每个公开头文件。

如果机器缺少 CMake，可选用 Python 在仓库本地安装工具；此步骤需要联网，后续 CMake 配置不需要：

```sh
python3 -m venv .venv
.venv/bin/python -m pip install 'cmake==3.31.10'
export PATH="$PWD/.venv/bin:$PATH"
```

`pyproject.toml` 仅预留未来 Python 工具配置，目前没有 Python 运行时代码、包或依赖。

## 目录

| 路径 | 用途 |
| --- | --- |
| `shared/foundation/` | Result、强类型 ID/序号、字节/凭证包装和单调时钟 |
| `shared/contracts/` | 消息/状态 DTO、基础值校验、ITransport 抽象端口 |
| `apps/foundation_demo.cpp` | Result、序号、时钟与 Hello 消息的本地演示 |
| `tests/unit/` | 基础类型及契约检查 |
| `VERSION.txt` | 唯一手写版本来源，构建读取后生成版本头 |
| `docs/` | 需求、设计、实施计划与验证记录 |

## 当前边界

业务 ID 使用互不混用的字符串类型；Tick、InputSeq、SnapshotSeq、Epoch 为独立 uint64 强类型，通过十进制字符串转换并拒绝溢出递增。ConnectionId 为本地槽位和代际，不能当作 PlayerId。

DTO 在内存中可以尚未通过验证。现有校验只覆盖有限向量、矩形边界、基础端点和输入方向；WP-02 负责完整协议编码、长度上限、消息方向及阶段相关字段校验。Error 消息由边界调用者脱敏并限制长度。凭证包装不提供加密或内存安全擦除保证。

尚未实现 ENet、Godot、实际网络、JSON 编解码、游戏逻辑或密码学。GameEvent、TraceRecord、ITraceSink 的详细契约留给 WP-09；ITokenProvider 留给 WP-07。完成 WP-01 不代表 W01、G0 或 WP-02 完成。

- [WP-01 实施与验证记录](docs/wp01-implementation.md)
- [WP-01 细化计划](docs/superpowers/plans/2026-10-18-wp01-foundation.md)
- [服务端与客户端接口设计](docs/服务端与客户端接口设计.md)
- [模块解耦与依赖设计](docs/模块解耦与依赖设计.md)
- [测试回放与 AI 接口设计](docs/测试回放与AI接口设计.md)
