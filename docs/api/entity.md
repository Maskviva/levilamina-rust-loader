# Actor / Entity — 实体对象

`Actor` 和 `Entity` 是**同一个类型的两个名字**：

```rust
pub type Actor = Entity;
```

原生 C++ 那边这个类叫 `Actor`（继承链 `Player : Mob : Actor`），LSE 和大部分基岩版插件文档也跟着叫 Actor；本 crate 早期选了 `Entity`。两个名字都在 `prelude` 里，随便用。

内部存的是一个 `ActorUniqueID`（i64），每次调用都通过 `Level::fetchEntity` 现查，所以不可能悬垂——实体没了就返回 `Err`。

## 拿到一个 Actor

```rust
use levilamina::prelude::*;

// 从玩家转过来（最常见）
let actor = player.get_actor()?;

// 生成一只
let actor = ctx.server().spawn_mob(0, "minecraft:zombie", 100.0, 64.0, 200.0)?;

// 枚举
for e in Entity::list(Some(0)) {          // None = 所有维度
    println!("{} {}", e.id, e.type_name);
}

// 已知 id
let actor = Actor::from_id(-1234567890);
```

| API | 说明 |
| --- | --- |
| `Entity::from_id(id)` | 包装一个已知的 ActorUniqueID |
| `Entity::list(dimension)` | 枚举实体。`Some(dim)` 限定维度，`None` 全部 |
| `actor.id() -> i64` | 取出裸 id |
| `actor.exists() -> bool` | 这个 id 现在还指向活的实体吗 |

`Entity::list` 返回 `Vec<EntityId>`：

```rust
pub struct EntityId {
    pub id: i64,
    pub type_name: String,
}
```

::: warning 别把 Actor 当长期存储
`ActorUniqueID` 在实体被卸载 / 玩家重进之后会变。要跨 tick 保存玩家，存 `Player`；要保存生物，每次用之前 `exists()` 确认一下。
:::

## 位置与朝向

| API | 返回 | 说明 |
| --- | --- | --- |
| `actor.pos()` | `Result<(f64, f64, f64)>` | 世界坐标 |
| `actor.rotation()` | `Result<(f64, f64)>` | `(pitch, yaw)`，度 |
| `actor.dimension_id()` | `Result<i32>` | 所在维度 |
| `actor.teleport(x, y, z)` | `Result<()>` | 同维度传送 |
| `actor.teleport_to_dimension(dim, x, y, z)` | `Result<()>` | 跨维度传送 |
| `actor.distance_to(other)` | `Result<f64>` | 到另一个实体的距离 |
| `actor.aabb()` | `Result<String>` | 碰撞箱 SNBT |

## 生命值与伤害

| API | 返回 | 说明 |
| --- | --- | --- |
| `actor.health()` | `Result<i32>` | 当前生命值 |
| `actor.max_health()` | `Result<i32>` | 最大生命值 |
| `actor.is_alive()` | `Result<bool>` | 活着吗 |
| `actor.heal(amount)` | `Result<()>` | 治疗 |
| `actor.hurt(amount)` | `Result<()>` | 造成伤害 |
| `actor.kill()` | `Result<()>` | 杀死（走正常死亡流程，掉落物照掉） |
| `actor.despawn()` | `Result<()>` | 直接移除（不死亡、不掉落） |
| `actor.set_on_fire(seconds)` | `Result<()>` | 点燃 |

> `kill()` 和 `despawn()` 的区别是会不会触发死亡事件和掉落。清理刷出来的实体用 `despawn()`。

## 药水效果

```rust
actor.add_effect("speed", 200, 1, false, true)?;
//               效果名   时长  等级  隐藏粒子 显示图标
actor.remove_effect("speed")?;
actor.clear_effects()?;
let snbt = actor.effects()?;   // 当前全部效果，SNBT
```

| API | 说明 |
| --- | --- |
| `actor.add_effect(effect, duration, amplifier, ambient, show_particles)` | 时长单位是刻 |
| `actor.remove_effect(effect)` | 移除一种 |
| `actor.clear_effects()` | 全清 |
| `actor.effects() -> Result<String>` | 读当前效果表（SNBT） |

## Tag

命令选择器 `@e[tag=xxx]` 用的就是这套，可以和原版命令联动。

| API | 返回 | 说明 |
| --- | --- | --- |
| `actor.add_tag(tag)` | `Result<bool>` | `true` = 这次确实加上了 |
| `actor.remove_tag(tag)` | `Result<bool>` | `true` = 确实移除了 |
| `actor.has_tag(tag)` | `Result<bool>` | 有没有 |

## 名称

| API | 说明 |
| --- | --- |
| `actor.type_name() -> Result<String>` | 实体类型，如 `minecraft:zombie` |
| `actor.name_tag() -> Result<String>` | 头顶显示名 |
| `actor.set_name_tag(name)` | 设置头顶显示名 |

## 状态查询

全部返回 `Result<bool>`：

| API | 含义 |
| --- | --- |
| `actor.is_on_ground()` | 站在地上 |
| `actor.is_in_water()` | 在水里 |
| `actor.is_in_lava()` | 在岩浆里 |
| `actor.is_on_fire()` | 着火 |
| `actor.is_invisible()` | 隐身 |
| `actor.is_sneaking()` | 潜行 |
| `actor.is_baby()` | 幼年体 |
| `actor.is_riding()` | 正骑着东西 |
| `actor.is_tame()` | 已驯服 |

还有 `actor.speed() -> Result<f64>` 拿移动速度。

### 原始状态位

| API | 说明 |
| --- | --- |
| `actor.status_flag(flag_index) -> Result<bool>` | 读一个原始 `ActorFlags` 位 |
| `actor.set_status_flag(flag_index, value)` | 写一个 |

上面那些 `is_*` 没覆盖到的状态用这个。索引值对照引擎的 `ActorFlags.h`。

## 关系：骑乘 / 主人 / 目标

| API | 返回 | 说明 |
| --- | --- | --- |
| `actor.vehicle()` | `Result<Entity>` | 我骑的东西 |
| `actor.first_passenger()` | `Result<Entity>` | 骑在我身上的第一个 |
| `actor.owner()` | `Result<Entity>` | 主人（狼、猫、弹射物的发射者） |
| `actor.target()` | `Result<Entity>` | 当前攻击目标 |

没有对应关系时返回 `Err`——这是"没有"，不是"出错"。

## 装备

| API | 说明 |
| --- | --- |
| `actor.equipped_item(slot) -> Result<String>` | 读一个装备槽（SNBT） |
| `actor.set_equipped_item(slot, item_snbt)` | 写一个装备槽 |

槽位：0=主手，1=副手，2..5=盔甲。

## 射线检测

```rust
let hit = actor.trace_ray(5.0, true, true)?;
//                        距离  查方块 查实体
```

返回命中信息的 SNBT。做"玩家看向的那个方块 / 那只怪"这类功能就用它，比自己步进采样准。

## 复制

```rust
let copy = actor.clone_at(0, 100.0, 64.0, 200.0)?;
```

在指定位置复制一份，返回新实体的句柄。

## 完整 NBT 快照

```rust
let nbt = actor.snapshot()?;             // NbtValue
let hp = nbt.path("Attributes.0.Current").and_then(|v| v.as_f64());
```

`snapshot()` 走的是原生 `Actor::save`，能拿到这份 API 没单独封装的一切字段。配合 [`NbtValue`](/api/nbt) 的 `path()` 取值。

**这是"逃生舱口"**：想读的东西这页没有，先试 `snapshot()`。

## 完整方法索引

<details>
<summary>点开看全部 48 个方法</summary>

**构造 / 静态**
`from_id` `list`

**基础**
`id` `exists` `snapshot` `type_name` `name_tag` `set_name_tag`

**位置**
`pos` `rotation` `dimension_id` `teleport` `teleport_to_dimension` `distance_to` `aabb` `trace_ray`

**生命值**
`health` `max_health` `is_alive` `heal` `hurt` `kill` `despawn` `set_on_fire` `speed`

**状态**
`is_on_ground` `is_in_water` `is_in_lava` `is_on_fire` `is_invisible` `is_sneaking` `is_baby` `is_riding` `is_tame` `status_flag` `set_status_flag`

**效果**
`add_effect` `remove_effect` `clear_effects` `effects`

**Tag**
`add_tag` `remove_tag` `has_tag`

**关系 / 装备 / 其他**
`vehicle` `first_passenger` `owner` `target` `equipped_item` `set_equipped_item` `clone_at`

</details>
