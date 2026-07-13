# Command — 命令

执行命令与注册自定义命令。**仅限服务器线程**。

> **接口来源**：当前 `Command::register` 对应 LeviLamina 的运行时命令系统（`ll::command::CommandRegistrar` / `CommandHandle` / `RuntimeOverload`，均在 `ll/api/command/`），桥接既支持把参数原样当作一整段 `RawText` 字符串的简单命令，也支持下方"参数化命令构建"所述的**多重载 + 类型化参数 + 自定义枚举**，两者均已可用。

| API | 作用 | 状态 |
| --- | --- | :---: |
| `Command::execute(cmd)` | 以控制台（Owner）身份执行命令，返回成功与输出 | ✅ |
| `Command::register(name, description, permission, handler)` | 注册自定义命令 `/name [args]`（`args` 是原样的整段文本） | ✅ |
| `player.runcmd(cmd)` | 以某玩家身份执行命令 | 🧩 |

## 说明

- 自定义命令会**存活到服务器关闭**（Bedrock 无法注销命令）；应在 `on_enable` 中注册。模组禁用/卸载后其命令自动静音。
- `handler` 收到 `CommandInvocation`（参数、调用者），可回写成功/错误输出。

## 参数化命令构建（进阶）

命令系统能表达"一个命令名 + 多套不同参数签名（重载）+ 每个参数带具体类型"，而不只是一整段文本。入口是 `Server::command(name, description, permission)`，返回一个 `CommandBuilder`。

### 参数类型

`ParamType` 定义了以下参数类型，注册命令时给每个参数指定其中一种：

| Kind | 说明 |
| --- | --- |
| `Int` / `Float` / `Bool` / `String` | 基础类型 |
| `RelativeFloat` | 支持 `~` 相对坐标写法的浮点数 |
| `Vec` | 三个浮点坐标（可各自使用 `~`） |
| `BlockPos` | 三个整数坐标 |
| `Dimension` | 维度 |
| `Actor` / `Player` | 目标选择器（如 `@a`、`@e[type=cow]`、玩家名），**解析结果是 0 个或多个匹配对象**，不是单个值 |
| `RawText` | 剩余部分整段原样文本 |
| `Message` | 聊天消息文本，选择器会被展开为实际名字（用于类似 `/say`、`/tell`） |
| `Item` | 物品 id（可带数据值） |
| `BlockName` | 方块 id |
| `Effect` | 药水效果 id |
| `ActorType` | 实体类型 id |
| `Enum` | 预先注册的固定枚举（值集合注册后不再变） |
| `SoftEnum` | 可在运行时动态增删值的枚举（如随时变化的候选列表，仍支持玩家自己输入不在列表里的值） |
| `FilePath` | 文件路径 |
| `Json` | 一段 JSON |
| `Command` | 嵌套的子命令解析器（进阶，用于命令套命令） |

### 构建一个命令

一个命令名可以注册**任意多个重载**（不同的参数签名），运行时按玩家实际输入匹配其中一个：

> 命令相关操作实际都挂在 `Server` 句柄上（本页按 `Command` 归类只是文档组织方式）。

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `Server::command(name, description, permission)` | 开始构建一个命令，返回 `CommandBuilder` | `CommandRegistrar::getOrCreateCommand` |
| `.overload(\|o\| o.required(...).optional(...))` | 追加一个参数重载；闭包里用 `OverloadBuilder` 逐个加参数 | `CommandHandle::runtimeOverload()` |
| `OverloadBuilder::required(name, kind)` | 追加一个必填参数 | `RuntimeOverload::required` |
| `OverloadBuilder::optional(name, kind)` | 追加一个可选参数 | `RuntimeOverload::optional` |
| `OverloadBuilder::required_enum(name, kind, enum_name)` / `optional_enum(...)` | 追加一个枚举/软枚举参数，引用已注册的枚举名 | 同名的 `Enum`/`SoftEnum` 重载版本 |
| `.register(handler)` | 完成并注册命令；`handler: FnMut(&CommandInvocationEx)`，对该命令的所有重载共用 | `RuntimeOverload::execute` |

读取参数：处理函数从 `CommandInvocationEx` 上按名字取值——`inv.arg("参数名") -> Option<&NbtValue>`，返回的 `NbtValue` 已按参数类型转好（整数 / 浮点 / 字符串 / 坐标 / 选择器结果等）。

> 原生 `RuntimeOverload` 还有字面量分支（`.text`）、命令别名（`CommandHandle::alias`）、以及 `.modify(fn)` / `.postfix(text)` / `.option` / `.deoption` 等更底层的逃生舱口，暂未纳入简化层。

### 自定义枚举 / 软枚举

原生其实有两套注册枚举的路径：`tryRegisterEnum`/`addEnumValues`（更底层，需要配一个 C++ 侧的类型解析函数指针，难以简单跨 FFI 暴露）和下表选用的 `tryRegisterRuntimeEnum`/`addRuntimeEnumValues`（只需要"名字 → 数值"这样的简单列表，是给模组用的实用版本）。

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `Server::register_command_enum(name, &[(name, value)])` | 注册一个固定枚举（名字 → `u64` 值），供 `ParamType::Enum` 参数引用 | `CommandRegistrar::tryRegisterRuntimeEnum` |
| `Server::register_command_soft_enum(name, &[value])` | 注册一个软枚举（取值列表可后续动态增删） | `CommandRegistrar::tryRegisterSoftEnum` |
| `Server::update_command_soft_enum(name, op, &[value])` | 用 `SoftEnumOp::Set` / `Add` / `Remove` 整体替换或增量增删软枚举取值 | `CommandRegistrar::setSoftEnumValues` / `addSoftEnumValues` / `removeSoftEnumValues` |

> 软枚举适合"候选列表会随游戏状态变化"的场景，比如把当前在线玩家、当前已定义的家的名字，做成一个会自动刷新提示的参数。

## 相关类型

| 类型 | 说明 |
| --- | --- |
| `CommandPermission` | `Any` / `GameDirectors` / `Admin` / `Host` / `Owner` |
| `CommandResult` | `success: bool` 与 `output: String` |
| `CommandInvocation` | `args`、`origin`，以及 `success()` / `error()` |
| `ParamType` | 见上方参数类型表 |
| `SoftEnumOp` | `Set` / `Add` / `Remove`（软枚举更新方式） |
| `CommandInvocationEx` | 参数化命令的调用上下文：`arg(name)` 取参数、`success()` / `error()` 回写输出 |

