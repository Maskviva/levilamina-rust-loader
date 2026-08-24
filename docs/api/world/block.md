# Block — 方块对象

`Block` 是 `(维度, 坐标)` 的组合，**每次调用都重新读世界**——所以它拿到的永远是那个位置当前的方块，不是创建句柄那一刻的快照。

```rust
use levilamina::prelude::*;

let b = Block::at(0, 100, 64, 200);
println!("{}", b.type_name()?);
b.set("minecraft:stone")?;
```

| API | 说明 |
| --- | --- |
| `Block::at(dimension, x, y, z)` | 构造 |
| `b.position() -> (i32, i32, i32)` | 坐标 |
| `b.dimension() -> i32` | 维度 |

## 读

| API | 返回 | 说明 |
| --- | --- | --- |
| `b.type_name()` | `Result<String>` | 如 `minecraft:stone` |
| `b.is_air()` | `Result<bool>` | 是不是空气 |
| `b.snbt()` | `Result<String>` | 完整序列化 |
| `b.to_nbt()` | `Result<NbtValue>` | 同上，结构化 |
| `b.description_id()` | `Result<String>` | 描述 id |
| `b.debug_string()` | `Result<String>` | 引擎的调试字符串 |
| `b.data()` | `Result<i32>` | 旧版 data 值 |
| `b.block_item_id()` | `Result<i32>` | 对应物品 id |
| `b.collision_shape()` | `Result<String>` | 碰撞形状 |

## 写

```rust
b.set("minecraft:oak_stairs")?;
```

`set` 接受方块规格字符串。带方块状态的写法交给 `set_state`。

## 方块状态

```rust
let facing = b.get_state("facing")?;
b.set_state("facing", "north")?;
```

楼梯朝向、栅栏连接、红石中继器档位这类东西都在这里。

## 标签

| API | 说明 |
| --- | --- |
| `b.tags() -> Result<Vec<String>>` | 全部标签 |
| `b.has_tag(tag) -> Result<bool>` | 有没有某个标签 |

判断"是不是木头 / 是不是矿物"用标签比枚举方块名可靠得多。

## 方块实体

箱子、告示牌、熔炉这类带数据的方块：

| API | 返回 | 说明 |
| --- | --- | --- |
| `b.has_block_entity()` | `Result<bool>` | 有没有方块实体 |
| `b.block_entity()` | `Result<Option<NbtValue>>` | 方块实体的 NBT |
| `b.is_crafting_block()` | `Result<bool>` | 工作台类 |
| `b.is_interactive_block()` | `Result<bool>` | 可交互 |

箱子里的物品用 [`Container::block(dim, x, y, z)`](/api/world/container) 更方便。

## 批量操作

一格一格地 `Block::at` 在大范围下很慢——每次都是一次 FFI + 一次世界查询。

- **读**大范围：用 [`Server::scan_region`](/api/world/world)，一次拿回整块。
- **写**大范围：用 `/fill`、`/clone` 这类原版命令（`Server::execute_command`），引擎内部的批量路径比逐格调用快一个数量级。
