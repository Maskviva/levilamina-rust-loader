# 命令

命令有两个方向：**执行**已有命令（含全部原版命令），和**注册**自己的命令。两者都已支持，且"执行原版命令"是当前实现各种写操作最通用的手段。

## 执行命令

`Server::execute_command` 以控制台（Owner 权限）身份执行任意命令，返回是否成功与输出文本：

```rust
let r = server.execute_command("time set day")?;
if r.success {
    logger.info(&r.output);
} else {
    logger.warn(&format!("失败: {}", r.output));
}
```

这条路径的价值远超"跑一条命令"：**原版命令行几乎能做到一切写操作**——`/setblock`、`/tp`、`/gamemode`、`/give`、`/effect`、`/summon`……在对应的强类型 API 就绪之前，拼一条命令是完全正当的做法（桥接内部的若干 API 也是这么实现的，原因见[设计取舍记录](/advanced/decisions)第 3 条）。

```rust
// 例:把玩家传送到出生点并切换旅行者模式
server.execute_command(&format!("tp \"{name}\" 0 100 0"))?;
server.execute_command(&format!("gamemode adventure \"{name}\""))?;
```

> 拼命令时注意给玩家名加引号，防止名字带空格时被拆成多个参数。

## 注册自定义命令

`Server::register_command` 注册 `/<name> [args]` 形式的命令：

```rust
fn on_enable(&mut self, ctx: &ModContext) -> Result<()> {
    ctx.server().register_command(
        "greet",                      // 命令名 → /greet
        "向某人问好",                  // 描述(命令列表里显示)
        CommandPermission::Any,       // 权限门槛
        |inv| {
            // inv.args 是命令名后面的整段原样文本
            let who = inv.args.trim();
            if who.is_empty() {
                inv.error("用法: /greet <名字>");
            } else {
                inv.success(&format!("你好, {who}! (来自 {})", inv.origin));
            }
        },
    )?;
    Ok(())
}
```

处理函数收到 `CommandInvocation`：

| 字段/方法 | 说明 |
| --- | --- |
| `inv.args` | 命令名之后的整段原样文本（当前版本参数不做类型解析） |
| `inv.origin` | 调用者名字（控制台或玩家名） |
| `inv.success(msg)` / `inv.error(msg)` | 回写执行结果，会显示给调用者 |

权限级别 `CommandPermission`：`Any` / `GameDirectors` / `Admin` / `Host` / `Owner`。

### 自己解析子命令

当前所有参数都是一整段文本，子命令自己 `split` 即可（`region-scan` 示例就是这么做的）：

```rust
|inv| {
    let mut it = inv.args.split_whitespace();
    match it.next() {
        Some("list") => { /* … */ inv.success("…"); }
        Some("add")  => { let name = it.next().unwrap_or(""); /* … */ }
        _ => inv.error("用法: /mymod <list|add>"),
    }
}
```

> 🧩 规划中的升级：类型化参数（坐标、目标选择器、枚举等 24 种参数类型）与多重载注册，届时 `/mymod add <玩家> <数量>` 这类签名可以直接声明、由引擎解析并给出补全提示。设计见 [Command 参考的"参数化命令构建"](/api/command)。

## 注册时机与生命周期

- **在 `on_enable` 里注册**。Bedrock 无法注销命令，因此命令本体会存活到服务器关闭；模组禁用/卸载后，加载器把该命令"静音"（执行时返回不可用错误），重新启用时自动重绑。
- 同名命令重复注册会失败，返回 `Err` 正常处理即可。

## 拦截别人的命令

想在**任意**命令执行前后做点什么（审计、拦截），不用注册命令，订阅命令事件即可：

```rust
server.subscribe_event("ExecutingCommandEvent", EventPriority::Normal, move |ev| {
    logger.info(&format!("即将执行: {}", ev.snbt()));
    // 需要拦截时: ev.cancel();
})?;
```
