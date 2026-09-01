# Player — 玩家对象

`Player` 是一个**选择器句柄**：内部只存一个名字 / XUID / UUID，每次调用方法时才去当前在线玩家里现查。查不到就返回 `Err`，不会有悬垂指针。

克隆很便宜，可以随便存进结构体、`HashMap`、闭包里。

## 拿到一个 Player

```rust
use levilamina::prelude::*;

// 按名字（精确匹配 getRealName，回退到显示名）
let p = Player::by_name("Steve");
let p = Player::get("Steve");        // by_name 的别名

// 按 XUID / UUID —— 长期存储优先用这两个，名字会变
let p = Player::by_xuid("2535400000000000");
let p = Player::by_uuid("00000000-0000-0000-0000-000000000000");

// 从事件回调里
ctx.server().subscribe_event(names::PLAYER_CHAT, EventPriority::Normal, |ev| {
    if let Some(p) = ev.player_handle() { /* … */ }
})?.forget();
```

| API | 说明 |
| --- | --- |
| `Player::by_name(name)` | 按账号名 |
| `Player::get(name)` | `by_name` 的别名 |
| `Player::by_xuid(xuid)` | 按 XUID |
| `Player::by_uuid(uuid)` | 按 UUID |
| `Player::list() -> Vec<PlayerInfo>` | 枚举全部在线玩家 |
| `Player::broadcast(msg)` | 给所有在线玩家发一条消息 |
| `player.is_online() -> bool` | 选择器现在能不能解析到人 |

`PlayerInfo` 的字段：

```rust
pub struct PlayerInfo {
    pub name: String,
    pub xuid: String,
    pub uuid: String,
    pub dimension: i32,
    pub pos: (f64, f64, f64),
}
```

## 转成 Actor —— 大部分能力在那一层

原生的继承链是 `Player : Mob : Actor`。**位置、朝向、生命值、药水效果、Tag、骑乘、AABB、射线检测这些全在 `Actor` 上**，`Player` 没有重复一遍。

```rust
let actor = player.get_actor()?;

let (x, y, z) = actor.pos()?;
let hp = actor.health()?;
actor.add_tag("in_arena")?;
actor.add_effect("speed", 200, 1, false, true)?;
```

| API | 说明 |
| --- | --- |
| `player.get_actor() -> Result<Actor>` | 拿到实体句柄。**日常用这个** |
| `player.as_entity() -> Result<Entity>` | 完全等价，老名字 |

::: tip Actor 和 Entity 是同一个类型
`Actor` 是 `Entity` 的类型别名。原生 C++ 那边这个类叫 `Actor`，LSE 文档也叫 Actor，但本 crate 早期用的是 `Entity`——现在两个名字都在，指向同一个东西，挑顺手的用。完整方法表见 [Actor / Entity](/api/actor/entity)。
:::

::: warning 别缓存 Actor
`Actor` 内部是 `ActorUniqueID`。玩家退出重进之后这个 id 会变，旧的 `Actor` 就指向一个不存在的实体了。**跨 tick 要存的是 `Player`，用的时候现转。**
:::

## 身份与网络

| API | 返回 | 原生对应 |
| --- | --- | --- |
| `player.real_name()` | `Result<String>` | `Player::getRealName` |
| `player.name_tag()` | `Result<String>` | `Actor::getNameTag`（显示名，会被称号插件改） |
| `player.uuid()` | `Result<String>` | `Player::getUuid` |
| `player.xuid()` | `Result<String>` | `Player::getXuid` |
| `player.ip_and_port()` | `Result<String>` | `Player::getIPAndPort` |
| `player.locale_code()` | `Result<String>` | `Player::getLocaleCode`，如 `"zh_CN"` |
| `player.client_sub_id()` | `Result<i32>` | 分屏 / 多用户的子客户端 id |
| `player.network_status()` | `Result<String>` | 网络状态 SNBT：`{ping, avg_ping, packet_loss, avg_packet_loss, max_bps}` |
| `player.is_operator()` | `Result<bool>` | 是否 OP |
| `player.can_use_operator_blocks()` | `Result<bool>` | 能否用命令方块等 OP 专属方块 |

## 消息与连接

| API | 说明 |
| --- | --- |
| `player.send_message(msg)` | 发一条普通聊天消息 |
| `player.tell(msg, kind)` | 指定消息类型发送，`kind` 见下表 |
| `player.disconnect(reason)` | 按给定理由踢下线 |
| `player.send_packet(packet_id, body)` | **原始发包**：按 id 反序列化包体后只发给这个玩家。逃生舱口，详见 [Packet](/api/infra/packet) |

`MessageType` 各档（数值对齐引擎的 `TextPacketType`）：

| 变体 | 值 | 效果 |
| --- | :---: | --- |
| `Raw` | 0 | 原样一行 |
| `Chat` | 1 | 聊天 |
| `Translate` | 2 | 翻译键 |
| `Popup` | 3 | 屏幕中上弹出 |
| `JukeboxPopup` | 4 | 唱片机那种提示 |
| `Tip` | 5 | 屏幕中下小字 |
| `SystemMessage` | 6 | 系统消息 |
| `Whisper` | 7 | 悄悄话 |
| `Announcement` | 8 | 公告 |
| `TextObjectWhisper` / `TextObject` / `TextObjectAnnouncement` | 9 / 10 / 11 | 富文本三档 |

> 带作者 / 参数的那几档（`Chat`、`Whisper`、`Translate`、`TextObject*`）在这里都按普通一行发出去——和 LSE 的 `tell(msg, type)` 是同一种简化。想真正用富文本，走 [`send_packet`](/api/infra/packet)。

## 标题栏

```rust
use levilamina::prelude::*;

player.send_title(TitleKind::Title, "§c警告", Some(TitleTimes::new(10, 60, 10)))?;
player.set_subtitle("你正在进入 PVP 区域")?;
player.set_actionbar("剩余 30 秒")?;
```

| API | 说明 |
| --- | --- |
| `player.send_title(kind, text, times)` | 通用入口。`times` 传 `None` 沿用客户端上次的时间 |
| `player.set_title(text)` | 等价于 `send_title(Title, text, None)` |
| `player.set_subtitle(text)` | 副标题 |
| `player.set_actionbar(text)` | 快捷栏上方那行 |
| `player.set_title_times(times)` | 只改时间不改内容 |
| `player.clear_title()` | 清掉当前显示 |

`TitleKind`：`Clear=0`、`Reset=1`、`Title=2`、`Subtitle=3`、`Actionbar=4`、`Times=5`。
`Clear` 只隐藏、保留时间设置；`Reset` 连时间也恢复成客户端默认。

`TitleTimes { fade_in, stay, fade_out }` 单位是**刻**（20 刻 = 1 秒），`default()` 是原版的 0.5s / 3s / 0.5s。

::: tip times 传 None 的实际后果
客户端存的是**上一次**被设置的时间，而任何命令方块、插件、数据包的 `/title … times` 都会全局改掉它。也就是说 `None` 的表现取决于服务器上别人干了什么。要稳定表现就显式传值。
:::

## 能力 Ability

```rust
player.set_ability(Ability::MayFly, true)?;
player.set_ability(Ability::FlySpeed, 0.2)?;      // 浮点槽
let can_build = player.can_use_ability(Ability::Build)?;
```

| API | 说明 |
| --- | --- |
| `player.set_ability(ability, value)` | 布尔槽传 `bool`，浮点槽传 `f64` / `f32` |
| `player.set_ability_raw(index, value)` | 直接传裸索引，用于本表没覆盖到的槽 |
| `player.can_use_ability(ability)` | 是否拥有某项能力 |
| `player.permission_level()` | `Result<PlayerPermission>`，玩家权限等级 |
| `player.set_permission_level(level)` | 设置玩家权限等级 |

::: warning 能力位和玩家权限等级是两件事
`PlayerPermission`（`Visitor=0` / `Member=1` / `Operator=2` / `Custom=3`）和
`Ability` 一起装在 `UpdateAbilitiesPacket` 里，但它是**独立的字段**，而且客户端
先看它：等级是 `Visitor` 时，普通方块**不画描边**、打不了人、放置不做本地预测，
可交互方块（中继器、容器）的描边还在。服务端一侧完全不受影响，所以指令照跑、
方块照放，只是全部变成服务端驱动的。

引擎的 `LayeredAbilities::setAbility` 本身就是「切成自定义权限」那条路，会把玩家
推到 `Custom`。**loader 已经在 `set_ability` 里把等级还原回去了**，所以正常用不会
踩到；要真的改等级请显式调 `set_permission_level`。

它还会被写进玩家存档（`PermissionsHandler::addSaveData`），重连不会自己恢复。
:::

`Ability` 的全部变体（已对 BDS 1.26.20 的 `AbilitiesIndex.h` 核实，枚举跑 0..=19）：

| 变体 | 索引 | 值类型 | 含义 |
| --- | :---: | :---: | --- |
| `Build` | 0 | bool | 放置方块 |
| `Mine` | 1 | bool | 挖掘 |
| `DoorsAndSwitches` | 2 | bool | 门与开关 |
| `OpenContainers` | 3 | bool | 打开容器 |
| `AttackPlayers` | 4 | bool | 攻击玩家 |
| `AttackMobs` | 5 | bool | 攻击生物 |
| `Operator` | 6 | bool | 管理员 |
| `Teleport` | 7 | bool | 传送 |
| `Invulnerable` | 8 | bool | 无敌 |
| `Flying` | 9 | bool | 当前是否在飞 |
| `MayFly` | 10 | bool | 允许飞行 |
| `Instabuild` | 11 | bool | 瞬间破坏 |
| `Lightning` | 12 | bool | 召唤闪电 |
| `FlySpeed` | 13 | **f64** | 飞行速度（默认 0.05） |
| `WalkSpeed` | 14 | **f64** | 行走速度（默认 0.1） |
| `Muted` | 15 | bool | 禁言 |
| `WorldBuilder` | 16 | bool | 世界编辑 |
| `NoClip` | 17 | bool | 穿墙 |
| `PrivilegedBuilder` | 18 | bool | 特权建造 |
| `VerticalFlySpeed` | 19 | **f64** | 垂直飞行速度 |

::: warning 旧文档这张表是错的
上一版把 `AttackMobs` 写成 4、`AttackPlayers` 写成 5（实际相反），`VerticalFlySpeed` 写成 15（实际是 19），还漏了 `Muted` / `NoClip` / `PrivilegedBuilder`。照旧表写会**静默**设错槽——FFI 边界上是个裸 `f64`，没有类型检查，不会报错，只是不生效。

判断某个槽是不是浮点用 `Ability::is_float()`，不要靠索引范围猜。
:::

## 游戏模式

| API | 说明 |
| --- | --- |
| `player.game_type() -> Result<i32>` | 原始 `GameType` 值 |
| `player.set_gamemode(mode)` | `GameMode::Survival` / `Creative` / `Adventure` / `Spectator` |

`GameMode` 取值：`Survival=0`、`Creative=1`、`Adventure=2`、`Spectator=6`。

> 原生的直接设置器是内部方法（`_setPlayerGameType`），桥接改走 `/gamemode` 命令实现，所以行为和管理员手打命令一致。

## 位置与传送

| API | 说明 |
| --- | --- |
| `player.dimension() -> Result<i32>` | 当前维度 id |
| `player.teleport(dimension, x, y, z)` | 跨维度传送 |
| `player.set_spawn_point(dimension, x, y, z)` | 设置重生点 |
| `player.has_respawn_position() -> Result<bool>` | 有没有设过重生点 |

::: warning 维度 id 不止 0/1/2
原版是 0（主世界）、1（下界）、2（末地），但通过 [自定义维度](/api/world/dimensions) 注册的维度 id 从 3 开始。**不要假设 `dimension()` 只会返回那三个值。**
:::

要拿具体坐标走 Actor：`player.get_actor()?.pos()?`。

## 属性：等级 / 经验 / 饥饿

| API | 说明 |
| --- | --- |
| `player.level()` / `set_level(n)` | 经验等级 |
| `player.experience()` / `set_experience(v)` | 到下一级的进度，`0.0..=1.0` |
| `player.xp_needed_for_next_level()` | 升到下一级还需多少经验 |
| `player.hunger()` / `set_hunger(v)` | 饥饿值 |
| `player.saturation()` / `set_saturation(v)` | 饱和度 |
| `player.exhaustion()` / `set_exhaustion(v)` | 疲劳值 |
| `player.luck()` | 幸运值 |

## 物品栏

| API | 返回 | 说明 |
| --- | --- | --- |
| `player.inventory()` | `Container` | 主物品栏 |
| `player.ender_chest()` | `Container` | 末影箱 |
| `player.armor()` | `Container` | 盔甲栏 |
| `player.hands()` | `Container` | 主副手 |
| `player.offhand()` | `Result<ItemStack>` | 副手物品 |
| `player.set_offhand(item)` | `Result<()>` | 设置副手 |
| `player.give_item(item)` | `Result<()>` | 给物品并刷新客户端 |
| `player.selected_slot()` | `Result<i32>` | 当前快捷栏槽位 |
| `player.set_selected_slot(slot)` | `Result<()>` | 切换快捷栏槽位 |

容器的详细读写见 [Container](/api/world/container)。

### 直接按槽位读写（SNBT）

绕过 `Container`，直接对着槽位号操作，返回的是 SNBT 字符串：

| API | 说明 |
| --- | --- |
| `player.carried_item() -> Result<String>` | 主手物品 SNBT |
| `player.get_item(slot) -> Result<String>` | 读第 `slot` 格（0 起） |
| `player.set_item(slot, item_snbt)` | 写第 `slot` 格 |
| `player.equipment() -> Result<String>` | 全部装备：`[{slot, item_snbt}, …]`，slot 0=主手 1=副手 2-5=盔甲 |

### 物品冷却

| API | 说明 |
| --- | --- |
| `player.item_cooldown(item_name) -> i32` | 剩余冷却刻数；不在冷却或玩家离线返回 `-1` |
| `player.start_item_cooldown(item_name, ticks)` | 开始一段冷却 |

## 状态查询

全部返回 `Result<bool>`：

| API | 含义 |
| --- | --- |
| `player.is_flying()` | 正在飞 |
| `player.can_jump()` | 能跳 |
| `player.can_sleep()` | 能睡觉 |
| `player.is_emoting()` | 正在做表情 |
| `player.is_in_raid()` | 处于袭击中 |
| `player.is_hurt()` | 正在受伤状态 |
| `player.is_scoping()` | 正在拉弓瞄准 |

更多状态（潜行、着火、在水里、在岩浆里…）在 [Actor](/api/actor/entity) 那一层。

## 完整方法索引

<details>
<summary>点开看全部 45 个方法</summary>

**构造 / 静态**
`by_name` `get` `by_xuid` `by_uuid` `list` `broadcast`

**查询**
`is_online` `as_entity` `get_actor` `real_name` `uuid` `xuid` `ip_and_port` `locale_code` `name_tag` `game_type` `dimension` `level` `experience` `hunger` `saturation` `exhaustion` `xp_needed_for_next_level` `luck` `selected_slot` `is_operator` `permission_level` `can_use_operator_blocks` `is_flying` `can_jump` `is_emoting` `is_in_raid` `is_hurt` `is_scoping` `can_sleep` `has_respawn_position` `client_sub_id`

**动作**
`set_level` `set_experience` `set_hunger` `set_saturation` `set_exhaustion` `send_message` `tell` `send_packet` `disconnect` `set_gamemode` `teleport` `set_ability` `set_ability_raw` `set_permission_level` `can_use_ability` `set_selected_slot` `give_item` `set_spawn_point` `send_title` `set_title_times` `clear_title` `set_title` `set_subtitle` `set_actionbar`

**物品栏**
`inventory` `ender_chest` `armor` `hands` `offhand` `set_offhand`

**槽位 / 冷却 / 网络**
`carried_item` `get_item` `set_item` `equipment` `item_cooldown` `start_item_cooldown` `network_status`

</details>
