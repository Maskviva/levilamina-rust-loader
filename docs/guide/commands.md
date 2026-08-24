# 命令

## 执行原版命令

```rust
let r = ctx.server().execute_command("gamerule doDaylightCycle false")?;
if r.success { println!("{}", r.output); }
```

以控制台身份执行。

::: tip 这是很多写操作最省事的实现
`/fill`、`/clone`、`/setblock`、`/summon`、`/effect`、`/tp`——引擎内部的实现比逐格调用 API 快一个数量级，而且行为和管理员手打完全一致。

大范围改方块尤其如此：一百万格的 `Block::set` 循环是一百万次 FFI，一条 `/fill` 是一次。
:::

## 注册命令

```rust
use levilamina::prelude::*;

ctx.server()
    .command("kit", "领取礼包", CommandPermission::Any)
    .overload(|o| o)                                              // /kit
    .overload(|o| o.required("name", ParamType::String))          // /kit <名字>
    .overload(|o| o                                               // /kit give <玩家> <名字>
        .required_enum("give", ParamType::Enum, "kit_kw_give")
        .required("target", ParamType::Player)
        .required("name", ParamType::String))
    .register(|inv| {
        match inv.overload {
            0 => inv.success("可用礼包：新手、建筑"),
            1 => {
                let name = inv.arg("name").and_then(|v| v.as_str()).unwrap_or("");
                inv.success(&format!("已发放 {name}"));
            }
            2 => { /* … */ }
            _ => inv.error("用法错误"),
        }
    })?;
```

::: warning 重载编号 = 注册顺序
`inv.overload` 是第几个 `.overload()`。中间插新的会把后面全部错位——加在末尾，或者同步改 `match`。
:::

## 参数

按名字取，不是按下标：

```rust
let name = inv.arg("name").and_then(|v| v.as_str()).unwrap_or("");
let n    = inv.arg("count").and_then(|v| v.as_i64()).unwrap_or(1);
let on   = inv.arg("flag").and_then(|v| v.as_bool()).unwrap_or(false);
```

常用 `ParamType`：`String` `Int` `Float` `Bool` `Player` `Actor` `BlockPos` `Vec3` `Item` `BlockName` `Effect` `Message` `Enum` `SoftEnum`。全表见 [Command API](/api/actor/command#paramtype-全部取值)。

## 谁在执行

```rust
let who = &inv.origin;
println!("{} type={}", who.name, who.origin_type);

// 控制台执行时这两个是 None —— 别 unwrap
if let (Some(dim), Some(pos)) = (who.dimension, who.position) {
    // 有位置
}
```

## 枚举与运行时补全

固定取值用**枚举**：

```rust
ctx.server().register_command_enum("kit_action", &[("give", 0), ("list", 1)])?;
```

运行时会变的用 **soft enum**：

```rust
// 先注册成空的
ctx.server().register_command_soft_enum("kit_names", &[])?;

// 之后随时刷新
ctx.server().update_command_soft_enum("kit_names", SoftEnumOp::Set, &["新手", "建筑"])?;
```

::: tip 为什么必须有 soft enum
基岩版的命令树在玩家进服时下发一次，之后**不能重新注册命令**。soft enum 是唯一能在运行时改变客户端补全内容的机制。

所以模式是：命令一次注册定死，变化的部分放进 soft enum。新建一个世界不需要重启服务器，刷新 soft enum 就出现在补全里了。
:::

## 权限

```rust
pub enum CommandPermission { Any = 0, GameDirectors = 1, Admin = 2, Host = 3, Owner = 4 }
```

引擎层的粗粒度门禁。细的自己在处理函数里判：

```rust
.register(|inv| {
    if !是管理员(&inv.origin.name) {
        inv.error("§c权限不足");
        return;
    }
    // …
})?;
```

::: tip 控制台专属命令怎么做
把权限设成 `Owner`，或者在处理函数里检查 `origin` 是不是控制台。

授权类命令建议**只允许控制台执行**：如果管理员能给别人授权，那任何一个被误授权的账号都能把权限扩散出去，而且没有回滚点。控制台是唯一不依赖数据库状态就能拿到的入口，把它当信任根，最坏情况下权限表整个写坏也能恢复。
:::

## 老接口

没有参数的单条命令，用 `register_command` 就够：

```rust
ctx.server().register_command("ping", "测试", CommandPermission::Any, |inv| {
    inv.success("pong");
})?;
```

拿到的是整行原文，没有客户端补全。有参数就换 `CommandBuilder`。

## 拦别人的命令

订 `ExecutingCommandEvent`，见 [事件](/guide/events)。注意它只报玩家发起的命令。
