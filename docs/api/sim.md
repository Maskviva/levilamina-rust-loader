# SimPlayer — 模拟玩家

carpet 风格的假人。底层就是一个真实的 `ServerPlayer`，所以**整套 `Player` API 都能用在它身上**——这个类型只额外提供 `simulate*` 那些动作动词。

```rust
use levilamina::prelude::*;

let bot = ctx.server().spawn_sim_player("bot_1", 0, 100.0, 64.0, 200.0)?;

bot.move_to(110.0, 64.0, 200.0, 1.0, true)?;
bot.chat("我来了")?;

// 需要普通玩家能力时转过去
bot.player().send_message("这条消息发给假人自己")?;
let hp = bot.player().get_actor()?.health()?;
```

## 创建与查找

| API | 说明 |
| --- | --- |
| `Server::spawn_sim_player(name, dim, x, y, z)` | 创建，返回 `Result<SimPlayer>` |
| `Server::sim_player(name)` | 按名字取句柄（不校验） |
| `Server::is_simulated(name) -> bool` | 这个名字是不是假人 |
| `Server::list_sim_players()` | 列出全部假人 |
| `SimPlayer::by_name(name)` | 同 `Server::sim_player` |
| `bot.name() -> &str` | 名字 |
| `bot.player() -> Player` | 转成普通玩家句柄 |

::: tip 重启之后句柄没了但假人还在
假人是真实玩家，会随世界持久化。重启后用 `Server::sim_player(name)` 重新拿句柄，或者先 `is_simulated` 确认一下。
:::

## 移动

| API | 说明 |
| --- | --- |
| `bot.move_to(x, y, z, speed, face_target)` | 直线走 |
| `bot.navigate_to(x, y, z, speed)` | 寻路走（会绕障碍） |
| `bot.look_at(x, y, z)` | 转头看向某点 |
| `bot.jump()` | 跳 |
| `bot.stop()` | 停下 |
| `bot.set_sneaking(on)` | 潜行 |
| `bot.set_flying(on)` | 飞行 |

## 交互

| API | 说明 |
| --- | --- |
| `bot.attack()` | 攻击面前的目标 |
| `bot.interact()` | 交互（右键） |
| `bot.use_item()` | 使用手持物品 |
| `bot.interact_block(x, y, z, face)` | 对指定方块交互 |
| `bot.destroy_block(x, y, z, face)` | 挖指定方块 |
| `bot.destroy_look_at(hand)` | 挖正在看的方块 |
| `bot.stop_destroying()` | 停止挖掘 |
| `bot.drop_selected()` | 丢掉手上的 |
| `bot.chat(msg)` | 发言 |

`face` 是面的编号：0=下 1=上 2=北 3=南 4=西 5=东。

## 生命周期

| API | 说明 |
| --- | --- |
| `bot.respawn()` | 重生 |
| `bot.despawn(self)` | 移除。**消耗 self**，之后这个句柄用不了了 |

## 都在服务器线程

和 SDK 其余部分一样。对已经 despawn / 离线的假人调用会返回 `Err`。

## 典型用法：红石测试

```rust
// 让假人持续按一个按钮
let bot = ctx.server().spawn_sim_player("tester", 0, 0.0, 64.0, 0.0)?;
bot.look_at(0.0, 64.0, 5.0)?;

ctx.server().schedule_after(Duration::from_secs(1), move || {
    bot.interact().ok();
});
```
