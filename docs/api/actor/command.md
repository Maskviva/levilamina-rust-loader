# Command — 命令

两件事：**执行**原版命令，**注册**自己的命令。

## 执行原版命令

```rust
let r = ctx.server().execute_command("say hello")?;
if r.success { println!("{}", r.output); }
```

```rust
pub struct CommandResult {
    pub success: bool,
    pub output: String,
}
```

以控制台身份执行。这是很多"写操作"最省事的实现方式——`/setblock`、`/summon`、`/effect`、`/tp` 都比自己拼 API 稳。

## 注册命令：CommandBuilder

带类型的参数、多重载、枚举补全，客户端能看到 `/` 提示。

```rust
use levilamina::prelude::*;

ctx.server()
    .command("home", "回到你的家", CommandPermission::Any)
    // 重载 0：/home
    .overload(|o| o)
    // 重载 1：/home set <名字>
    .overload(|o| o
        .required_enum("set", ParamType::Enum, "home_kw_set")
        .required("name", ParamType::String))
    // 重载 2：/home <名字>
    .overload(|o| o.required("name", ParamType::String))
    .register(|inv| {
        match inv.overload {
            0 => inv.success("传送中…"),
            1 => {
                let name = inv.arg("name").and_then(|v| v.as_str()).unwrap_or("");
                inv.success(&format!("已保存：{name}"));
            }
            _ => inv.error("用法错误"),
        }
    })?;
```

### OverloadBuilder

| 方法 | 说明 |
| --- | --- |
| `.required(name, kind)` | 必填参数 |
| `.optional(name, kind)` | 可选参数 |
| `.required_enum(name, kind, enum_name)` | 必填，取值来自已注册的枚举 |
| `.optional_enum(name, kind, enum_name)` | 可选，同上 |

::: warning 重载顺序就是 inv.overload 的编号
`.overload()` 的调用顺序决定了 `inv.overload` 拿到的是 0、1 还是 2。中间插一个新重载会把后面全部错位——加重载请加在末尾，或者同步改 `match`。
:::

### CommandInvocationEx

| 字段 / 方法 | 说明 |
| --- | --- |
| `inv.overload: usize` | 命中的是第几个重载 |
| `inv.args: NbtValue` | 全部参数 |
| `inv.arg(name) -> Option<&NbtValue>` | 按名字取一个参数 |
| `inv.origin: CommandOrigin` | 谁执行的 |
| `inv.success(msg)` | 成功输出 |
| `inv.error(msg)` | 失败输出（红色） |

```rust
pub struct CommandOrigin {
    pub name: String,
    pub origin_type: i32,
    pub dimension: Option<i32>,
    pub position: Option<(f64, f64, f64)>,
}
```

`position` 和 `dimension` 在控制台执行时是 `None`——**别 unwrap**。

### ParamType 全部取值

| 取值 | 客户端表现 |
| --- | --- |
| `Int` `Float` `Bool` `String` | 基础类型 |
| `RawText` `Message` `Json` | 文本三档，`Message` 支持 `@a` 展开 |
| `Actor` `Player` | 目标选择器，客户端会提示 `@a @e @p @s` |
| `BlockPos` `Vec3` | 坐标，支持 `~` 相对坐标 |
| `RelativeFloat` | 单个支持 `~` 的浮点 |
| `Item` `BlockName` `Effect` `ActorType` | 对应的原版补全列表 |
| `Dimension` | 维度 |
| `Command` | 一整条子命令 |
| `FilePath` | 文件路径 |
| `Enum` `SoftEnum` | 自定义枚举，见下 |

### 枚举与 soft enum

**枚举**是编译期固定的取值集合，注册后不能改：

```rust
ctx.server().register_command_enum("home_action", &[
    ("set", 0),
    ("del", 1),
    ("list", 2),
])?;
```

**soft enum** 可以在运行时更新——世界名、玩家自定义的传送点这类东西用它：

```rust
ctx.server().register_command_soft_enum("home_names", &[])?;

// 之后随时更新
ctx.server().update_command_soft_enum(
    "home_names",
    SoftEnumOp::Set,          // Set 整表替换 / Add 追加 / Remove 移除
    &["家", "矿洞", "村庄"],
)?;
```

::: tip 为什么需要 soft enum
基岩版的命令树在玩家进服时下发一次，之后不能重新注册命令。soft enum 是唯一能在运行时改变客户端补全内容的机制。新建一个世界之后不需要重启，刷新 soft enum 即可出现在补全里。
:::

## 权限档位

```rust
pub enum CommandPermission {
    Any = 0,            // 所有人
    GameDirectors = 1,
    Admin = 2,
    Host = 3,
    Owner = 4,          // 仅控制台
}
```

这是引擎层的粗粒度门禁。更细的权限（比如"只有地皮主人能用"）在处理函数里自己判断。

## 老接口：raw-text 命令

`register_command` 是 v0.x 时代的接口，处理函数拿到的是整行原文，没有参数解析和客户端补全：

```rust
ctx.server().register_command(
    "ping", "测试", CommandPermission::Any,
    |inv| inv.success("pong"),
)?;
```

只有一条命令、没有参数的时候用它够了。有参数就用 `CommandBuilder`。

## 拦截别人的命令

订阅 `ExecutingCommandEvent`，见 [Event](/api/infra/event)。注意它只报玩家发起的命令，控制台不上报。
