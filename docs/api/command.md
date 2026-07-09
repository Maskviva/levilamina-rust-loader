# Command — 命令

执行命令与注册自定义命令。**仅限服务器线程**。

> **接口来源**：当前 `Command::register` 对应 LeviLamina 的运行时命令系统（`ll::command::CommandRegistrar` / `CommandHandle` / `RuntimeOverload`，均在 `ll/api/command/`），今天的桥接只把参数原样当作一整段 `RawText` 字符串传过 FFI 边界。下方"参数化命令构建"一节按真实头文件描述这套系统本来能做到的程度——多重载、类型化参数、自定义枚举——供后续扩展 ABI 时对照。

| API | 作用 | 状态 |
| --- | --- | :---: |
| `Command::execute(cmd)` | 以控制台（Owner）身份执行命令，返回成功与输出 | ✅ |
| `Command::register(name, description, permission, handler)` | 注册自定义命令 `/name [args]`（`args` 是原样的整段文本） | ✅ |
| `player.runcmd(cmd)` | 以某玩家身份执行命令 | 🧩 |

## 说明

- 自定义命令会**存活到服务器关闭**（Bedrock 无法注销命令）；应在 `on_enable` 中注册。模组禁用/卸载后其命令自动静音。
- `handler` 收到 `CommandInvocation`（参数、调用者），可回写成功/错误输出。

## 参数化命令构建（进阶，🧩 规划）

原生系统能表达"一个命令名 + 多套不同参数签名（重载）+ 每个参数带具体类型"，而不只是一整段文本。这里按真实的类型/方法命名说明其形状。

### 参数类型

`ll::command::ParamKind` 定义了 24 种参数类型，注册命令时给每个参数指定其中一种：

| Kind | 说明 |
| --- | --- |
| `Int` / `Float` / `Bool` / `String` | 基础类型 |
| `RelativeFloat` | 支持 `~` 相对坐标写法的浮点数 |
| `Vec3` | 三个浮点坐标（可各自使用 `~`） |
| `BlockPos` | 三个整数坐标 |
| `Dimension` | 维度 |
| `Actor` / `Player` | 目标选择器（如 `@a`、`@e[type=cow]`、玩家名），**解析结果是 0 个或多个匹配对象**，不是单个值 |
| `WildcardActor` | 允许 `*` 通配的目标选择器 |
| `RawText` | 剩余部分整段原样文本（当前桥接唯一支持的类型） |
| `Message` | 聊天消息文本，选择器会被展开为实际名字（用于类似 `/say`、`/tell`） |
| `Item` | 物品 id（可带数据值） |
| `BlockName` | 方块 id |
| `BlockState` | 方块状态列表 |
| `Effect` | 药水效果 id |
| `ActorType` | 实体类型 id |
| `Enum` | 预先注册的固定枚举（值集合注册后不再变） |
| `SoftEnum` | 可在运行时动态增删值的枚举（如随时变化的候选列表，仍支持玩家自己输入不在列表里的值） |
| `IntegerRange` | 整数区间，如 `1..5` |
| `WildcardInt` | 允许 `*` 通配的整数 |
| `FilePath` | 文件路径 |
| `JsonValue` | 一段 JSON |
| `Command` | 嵌套的子命令解析器（进阶，用于命令套命令） |

### 构建一个命令

一个命令名可以注册**任意多个重载**（不同的参数签名），运行时按玩家实际输入匹配其中一个：

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `Command::overload(name)` | 为已有命令名开一个新的参数重载 | `CommandHandle::runtimeOverload()` |
| `.required(param_name, kind)` | 追加一个必填参数 | `RuntimeOverload::required` |
| `.optional(param_name, kind)` | 追加一个可选参数 | `RuntimeOverload::optional` |
| `.required(param_name, ParamKind::Enum, enum_name)` / `.optional(...)` | 追加一个枚举/软枚举参数，引用已注册的枚举名 | 同名的 `Enum`/`SoftEnum` 重载版本 |
| `.literal(text)` | 追加一个固定字面量（用来做子命令分支，如 `/mymod list`） | `RuntimeOverload::text` |
| `.execute(handler)` | 为这个重载绑定处理函数 | `RuntimeOverload::execute` |
| `Command::alias(name, alias)` | 给命令注册一个别名 | `CommandHandle::alias` |

读取参数：处理函数按名字取出每个参数（`rt["参数名"]`），先确认它是期望的类型再取值——原生是 `RuntimeCommand::operator[]` 配合 `ParamStorageType::hold`/`get<N>`。

> 原生 `RuntimeOverload` 还有 `.modify(fn)`（直接改底层 `CommandParameterData`）、`.postfix(text)`、`.option`/`.deoption`（切换参数标志位）几个更底层的逃生舱口，暂未纳入简化层。

### 自定义枚举 / 软枚举

原生其实有两套注册枚举的路径：`tryRegisterEnum`/`addEnumValues`（更底层，需要配一个 C++ 侧的类型解析函数指针，难以简单跨 FFI 暴露）和下表选用的 `tryRegisterRuntimeEnum`/`addRuntimeEnumValues`（只需要"名字 → 数值"这样的简单列表，是给模组用的实用版本）。

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `Command::register_enum(name, values)` | 注册一个固定枚举（名字 → 值） | `CommandRegistrar::tryRegisterRuntimeEnum` |
| `Command::add_enum_values(name, values)` | 给已注册枚举追加取值 | `CommandRegistrar::addRuntimeEnumValues` |
| `Command::register_soft_enum(name, values)` | 注册一个软枚举（取值列表可后续动态增删） | `CommandRegistrar::tryRegisterSoftEnum` |
| `Command::set_soft_enum_values(name, values)` | 整体替换软枚举取值 | `CommandRegistrar::setSoftEnumValues` |
| `Command::add_soft_enum_values` / `remove_soft_enum_values` | 增量增删软枚举取值 | `CommandRegistrar::addSoftEnumValues` / `removeSoftEnumValues` |

> 软枚举适合"候选列表会随游戏状态变化"的场景，比如把当前在线玩家、当前已定义的家的名字，做成一个会自动刷新提示的参数。

## 相关类型

| 类型 | 说明 |
| --- | --- |
| `CommandPermission` | `Any` / `GameDirectors` / `Admin` / `Host` / `Owner` |
| `CommandResult` | `success: bool` 与 `output: String` |
| `CommandInvocation` | `args`、`origin`，以及 `success()` / `error()` |
| `ParamKind` | 见上方参数类型表 |

