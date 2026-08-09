# Container — 容器对象

一套接口覆盖玩家物品栏、末影箱、盔甲栏和世界里的箱子。桥接每次调用时通过引擎的 `Container` 虚接口解析「归属者 + 哪一个容器」。

```rust
use levilamina::prelude::*;

// 玩家的
let inv = player.inventory();
let ec  = player.ender_chest();

// 世界里的箱子
let chest = Container::block(0, 100, 64, 200);
```

| 来源 | 写法 |
| --- | --- |
| 主物品栏 | `player.inventory()` |
| 末影箱 | `player.ender_chest()` |
| 盔甲栏 | `player.armor()` |
| 主副手 | `player.hands()` |
| 方块容器 | `Container::block(dim, x, y, z)` |

## 读

| API | 返回 | 说明 |
| --- | --- | --- |
| `c.size()` | `Result<i32>` | 格子数 |
| `c.item(slot)` | `Result<ItemStack>` | 读一格 |
| `c.items()` | `Result<Vec<ItemStack>>` | 读全部 |

## 写

| API | 返回 | 说明 |
| --- | --- | --- |
| `c.set_item(slot, &item)` | `Result<()>` | 覆盖一格 |
| `c.add_item(&item)` | `Result<bool>` | 塞进第一个能放的地方；`false` = 满了 |
| `c.remove_item(slot, count)` | `Result<()>` | 从某格拿走 n 个 |
| `c.clear()` | `Result<()>` | 清空 |
| `c.refresh()` | `Result<bool>` | 强制刷新客户端显示 |

::: tip 什么时候需要 refresh
大部分写操作会自动同步。批量改完一堆格子之后客户端偶尔会显示不同步，这时候补一次 `refresh()`。给玩家物品优先用 `player.give_item()`，它自带刷新。
:::

## 遍历

```rust
for (i, item) in inv.items()?.iter().enumerate() {
    if item.is_null()? { continue; }
    println!("{i}: {} x{}", item.type_name()?, item.count()?);
}
```

`items()` 一次拿回全部，比循环 `item(i)` 少很多次 FFI。

## 数数

```rust
let mut total = 0;
for item in inv.items()? {
    if item.type_name()? == "minecraft:diamond" {
        total += item.count()? as i32;
    }
}
```

没有内置的 `count_item`——不同模组对"算不算同一种物品"的判断不同（要不要比附魔、比自定义名），所以留给你自己定。需要严格比较用 `item.matches(&other)`。
