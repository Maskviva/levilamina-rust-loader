# Client — 客户端 API

加载器有服务端和客户端两套构建。客户端构建里，模组能操作本地客户端：本地玩家、按键绑定、界面状态。

```toml
[dependencies]
levilamina = { version = "26.20.4", default-features = false, features = ["client"] }
```

::: warning server 和 client 互斥
必须**恰好**启用一个。同时开或者一个都不开都会在编译期报错——这是故意的，混在一起会产生错误的 ABI 布局。
:::

```rust
use levilamina::prelude::*;

impl LeviMod for MyClientMod {
    fn on_enable(&mut self, ctx: &ModContext) -> Result<()> {
        let c = ctx.client();
        if c.is_in_level() {
            let me = c.local_player()?;
            ctx.logger().info(&format!("我是 {}", me.real_name()?));
        }
        Ok(())
    }
}
```

## 状态

| API | 返回 | 说明 |
| --- | --- | --- |
| `c.gaming_status()` | `GamingStatus` | **线程安全** |
| `c.is_in_level()` | `bool` | 在不在世界里 |
| `c.local_player()` | `Result<Player>` | 本地玩家 |
| `c.screen_name()` | `Result<String>` | 当前界面名 |
| `c.current_tick()` | `u64` | |
| `c.tick_delta_time()` | `f64` | |

`local_player()` 返回的是普通的 [`Player`](/api/player) 句柄，整套玩家 API 都能用，包括 `.get_actor()`。

## 调度

和服务端同名同义，只是排到**客户端线程**：

| API | 说明 |
| --- | --- |
| `c.schedule(f)` | 线程安全 |
| `c.schedule_after(delay, f)` | 线程安全 |
| `c.cancel_task(id)` | |
| `c.pending_tasks()` | |

## 事件

| API | 说明 |
| --- | --- |
| `c.subscribe_event(id, priority, handler)` | 和服务端同接口 |
| `c.list_events()` | 列出可订阅的事件 |

## 按键绑定

```rust
use levilamina::client::{KeyBinding, KeyAction};

let binding = KeyBinding::register(
    &[0x42],                       // 键码，B
    Some(|| println!("按下")),
    Some(|| println!("松开")),
)?;
binding.key_codes();               // 查绑了哪些键
// binding 被 drop → 自动解绑
```

`down` / `up` 都在客户端线程触发，两个都可以传 `None`。

`KeyAction`：`Up = 0`、`Down = 1`。

## 两边都有的东西

这些不分服务端 / 客户端，两种构建里都在：

- [`Block`](/api/block) / [`Actor`](/api/entity) / [`Player`](/api/player) / [`ItemStack`](/api/item) / [`Container`](/api/container)
- [`NbtValue`](/api/nbt) / [`KvDb`](/api/data) / [`system`](/api/system) / [`Logger`](/api/log)
- [`bus`](/api/bus) / [`service`](/api/service)
- [`packets()`](/api/packet)（ABI 槽位排在客户端专属块之前，两边都提供）

服务端专属的是：`command`、`gui`、`money`、`scoreboard`、`server`、`sim`、`more_dimensions`。
