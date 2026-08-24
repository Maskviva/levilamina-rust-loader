# Objects — 其他类型

零散的值类型，以及一份全类型索引。

## 类型别名

```rust
pub type PositionI32 = (i32, i32, i32);
pub type PositionF64 = (f64, f64, f64);
pub type Actor = Entity;
pub type Result<T> = std::result::Result<T, Error>;
```

坐标就是元组，没有包装成结构体——省掉一层 `.x` `.y` `.z`，解构也方便：

```rust
let (x, y, z) = actor.pos()?;
```

## Error

```rust
pub struct Error(pub String);
```

一个字符串。可以从 `String` 和 `&str` 直接转，实现了 `std::error::Error`。

::: tip Err 通常表示「没找着」，不是「出错了」
`player.xxx()` 返回 `Err` 几乎总是因为玩家下线了；`actor.xxx()` 是因为实体没了；`block.xxx()` 是因为区块没加载。

所以下面这种写法是正常的，不是在吞错误：
```rust
if let Ok(hp) = actor.health() {
    // 实体还在
}
```
:::

## ModContext

生命周期钩子拿到的入口对象。没有自己的状态，复制很便宜。

| API | 返回 | 可用性 |
| --- | --- | --- |
| `ctx.logger()` | `Logger` | 两边 |
| `ctx.packets()` | `Packets` | 两边 |
| `ctx.server()` | `Server` | `server` 特性 |
| `ctx.client()` | `Client` | `client` 特性 |

## LeviMod

```rust
pub trait LeviMod: Sized + 'static {
    fn on_load(ctx: &ModContext) -> Result<Self>;
    fn on_enable(&mut self, ctx: &ModContext) -> Result<()> { Ok(()) }
    fn on_disable(&mut self, ctx: &ModContext) -> Result<()> { Ok(()) }
    fn on_unload(&mut self, ctx: &ModContext) -> Result<()> { Ok(()) }
}
```

除 `on_load` 外都有默认空实现。

所有钩子在服务器线程运行，模组实例也只被服务器线程碰，所以**可以放心持有 `!Send` 的东西**，比如 `Listener`。

配合 `register_mod!(MyMod)` 宏使用，一个模组写一次。

## RAII 句柄一览

四种回调句柄，行为完全一致——drop 就注销，`.forget()` 就活到模组卸载：

| 类型 | 来自 | 注销什么 |
| --- | --- | --- |
| `Listener` | `subscribe_event` | 事件订阅 |
| `Subscription` | `bus::subscribe` | 总线订阅 |
| `Registration` | `service::register` | 服务注册 |
| `PacketHook` | `packets().intercept` | 包拦截器 |

统一成一套习惯，比每个地方各有各的巧妙设计有用。

::: warning 忘了接返回值 = 立刻失效
```rust
ctx.server().subscribe_event(...)?;      // ❌ 当场 drop，订阅没了
ctx.server().subscribe_event(...)?.forget();   // ✅
```
编译器会给 `unused_must_use` 警告。
:::

`money::on_before` / `on_after` 返回的 guard 也是同样的用法，但语义特殊，见 [Money](/api/actor/money)。

## 枚举速查

| 枚举 | 取值 | 在哪 |
| --- | --- | --- |
| `EventPriority` | Highest=0 High=1 Normal=2 Low=3 Lowest=4 | [Event](/api/infra/event) |
| `LogLevel` | Fatal=0 Error=1 Warn=2 Info=3 Debug=4 Trace=5 | [Log](/api/rt/log) |
| `GameMode` | Survival=0 Creative=1 Adventure=2 Spectator=6 | [Player](/api/actor/player) |
| `MessageType` | Raw=0 … TextObjectAnnouncement=11 | [Player](/api/actor/player) |
| `TitleKind` | Clear=0 Reset=1 Title=2 Subtitle=3 Actionbar=4 Times=5 | [Player](/api/actor/player) |
| `Ability` | Build=0 … VerticalFlySpeed=19 | [Player](/api/actor/player) |
| `CommandPermission` | Any=0 GameDirectors=1 Admin=2 Host=3 Owner=4 | [Command](/api/actor/command) |
| `ParamType` | Int Bool Float String … FilePath（21 种） | [Command](/api/actor/command) |
| `SoftEnumOp` | Set=0 Add=1 Remove=2 | [Command](/api/actor/command) |
| `Weather` | Clear=0 Rain=1 Thunder=2 | [Server](/api/infra/server) |
| `GamingStatus` | Default Starting Running Stopping | [Server](/api/infra/server) |
| `DisplaySlot` | Sidebar List BelowName | [Scoreboard](/api/infra/scoreboard) |
| `FormValue` | Int Float Text Choice | [Gui](/api/actor/gui) |
| `FormResponse` | Cancelled Button Custom Modal | [Gui](/api/actor/gui) |
| `NbtValue` | Byte … LongArray（12 种） | [Nbt](/api/world/nbt) |
| `NbtBinaryFormat` | Disk=0 Network=1 | [Nbt](/api/world/nbt) |
| `Direction` | Inbound Outbound Both | [Packet](/api/infra/packet) |
| `Verdict` | Forward Drop | [Packet](/api/infra/packet) |
| `ConnectionState` | Opened Closed | [Packet](/api/infra/packet) |
| `CallError` | NotFound Provider Refused Unknown | [Service](/api/core/service) |
| `GeneratorType` | Overworld=1 Flat=2 Nether=3 TheEnd=4 Void=5 | [Dimensions](/api/world/dimensions) |
| `DimensionRule` | SpawnMonster=0 … EntityCrossPlot=12 | [Dimensions](/api/world/dimensions) |
| `KeyAction` | Up=0 Down=1 | [Client](/api/rt/client) |

## 值类型速查

| 结构体 | 字段 |
| --- | --- |
| `PlayerInfo` | `name` `xuid` `uuid` `dimension` `pos` |
| `PlayerIdentity` | `name` `xuid` `uuid` |
| `EntityId` | `id` `type_name` |
| `CommandResult` | `success` `output` |
| `CommandOrigin` | `name` `origin_type` `dimension` `position` |
| `Objective` | `name` `display_name` |
| `TitleTimes` | `fade_in` `stay` `fade_out` |
| `PlayerPos` | `x` `y` `z` `dim` |
| `BlockInfo` | `name` `snbt` |
| `EntityInfo` | `kind` `snbt` |
| `Cell` | `block` `entities` |
| `ScanLayer` | `y` `cells` |
| `Scan` | `min` `max` `layers` |
| `Bounds` | `min` `max` |
| `VillageInfo` | `uuid` `center` `bounds` `poi_count` |
| `StructureInfo` | `kind` `bounds` |
| `LocalTime` | `year` `month` `day` `hour` `minute` `second` `ms` |
| `Vetoable` | `refused` `delivered` |
| `ServiceInfo` | `name` `owner` |
| `MoneyEvent` | `kind` `from` `to` `amount` |
| `PlotLayout` | `plot_size` `road_width` `border_width` `floor_y` `floor_block` `fill_block` `road_block` `border_block` `biome` |
| `PlotMerge` | `x` `z` `mask` |
