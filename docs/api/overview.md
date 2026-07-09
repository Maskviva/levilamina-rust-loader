# API 参考总览

本参考只回答"**有哪些 API、各自做什么**"，不含教程——上手与示例见[初级开发](/guide/getting-started)，架构与设计原理见[高级开发](/advanced/architecture)。分类结构对标 [LSE(LegacyScriptEngine)](https://lse.levimc.org/zh/apis/) 的组织方式并适配到本 Rust 加载器。

## 状态标注

本加载器基于 FFI 桥接，能力按 ABI 版本演进。每个条目标注实现状态：

- ✅ **已支持** —— 当前 ABI 已提供该能力。
- 🧩 **规划** —— 对标 LSE 的目标设计，已按原生头文件核实可行，待扩展桥接后提供。

> 本加载器不是照搬 LSE。LSE 是 JS/Lua 脚本引擎，架构不同；本参考描述的是「以 LSE 为蓝本、适配到 Rust」的目标 API。部分 LSE 能力（使客户端崩溃、跨服传送、模拟玩家等）不在计划内。

## 分类索引

| 类别 | 负责 | 状态 | 页面 |
| --- | --- | :---: | --- |
| `Event` | 事件订阅（`subscribe_event` + `EventRef`） | ✅ | [Event](/api/event) |
| `Player` | 玩家对象 | 部分✅ | [Player](/api/player) |
| `Entity` | 实体对象 | 🧩 | [Entity](/api/entity) |
| `Block` | 方块对象 | 部分✅ | [Block](/api/block) |
| `Item` | 物品对象 | 🧩 | [Item](/api/item) |
| `Container` | 容器对象 | 🧩 | [Container](/api/container) |
| `ScoreBoard` | 计分板 | 🧩 | [ScoreBoard](/api/scoreboard) |
| `Packet`/`BlockEntity` 等 | 其他游戏对象 | 🧩 | [Objects](/api/objects) |
| `World` | 世界读写、区域扫描 | ✅ | [World](/api/world) |
| `Command` | 执行/注册命令 | ✅ | [Command](/api/command) |
| `Server` | 服务端状态、时间、天气、设置 | 部分✅ | [Server](/api/server) |
| `Nbt` | NBT 读写 | 🧩 | [Nbt](/api/nbt) |
| `Data` | 配置、数据库、经济、玩家数据 | 🧩 | [Data](/api/data) |
| `Gui` | 表单界面 | 🧩 | [Gui](/api/gui) |
| `System` | 文件、网络、进程、系统信息 | 🧩 | [System](/api/system) |
| `Log` | 日志 | ✅ | [Log](/api/log) |
| `Scheduler` | 任务调度 | ✅ | [Scheduler](/api/scheduler) |

## 页面编写方法（读参考前先知道这个）

游戏对象页（`Entity`、`Player` 等）的内容**直接从 LeviLamina SDK 的原生 C++ 头文件提取**（如 `mc/world/actor/Actor.h`），排除引擎内部的虚函数插桩（`$` 前缀或前导下划线的条目），再套上 LSE 风格的简洁命名。每页统一为三段：

1. **状态与来源说明**（页首引用块）——该页对应哪个原生类、当前支持到什么程度、句柄如何获取。
2. **方法表**——挑选出的高频操作，每行标注对应的**原生方法名**以便核实；规划中的小节以"🧩 规划"显式标出。
3. **附录：其余原生方法**（进阶）——该类里确实存在、尚未封装的原生方法清单，附一行说明。这样即使某个方法还没纳入简化层，也能在附录里查到并按需提需求，不会出现"文档说没有、其实原生有"的落差。

## 通用约定（速查）

详细展开见[核心概念](/guide/concepts)，这里只列结论：

- **句柄是标识符不是指针**：从事件回调或所属类别的方法获取（`Player::get(info)`、`Entity::get(id)`、`World::get_block(dim, pos)`——背后统一走桥接的 `Server` 入口，按类别分组只是文档组织方式）。句柄失效时调用安全地返回失败；不要长期缓存句柄，需要时重新获取。
- **命名**：分组 `类别::方法()`、对象 `对象.方法()`，一律 snake_case；可失败返回 `Result<T>`，可缺失返回 `Option<T>`。
- **线程**：所有回调在服务器线程；仅 `Log::*`、`Scheduler::*`、`Server::status()` 线程安全。
- **通用类型**：`IntPos`/`FloatPos`（坐标+维度）、`DirectionAngle`（pitch/yaw）、`Dimension`（0 主世界 / 1 下界 / 2 末地）。
- **ABI**：当前为 **v4**，追加式演进；加载器与模组须同一大版本（详见 [ABI 契约与演进](/advanced/abi)）。
