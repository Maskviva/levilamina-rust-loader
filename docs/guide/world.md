# 世界与玩家

## 从玩家到实体

最常见的第一个卡点：**位置、生命值、效果这些在 `Player` 上找不到**。

原生继承链是 `Player : Mob : Actor`，那些能力在 `Actor` 那一层，`Player` 没有重复一遍。转过去就有了：

```rust
use levilamina::prelude::*;

let actor = player.get_actor()?;

let (x, y, z)   = actor.pos()?;
let (pitch, yaw) = actor.rotation()?;
let hp          = actor.health()?;
let dim         = actor.dimension_id()?;

actor.add_effect("speed", 200, 1, false, true)?;
actor.add_tag("in_arena")?;
actor.teleport(100.0, 64.0, 200.0)?;
```

`Actor` 是 `Entity` 的类型别名——同一个类型两个名字。原生叫 Actor，本 crate 早期叫 Entity，现在两个都能用。

::: warning 别把 Actor 存过一个 tick
`Actor` 内部是 `ActorUniqueID`。玩家退出重进后这个 id 会变，旧句柄就指向一个不存在的实体了。

**该存的是 `Player`（按 XUID），用的时候现转。**
:::

## 方块

```rust
let b = Block::at(0, 100, 64, 200);

println!("{}", b.type_name()?);
b.set("minecraft:stone")?;
b.set_state("facing", "north")?;

if b.has_tag("minecraft:is_pickaxe_item_destructible")? { /* … */ }
```

## 一切写操作走命令

这是最实用的一个模式。

大范围改方块、生成结构、给效果、传送——**优先用 `execute_command`**：

```rust
ctx.server().execute_command("fill 0 60 0 100 60 100 minecraft:stone")?;
ctx.server().execute_command("clone 0 60 0 10 70 10 100 60 100")?;
ctx.server().execute_command("effect @a speed 30 1 true")?;
```

三个理由：

1. **快得多。** 引擎内部有批量路径，一百万格 `Block::set` 是一百万次 FFI，一条 `/fill` 是一次。
2. **行为一致。** 和管理员手打的效果完全相同，不会出现"API 改的方块没触发方块更新"这类问题。
3. **少写代码。** 相邻方块更新、光照重算、区块保存，引擎都替你做了。

需要精细控制、需要读返回值、或者要在事件回调里做单点判断，才用 API。

## 扫描区域

读大范围用 `scan_region`，一次拿回整块：

```rust
let scan = ctx.server().scan_region(0, (0, 60, 0), (15, 70, 15))?;

for layer in &scan.layers {
    for (dx, column) in layer.cells.iter().enumerate() {
        for (dz, cell) in column.iter().enumerate() {
            if cell.block.is_air() { continue; }
            let x = scan.min.0 + dx as i32;
            let z = scan.min.2 + dz as i32;
            println!("{x} {} {z} = {}", layer.y, cell.block.name);
        }
    }
}
```

索引是**相对最小角的偏移**，世界坐标 = `min + 偏移`。

::: warning 别一次扫太大
内存开销是 `x * y * z * (方块名 + SNBT)`。100×100×100 就是一百万个 `Cell`，每个带两个 `String`。分块扫，只扫真正需要的 Y 范围。
:::

## 实体

```rust
// 生成
let zombie = ctx.server().spawn_mob(0, "minecraft:zombie", 100.0, 64.0, 200.0)?;
zombie.set_name_tag("§c守卫")?;
zombie.add_tag("guard")?;

// 枚举
for e in Entity::list(Some(0)) {
    if e.type_name == "minecraft:item" {
        Entity::from_id(e.id).despawn()?;      // 清理掉落物
    }
}
```

`kill()` 走正常死亡流程（掉落物照掉、触发死亡事件），`despawn()` 直接移除。清理刷出来的东西用后者。

## 物品

`ItemStack` 是纯值对象，改它不影响游戏，交给容器或玩家才生效：

```rust
let mut item = ItemStack::create("minecraft:diamond_sword", 1);
item.set_custom_name("§b霜之哀伤")?;
item.set_lore(&["§7一把剑"])?;
player.give_item(&item)?;
```

容器统一接口：

```rust
let inv = player.inventory();
for (i, it) in inv.items()?.iter().enumerate() {
    if it.is_null()? { continue; }
    println!("{i}: {} x{}", it.type_name()?, it.count()?);
}

let chest = Container::block(0, 100, 64, 200);
chest.add_item(&item)?;
```

`items()` 一次拿回全部，比循环 `item(i)` 少很多次 FFI。

## 维度 id 不止 0/1/2

有 [自定义维度](/api/dimensions) 的服务器上，`dimension()` 会返回 >= 3 的值：

```rust
// ❌
match dim { 0 => "主世界", 1 => "下界", _ => "末地" }

// ✅
match dim {
    0 => "主世界".into(), 1 => "下界".into(), 2 => "末地".into(),
    n => format!("自定义 {n}"),
}
```

## 想读的东西这页没有

先试 `snapshot()`：

```rust
let nbt = actor.snapshot()?;                        // 完整 Actor::save
let v = nbt.path("Attributes.0.Current").and_then(|x| x.as_f64());
```

方块同理有 `to_nbt()`，物品有 `to_nbt()`。这些是逃生舱口——SDK 没单独封装的字段基本都能从这里挖出来。
