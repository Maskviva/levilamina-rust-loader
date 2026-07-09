# Event — 事件

> 状态：✅ **已支持**。订阅走 `Server::subscribe_event`，回调收到 `EventRef`。事件数据以 **SNBT** 提供（`snbt()`），也可解析为结构化的 [NbtValue](/api/nbt)（`value()`）；可改写（`set_snbt` / `set_value`）、可取消（`cancel`）。玩家类事件与命令事件的回调还能直接取出玩家身份（`player()` / `player_handle()`）。
>
> 按事件类型给出**逐事件强类型访问器**（如 `chat.message() -> &mut String`、`block_changed.new_block() -> Block`）仍属 🧩 规划——见下方「字段访问」一节，那里按真实原生事件类核实了目标设计。

事件是模组感知游戏的主通道。订阅**仅限服务器线程**。`subscribe_event` 返回一个 `Listener`：丢弃它即自动退订（RAII），调用 `.forget()` 让其存活到模组卸载。回调收到 `&mut EventRef`，可读取数据、改写数据、或取消（对可取消事件）。

## 订阅 API

| API | 作用 |
| --- | --- |
| `Server::subscribe_event(id, priority, handler) -> Result<Listener>` | 按事件 id 订阅；`id` 支持**唯一后缀匹配**（写 `"PlayerChatEvent"` 即可，不必带命名空间全名）。id 未知或后缀有歧义时返回 `Err` |
| `Server::list_events() -> Vec<String>` | 列出当前已注册的全部事件 id（含其他模组发布的事件）。等价于进游戏执行 `/levirs events` |

```rust
use levilamina::prelude::*;

fn on_enable(&mut self, ctx: &ModContext) -> Result<()> {
    let logger = ctx.logger();
    ctx.server()
        .subscribe_event("PlayerChatEvent", EventPriority::Normal, move |ev| {
            logger.info(&format!("{}: {}", ev.id(), ev.snbt()));
        })?
        .forget();
    Ok(())
}
```

> 没有 `Event::player_join(..)` 这类「每个事件一个具名方法」的封装层——**所有事件都通过同一个 `subscribe_event` 按 id 订阅**。下表的事件名只是常见 id 的清单，不是独立的 API。稳定书写这些 id 可用 `levilamina::event::names` 模块里的常量（如 `names::PLAYER_CHAT`），避免手写字符串拼错。

## `EventRef` —— 回调参数

回调收到 `&mut EventRef`，方法如下：

| 方法 | 返回 | 说明 |
| --- | --- | --- |
| `id()` | `&str` | 实际匹配到的完整事件 id（如 `ll::event::PlayerChatEvent`） |
| `snbt()` | `&str` | 事件全部字段的 SNBT 文本（NBT 的字符串表示，形似 JSON） |
| `value()` | `Result<NbtValue>` | 把事件数据解析成结构化的 [NbtValue](/api/nbt)；已包含尚未提交的改写，故 `value → 改 → set_value` 可链式组合 |
| `player()` | `Option<PlayerIdentity>` | 若桥接为该事件附加了 `_player` 身份块，取出 `{name, xuid, uuid}` |
| `player_handle()` | `Option<Player>` | 在 `player()` 基础上解析成可调用的 [Player](/api/player) 句柄（优先按 xuid，退回按名字） |
| `set_snbt(s)` | — | 用改写后的 SNBT **整体覆盖**事件数据，桥接会反序列化回事件 |
| `set_value(&v)` | — | 结构化写回：序列化 `NbtValue` 后作为新数据暂存 |
| `cancel()` | — | 取消可取消事件（结构化置 `cancelled = 1b`；解析失败时退回文本替换 `cancelled:0b → 1b`） |

> **改写何时真正生效**：只有原生实现了 `deserialize(CompoundTag const&)` 的事件（带**可变字段**者，如 `PlayerChatEvent` 的消息、`ActorHurtEvent` 的伤害）写回才会作用到引擎。桥接对所有事件统一尝试写回，实际是否生效取决于该事件原生是否支持——`set_snbt` / `set_value` / `cancel` 对不支持的事件是无害的空操作。

### `player()` / `player_handle()` 从哪来

桥接在序列化事件时，如果事件的 CompoundTag 里嵌有一个**在线玩家**的指针，就会拼进一个 `_player` 块（`{name, xuid, uuid}`）。玩家类事件普遍带这个块；**命令事件**（`ExecutingCommandEvent` / `ExecutedCommandEvent`）由桥接显式补上执行者的 `_player` 与 `command` 字段。因此这两类事件里，直接 `ev.player_handle()` 就能拿到执行者句柄，`ev.value().get("command")` 能取到命令文本，不必自己从 origin 解析。

```rust
server.subscribe_event("ExecutedCommandEvent", EventPriority::Normal, move |ev| {
    // 控制台/面板发起的命令没有 _player，player() 返回 None，可据此跳过
    if let Some(who) = ev.player() {           // who: PlayerIdentity { name, xuid, uuid }
        let cmd = ev.value().ok()
            .and_then(|v| v.get("command").and_then(|c| c.as_str()).map(String::from))
            .unwrap_or_default();
        logger.info(&format!("{} 执行了 /{}", who.name, cmd));
        // 需要进一步操作该玩家时，用 ev.player_handle() 拿到 Player 句柄
    }
})?;
```

## 事件清单

以下是可直接用后缀名订阅的常见事件；「可取消」列指该事件在原生是否可取消（对不可取消事件调用 `cancel()` 是空操作）。完整列表以你服务器上的 `/levirs events` 输出为准。

### 玩家事件

| 事件 id | 触发时机 | 可取消 |
| --- | --- | :---: |
| `PlayerConnectEvent` | 玩家开始连接 | ✅ |
| `PlayerJoinEvent` | 玩家加入服务器 | ✅ |
| `PlayerDisconnectEvent` | 玩家断开连接 | — |
| `PlayerRespawnEvent` | 玩家重生 | — |
| `PlayerChatEvent` | 玩家发送聊天 | ✅ |
| `PlayerDieEvent` | 玩家死亡 | — |
| `PlayerAttackEvent` | 玩家攻击 | ✅ |
| `PlayerDestroyBlockEvent` | 玩家破坏方块 | ✅ |
| `PlayerPlacingBlockEvent` | 玩家放置方块（前置） | ✅ |
| `PlayerPlacedBlockEvent` | 玩家放置方块（后置） | — |
| `PlayerInteractBlockEvent` | 玩家与方块交互 | ✅ |
| `PlayerUseItemEvent` | 玩家使用物品 | ✅ |
| `PlayerPickUpItemEvent` | 玩家拾取物品 | ✅ |
| `PlayerJumpEvent` | 玩家跳跃 | — |
| `PlayerSneakingEvent` | 玩家开始潜行（前置） | ✅ |
| `PlayerSneakedEvent` | 玩家结束潜行（后置） | ✅ |
| `PlayerSprintingEvent` | 玩家开始疾跑 | — |
| `PlayerSprintedEvent` | 玩家结束疾跑 | — |
| `PlayerSwingEvent` | 玩家挥手 | — |
| `PlayerAddExperienceEvent` | 玩家获得经验 | ✅ |
| `PlayerChangePermEvent` | 玩家权限变更 | ✅ |

> **前置/后置成对事件**：原生把「正在发生（可取消）」与「已经发生（不可取消）」拆成两个类（如 `PlayerPlacingBlockEvent` / `PlayerPlacedBlockEvent`），本表按真实类名一一列出，不合并。`PlayerClickEvent` / `PlayerRightClickEvent` / `PlayerLeftClickEvent` 是**抽象分类基类**（攻击、放置、交互等由它们派生），不独立派发，故不在清单内。潜行的 `PlayerSneakingEvent` / `PlayerSneakedEvent` 类型上均继承自可取消基类 `PlayerSneakEvent`；取消「已结束潜行」的后置事件是否有效取决于引擎，不要依赖。

### 实体事件

| 事件 id | 触发时机 | 可取消 |
| --- | --- | :---: |
| `ActorHurtEvent` | 实体受伤 | ✅ |
| `MobHurtEvent` | 生物受伤 | ✅ |
| `MobDieEvent` | 生物死亡 | — |
| `SpawningMobEvent` | 生物即将生成（前置） | ✅ |
| `SpawnedMobEvent` | 生物已生成（后置） | — |

### 世界事件

| 事件 id | 触发时机 | 可取消 |
| --- | --- | :---: |
| `BlockChangedEvent` | 方块变化 | — |
| `FireSpreadEvent` | 火焰蔓延 | ✅ |
| `ServerLevelTickEvent` | 每个存档 tick | — |

### 命令事件

| 事件 id | 触发时机 | 可取消 |
| --- | --- | :---: |
| `ExecutingCommandEvent` | 命令执行前 | ✅ |
| `ExecutedCommandEvent` | 命令执行后 | — |

> 命令事件的回调载荷由桥接构造，含 `command`（命令文本）与执行者的 `_player` 块；控制台/面板发起的命令没有 `_player`（`player_handle()` 返回 `None`），据此可只记录玩家指令。这两个事件在原生只派发给 typed listener、且类型位于内联命名空间 `ll::event::inline command` 下，桥接已处理好 id 匹配，你直接用后缀名 `"ExecutedCommandEvent"` 订阅即可。

### 服务器与控制台事件

| 事件 id | 触发时机 | 可取消 |
| --- | --- | :---: |
| `ServerStartedEvent` | 服务器启动完成 | — |
| `ServerStoppingEvent` | 服务器开始停止 | — |
| `ConsoleOutputtingEvent` | 控制台即将输出（前置） | ✅ |
| `ConsoleOutputtedEvent` | 控制台已输出（后置） | — |

> `ServerStartedEvent` 有个时序细节：若模组在服务器**已启动之后**才加载，这个事件可能已经派发过、你订阅不到。需要「服务器已在运行」这一状态时，配合轮询 `Server::gaming_status()` 作兜底更稳妥。

## 事件 id 常量

`levilamina::event::names` 模块提供了核实过的 id 常量，用它们代替手写字符串可避免拼写错误，也不受上游类名改动影响：

```rust
use levilamina::event::names;

server.subscribe_event(names::PLAYER_CHAT, EventPriority::Normal, |ev| { /* … */ })?;
```

常量覆盖上表各事件（如 `PLAYER_CHAT`、`PLAYER_JOIN`、`ACTOR_HURT`、`EXECUTING_COMMAND`、`EXECUTED_COMMAND`、`SERVER_STARTED`、`SERVER_STOPPING` 等）。

## 字段访问（进阶设计，🧩 规划）

> **接口来源**：以下按真实的原生事件类核实（`ll/api/event/` 下各事件头文件），说明具名事件的载荷本来带有哪些具体字段、哪些字段真正可写。**当前**已可通过 `value()` 拿到这些字段的结构化 `NbtValue`；下面描述的是把它们进一步包成**逐事件强类型访问器**的目标。

原生事件不是无结构的大 SNBT 袋子——每个事件类都有自己的具名字段访问器，且都能追溯到一个共同的「给我这个事件的主体」入口：

| 事件所属家族 | 取主体的访问器 | 返回类型 |
| --- | --- | --- |
| 玩家事件（`PlayerEvent` 及其派生） | `.self()` | `Player&`（`ServerPlayerEvent` 派生的进一步细化为 `ServerPlayer&`） |
| 实体事件（`ActorEvent` 及其派生） | `.self()` | `Actor&` |
| 世界事件（`WorldEvent` 及其派生） | `.blockSource()` | `BlockSource&`（该维度的方块访问入口） |

在此之上，具体事件再各自附加自己的字段，例如：

| 事件 | 附加字段访问器 | 返回类型 | 可写？ |
| --- | --- | --- | --- |
| `PlayerChatEvent` | `.message()` | `std::string&` | ✅ 可改写聊天内容 |
| `ActorHurtEvent` | `.source()` / `.damage()` | `ActorDamageSource const&` / `float&` | 伤害数值 ✅ 可改 |
| `PlayerDestroyBlockEvent` | `.pos()` | `BlockPos const&` | 只读 |
| `PlayerPlacingBlockEvent`（放置前，可取消） | `.pos()` / `.face()` | `BlockPos const&` / `uchar const&` | 只读 |
| `PlayerPlacedBlockEvent`（放置后） | `.pos()` / `.placedBlock()` | `BlockPos const&` / `Block const&` | 只读 |
| `BlockChangedEvent` | `.layer()` / `.previousBlock()` / `.newBlock()` / `.pos()` | `uint const&` / `Block const&` ×2 / `BlockPos const&` | 只读 |

目标设计是让 Rust 侧的 `EventRef` 按事件具体类型给出对应的强类型访问器——如 `chat_event.message()` 返回可改写的 `&mut String`、`block_changed_event.new_block()` 直接给出 [Block](/api/block) 句柄——省去手工解析 SNBT 字段名。这仰赖两件已打好基础的事：[Nbt](/api/nbt) 层的结构化解析（`value()` 已用它），以及 [Player](/api/player)/[Entity](/api/entity)/[Block](/api/block) 页已确定的句柄方法面（`player_handle()` 已用它）。

## 相关类型

| 类型 | 说明 |
| --- | --- |
| `EventPriority` | 优先级：`Highest` / `High` / `Normal` / `Low` / `Lowest`，决定多监听器间的调用顺序 |
| `Listener` | 订阅句柄；丢弃即退订，`.forget()` 使其常驻到模组卸载 |
| `EventRef`（回调参数） | 事件数据引用：`id()`、`snbt()`、`value()`、`player()`、`player_handle()`、`set_snbt()`、`set_value()`、`cancel()` |
| `PlayerIdentity` | `player()` 的返回：`{ name, xuid, uuid }` 三个 `String` 字段 |
