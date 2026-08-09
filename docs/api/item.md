# Item — 物品对象

`ItemStack` 是**纯值对象**：内部就是一段 SNBT，不指向游戏里的任何东西。克隆它就是克隆那段文本；只有把它交给容器或玩家，游戏世界才会变。

所有查询和变换都往返一次引擎的 `ItemStack::fromTag` / `save`，所以最大堆叠数、附魔规则、耐久逻辑这些**永远来自引擎**，不是 Rust 这边重新实现的一套。

```rust
use levilamina::prelude::*;

let mut item = ItemStack::create("minecraft:diamond_sword", 1);
item.set_custom_name("§b霜之哀伤")?;
item.set_lore(&["§7一把剑", "§7仅此而已"])?;
player.give_item(&item)?;
```

## 构造

| API | 说明 |
| --- | --- |
| `ItemStack::create(type_name, count)` | 新建 |
| `ItemStack::from_snbt(snbt)` | 从已有 SNBT 包装（容器读取、事件载荷、实体快照的 `Item` 字段） |
| `ItemStack::empty()` | 空堆叠 |

## 读

| API | 返回 |
| --- | --- |
| `item.snbt()` | `&str` — 底层 SNBT |
| `item.to_nbt()` | `Result<NbtValue>` |
| `item.type_name()` | `Result<String>` — 如 `minecraft:apple` |
| `item.raw_name_id()` | `Result<String>` |
| `item.name()` | `Result<String>` — 显示名 |
| `item.custom_name()` | `Result<String>` — 自定义名 |
| `item.count()` | `Result<u8>` |
| `item.max_stack_size()` | `Result<u8>` |
| `item.id()` | `Result<i32>` |
| `item.aux_value()` | `Result<i32>` |
| `item.damage()` | `Result<i32>` |

## 判定

全部返回 `Result<bool>`：

`is_null`（空堆叠）、`is_block`、`is_enchanted`、`is_armor`、`is_damageable`、`is_damaged`

## 写

| API | 说明 |
| --- | --- |
| `item.set_count(n)` | 数量 |
| `item.set_custom_name(name)` | 自定义名（相当于铁砧改名） |
| `item.set_lore(&lines)` | 描述文字，一行一个元素 |
| `item.set_damage(d)` | 耐久损耗 |

这几个是 `&mut self`，改的是本地那份 SNBT。改完要生效得再交给容器或玩家。

## 附魔

| API | 说明 |
| --- | --- |
| `item.enchants() -> Result<String>` | 当前附魔（SNBT） |
| `item.with_enchants(snbt) -> Result<ItemStack>` | 返回一个换了附魔的**新**堆叠 |

`with_enchants` 不改原对象——它是值对象，这样组合起来更安全。

## 其他

| API | 说明 |
| --- | --- |
| `item.matches(&other) -> bool` | 两个堆叠是不是同一种东西 |
| `item.user_data() -> Result<String>` | 自定义 NBT 数据区 |

## 想改的字段这里没有？

`to_nbt()` 拿出来直接改，再 `from_snbt()` 包回去：

```rust
let mut v = item.to_nbt()?;
v.insert("SomeCustomTag", NbtValue::Int(42));
let item = ItemStack::from_snbt(v.to_snbt());
```
