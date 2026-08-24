# Event — 事件监听

一个订阅接口打天下：给事件名和优先级，拿回一个 RAII 句柄。事件数据是 SNBT，可以读、可以改写、可以取消。

```rust
use levilamina::prelude::*;
use levilamina::event::names;

ctx.server()
    .subscribe_event(names::PLAYER_CHAT, EventPriority::Normal, |ev| {
        if let Some(p) = ev.player() {
            println!("{} 说：{}", p.name, ev.snbt());
        }
    })?
    .forget();
```

## 订阅

```rust
Server::subscribe_event(
    &self,
    event_id: &str,
    priority: EventPriority,
    handler: impl FnMut(&mut EventRef) + 'static,
) -> Result<Listener>
```

`EventPriority`：`Highest=0`、`High=1`、`Normal=2`、`Low=3`、`Lowest=4`。数值小的先跑。

想拦截别人的操作就用 `Highest`；只想观察最终结果用 `Lowest`。

### 事件名可以只写后缀

引擎里的完整 id 长这样：`ll::event::PlayerChatEvent`。桥接支持**唯一后缀匹配**，所以直接写 `"PlayerChatEvent"` 就行。

只要后缀在全服注册的事件里唯一就能解析。不唯一会返回 `Err`，并且日志里会打印出所有相近的名字——所以名字写错不会静默失败。

::: tip 优先用 names 里的常量
`levilamina::event::names` 下有全部已核实的事件名常量。用常量的好处是上游改名时你会得到一个编译错误，而不是一个运行时的 `Err`。
:::

### Listener 的生命周期

```rust
let listener = ctx.server().subscribe_event(...)?;
// listener 被 drop → 自动退订

listener.forget();   // 活到模组卸载
```

99% 的情况是在 `on_enable` 里注册然后 `.forget()`。需要动态开关的才留着句柄。

::: warning 不要写 `subscribe_event(...)?;` 就完事
不接收返回值的话，`Listener` 当场就被 drop 了，订阅**立刻失效**。编译器会给一个 `unused_must_use` 警告，别忽略它。
:::

## 在回调里能做什么

回调收到 `&mut EventRef`：

| API | 说明 |
| --- | --- |
| `ev.id() -> &str` | 完整事件 id |
| `ev.snbt() -> &str` | 事件数据的 SNBT 原文 |
| `ev.value() -> Result<NbtValue>` | 解析成结构化的值（含尚未提交的改动） |
| `ev.player() -> Option<PlayerIdentity>` | 事件携带的玩家身份 `{name, xuid, uuid}` |
| `ev.player_handle() -> Option<Player>` | 直接拿一个 `Player` 句柄 |
| `ev.set_snbt(snbt)` | 整体替换事件数据 |
| `ev.set_value(&nbt)` | 结构化写回 |
| `ev.cancel()` | 取消（仅对可取消事件有效） |

### 改一个字段

```rust
ctx.server().subscribe_event(names::PLAYER_CHAT, EventPriority::High, |ev| {
    let Ok(mut v) = ev.value() else { return };
    if let Some(msg) = v.get("message").and_then(|m| m.as_str()) {
        if msg.contains("敏感词") {
            v.insert("message", NbtValue::String("[已屏蔽]".into()));
            ev.set_value(&v);
        }
    }
})?.forget();
```

`value()` 会带上本次回调里已经写过的改动，所以 `读 → 改 → 写` 可以串起来。

### 取消

```rust
ctx.server().subscribe_event(names::PLAYER_DESTROY_BLOCK, EventPriority::Highest, |ev| {
    if 不该让他挖 {
        ev.cancel();
    }
})?.forget();
```

对不可取消的事件调 `cancel()` 不会报错，但也不会有效果——下面的表里标了哪些能取消。

## 事件名常量表

全部在 `levilamina::event::names` 下，也按域分成了 `names::player` / `names::mob` / `names::server` 三个子模块（两种写法等价）。

### 玩家事件

| 常量 | 事件 id | 可取消 | 备注 |
| --- | --- | :---: | --- |
| `PLAYER_JOIN` | `PlayerJoinEvent` | ✅ | |
| `PLAYER_CONNECT` | `PlayerConnectEvent` | ✅ | |
| `PLAYER_CHAT` | `PlayerChatEvent` | ✅ | 载荷 `{name, message, _player}`，改 `message` 即改写发言 |
| `PLAYER_DISCONNECT` | `PlayerDisconnectEvent` | ❌ | |
| `PLAYER_DIE` | `PlayerDieEvent` | ✅ | |
| `PLAYER_RESPAWN` | `PlayerRespawnEvent` | ❌ | |
| `PLAYER_JUMP` | `PlayerJumpEvent` | ❌ | |
| `PLAYER_SPRINT` | `PlayerSprintEvent` | ❌ | |
| `PLAYER_SWING` | `PlayerSwingEvent` | ❌ | |
| `PLAYER_ATTACK` | `PlayerAttackEvent` | ✅ | |
| `PLAYER_PICK_UP_ITEM` | `PlayerPickUpItemEvent` | ✅ | |
| `PLAYER_USE_ITEM` | `PlayerUseItemEvent` | ✅ | |
| `PLAYER_INTERACT_BLOCK` | `PlayerInteractBlockEvent` | ✅ | |
| `PLAYER_DESTROY_BLOCK` | `PlayerDestroyBlockEvent` | ✅ | **破坏保护订这一个** |
| `PLAYER_PLACING_BLOCK` | `PlayerPlacingBlockEvent` | ✅ | 前置 |
| `PLAYER_PLACED_BLOCK` | `PlayerPlacedBlockEvent` | ❌ | 后置 |
| `PLAYER_SNEAKING` | `PlayerSneakingEvent` | ✅ | 前置 |
| `PLAYER_SNEAKED` | `PlayerSneakedEvent` | ❌ | 后置 |

::: warning 没有 `PlayerDestroyingBlockEvent`
放置那一对是 `PlayerPlacingBlockEvent`（前置可取消）+ `PlayerPlacedBlockEvent`（后置），于是「破坏也该有 -ing 版本」看起来很自然。**破坏不按这个规律**：只有一个 `PlayerDestroyBlockEvent`，过去式的名字，但它就是那个可取消的前置事件。订它就够了，而且真的拦得住。
:::

### 玩家事件（桥接 hook 实现）

下面这些**不是 LeviLamina 总线上的事件**，是加载器自己在 C++ 侧挂钩子做出来的。用法完全一样。

| 常量 | 事件 id | 可取消 | 载荷 / 备注 |
| --- | --- | :---: | --- |
| `PLAYER_DROP_ITEM` | `PlayerDropItemEvent` | ✅ | `{x,y,z,dim,item,randomly,viaInventoryUi,_player}`。同时钩了 Q 键丢弃和背包界面拖出 |
| `PLAYER_START_DESTROY_BLOCK` | `PlayerStartDestroyBlockEvent` | ❌ | `{x,y,z,face,_player}`。**开始**挖时触发，比 `PLAYER_DESTROY_BLOCK` 早。同步派发且在原函数之前，所以在回调里切快捷栏，破坏逻辑读到的就是换好的工具 |
| `PLAYER_CHANGE_DIMENSION` | `PlayerChangeDimensionEvent` | ✅ | |
| `PLAYER_OPEN_CONTAINER` | `PlayerOpenContainerEvent` | ✅ | |
| `PLAYER_USE_ITEM_ON` | `PlayerUseItemOnEvent` | ✅ | 对方块用物品 |
| `PLAYER_INTERACT_ENTITY` | `PlayerInteractEntityEvent` | ✅ | |
| `PLAYER_RIDE` | `PlayerRideEvent` | ✅ | |
| `PLAYER_SPAWN_PROJECTILE` | `PlayerSpawnProjectileEvent` | ✅ | |
| `PLAYER_STEP_ON_PRESSURE_PLATE` | `PlayerStepOnPressurePlateEvent` | ✅ | |
| `PLAYER_PUSH_ENTITY` | `PlayerPushEntityEvent` | ✅ | 撞开实体。**唯一一种不需要点击的干扰方式**——做保护类模组时容易漏 |
| `PLAYER_CHANGE_GAME_MODE` | `PlayerChangeGameModeEvent` | ✅ | |

### 生物事件

| 常量 | 事件 id |
| --- | --- |
| `SPAWNING_MOB` | `SpawningMobEvent` |
| `SPAWNED_MOB` | `SpawnedMobEvent` |
| `MOB_HURT` | `MobHurtEvent` |
| `MOB_DIE` | `MobDieEvent` |
| `ACTOR_HURT` | `ActorHurtEvent` |
| `FIRE_SPREAD` | `FireSpreadEvent` |

### 服务端与命令事件

| 常量 | 事件 id | 可取消 | 备注 |
| --- | --- | :---: | --- |
| `EXECUTING_COMMAND` | `ExecutingCommandEvent` | ✅ | 载荷 `{name, command, _player}`，`command` 是原始输入行（带斜杠） |
| `EXECUTED_COMMAND` | `ExecutedCommandEvent` | ❌ | 已经执行完了 |
| `CONSOLE_OUTPUTTING` | `ConsoleOutputtingEvent` | ✅ | |
| `CONSOLE_OUTPUTTED` | `ConsoleOutputtedEvent` | ❌ | |
| `SERVER_STARTED` | `ServerStartedEvent` | ❌ | |
| `SERVER_STOPPING` | `ServerStoppingEvent` | ❌ | |
| `HOPPER_TRANSFER` | `HopperTransferEvent` | ❌ | 桥接 hook。载荷 `{x,y,z,slot,item,count,old_item,old_count}` |

::: warning `EXECUTING_COMMAND` 只报玩家发起的命令
控制台、命令方块和其他非玩家来源**完全不上报**。这是故意的：拦掉控制台会把服主锁在自己服务器外面，且没有恢复路径。
:::

::: warning `HOPPER_TRANSFER` 没有维度字段
`HopperBlockActor::setItem` 那一层拿不到 `BlockSource`。要区分维度就按自己注册时记下的坐标认。另外它是高频事件，回调里别做重活。
:::

## 事件名不在表里怎么办

表里是已核实的常量，不是全集。任何 LeviLamina 总线上的事件都能订，直接传字符串即可：

```rust
ctx.server().subscribe_event("PlayerAddExperienceEvent", EventPriority::Normal, |ev| {
    println!("{}", ev.snbt());
})?.forget();
```

服务器里输入 `/levirs events` 可以列出当前全部可订阅的事件 id。

不知道某个事件的载荷长什么样，就先订上打印 `ev.snbt()` 看一眼——这比翻头文件快。
