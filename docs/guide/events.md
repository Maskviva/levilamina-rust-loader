# 事件

一个接口订阅一切：LeviLamina 总线上的事件、加载器自己钩出来的事件、别的模组发布的事件。

```rust
use levilamina::prelude::*;
use levilamina::event::names;

ctx.server()
    .subscribe_event(names::PLAYER_JOIN, EventPriority::Normal, |ev| {
        if let Some(p) = ev.player() {
            Player::broadcast(&format!("§e{} 加入了游戏", p.name));
        }
    })?
    .forget();
```

## 三步

### 1. 挑事件名

用 `levilamina::event::names` 里的常量。上游改名时你会得到编译错误而不是运行时 `Err`。

常量表全在 [Event API](/api/event#事件名常量表)。

不在表里的事件直接传字符串——**支持唯一后缀匹配**，写 `"PlayerAddExperienceEvent"` 就行，不用写全 `ll::event::PlayerAddExperienceEvent`。

服务器里 `/levirs events` 能列出当前全部可订阅的 id。

### 2. 挑优先级

```rust
pub enum EventPriority { Highest = 0, High = 1, Normal = 2, Low = 3, Lowest = 4 }
```

数字小的先跑。

| 你想干什么 | 用哪档 |
| --- | --- |
| 拦截 / 否决（保护类模组） | `Highest` |
| 修改内容 | `High` |
| 一般业务 | `Normal` |
| 观察最终结果、统计 | `Lowest` |

### 3. 别忘了 forget

```rust
ctx.server().subscribe_event(...)?.forget();
```

不接返回值的话 `Listener` 当场被 drop，订阅立刻失效。

## 读事件数据

回调收到 `&mut EventRef`。数据是 SNBT，两种读法：

```rust
// 直接看原文（调试时最有用）
println!("{}", ev.snbt());

// 结构化
let v = ev.value()?;
let x = v.get("x").and_then(|n| n.as_i64()).unwrap_or(0);
let name = v.path("_player.name").and_then(|n| n.as_str()).unwrap_or("?");
```

::: tip 不知道载荷长什么样就先打印
```rust
ctx.server().subscribe_event("SomeEvent", EventPriority::Lowest, |ev| {
    println!("{}", ev.snbt());
})?.forget();
```
比翻头文件快。
:::

### 玩家身份

大部分玩家相关的事件里，桥接会拼一个 `_player` 块进去：

```rust
if let Some(id) = ev.player() {
    println!("{} {} {}", id.name, id.xuid, id.uuid);
}

// 或者直接拿句柄
if let Some(p) = ev.player_handle() {
    p.send_message("嗨")?;
    let (x, y, z) = p.get_actor()?.pos()?;
}
```

`player_handle()` 优先按 XUID 构造，拿不到才退回名字。

## 改事件数据

```rust
ctx.server().subscribe_event(names::PLAYER_CHAT, EventPriority::High, |ev| {
    let Ok(mut v) = ev.value() else { return };

    if let Some(msg) = v.get("message").and_then(|m| m.as_str()) {
        if msg.contains("垃圾话") {
            v.insert("message", NbtValue::String("[已屏蔽]".into()));
            ev.set_value(&v);
        }
    }
})?.forget();
```

`value()` 会带上本次回调里已经写过的改动，所以多次「读→改→写」能串起来。

## 取消事件

```rust
ctx.server().subscribe_event(names::PLAYER_DESTROY_BLOCK, EventPriority::Highest, |ev| {
    let Ok(v) = ev.value() else { return };
    let (Some(x), Some(z)) = (
        v.get("x").and_then(|n| n.as_i64()),
        v.get("z").and_then(|n| n.as_i64()),
    ) else { return };

    if 在保护区内(x, z) {
        ev.cancel();
        if let Some(p) = ev.player_handle() {
            let _ = p.tell("§c这里不能挖", MessageType::Tip);
        }
    }
})?.forget();
```

对不可取消的事件调 `cancel()` 不报错也不生效。哪些能取消见 [常量表](/api/event#事件名常量表)。

## 写保护类模组的常见坑

::: warning 破坏保护只有一个事件
没有 `PlayerDestroyingBlockEvent`。放置那一对确实是 `PlayerPlacingBlockEvent`（前置）+ `PlayerPlacedBlockEvent`（后置），但破坏只有一个 `PlayerDestroyBlockEvent`——过去式的名字，但它就是可取消的前置事件。
:::

::: warning 别漏了「不需要点击」的干扰方式
一个既不能破坏、不能放置、不能交互、不能攻击的访客，照样可以：

- **撞开实体**（`PlayerPushEntityEvent`）——把羊全赶出羊圈，或者把船顶进虚空
- **丢东西**（`PlayerDropItemEvent`）——掉落物刷屏、卡实体上限
- **从背包界面拖出物品**——和 Q 键是两条不同的代码路径，`PlayerDropItemEvent` 两条都钩了

全部锁死之后剩下的就是这几样。
:::

::: warning 命令事件只报玩家
`ExecutingCommandEvent` **不上报**控制台、命令方块和其他非玩家来源。这是故意的——拦掉控制台会把服主锁在自己服务器外面。
:::

## 高频事件

`HopperTransferEvent`、移动类事件一秒能触发几千次。

- 回调里**不要打日志**
- 不要做 I/O、不要开数据库事务
- 先做最便宜的判断早退

```rust
ctx.server().subscribe_event(names::HOPPER_TRANSFER, EventPriority::Lowest, |ev| {
    let Ok(v) = ev.value() else { return };
    let Some(x) = v.get("x").and_then(|n| n.as_i64()) else { return };

    if !我关心的坐标(x) { return; }      // 早退

    // 到这里才做贵的事
})?.forget();
```

## 别的模组的事件

`subscribe_event` 订的是引擎总线。模组之间自己约定的消息走 [Bus](/api/bus)：

```rust
use levilamina::bus;

bus::subscribe("plot:enter", |_topic, payload| {
    println!("{payload}");
    false
})?.forget();
```
