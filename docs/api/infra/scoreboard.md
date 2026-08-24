# ScoreBoard — 计分板

分数的身份是**「假玩家名」**——和原版 `/scoreboard players` 用的是同一套命名空间，所以这里做的一切管理员在游戏里看得见、也能手动改。

```rust
use levilamina::prelude::*;

let sb = Scoreboard::get();
sb.add_objective("kills", "§c击杀数")?;
sb.set_display(DisplaySlot::Sidebar, "kills")?;
sb.add_score("kills", "Steve", 1)?;
```

## 计分项

| API | 说明 |
| --- | --- |
| `sb.add_objective(name, display_name)` | 新建 |
| `sb.remove_objective(name)` | 删除 |
| `sb.objectives() -> Vec<Objective>` | 列出全部 |

```rust
pub struct Objective { pub name: String, pub display_name: String }
```

## 分数

| API | 返回 | 说明 |
| --- | --- | --- |
| `sb.score(objective, who)` | `Option<i64>` | 读；`None` = 没有记录 |
| `sb.set_score(objective, who, value)` | `Result<i64>` | 设为某值，返回设置后的值 |
| `sb.add_score(objective, who, delta)` | `Result<i64>` | 加 |
| `sb.reduce_score(objective, who, delta)` | `Result<i64>` | 减 |
| `sb.reset_score(objective, who)` | `Result<()>` | 清掉这条记录 |

::: tip `score()` 返回 None 和返回 Some(0) 不一样
`None` 是"这个人在这个计分项里没有记录"，`Some(0)` 是"有记录，值是 0"。侧边栏只显示有记录的行，所以这个区别是看得见的。
:::

## 显示槽

```rust
sb.set_display(DisplaySlot::Sidebar, "kills")?;
sb.clear_display(DisplaySlot::Sidebar)?;
```

`DisplaySlot`：`Sidebar`（右侧边栏）、`List`（暂停菜单玩家列表）、`BelowName`（头顶名字下方）。

## who 传什么

传玩家名就是给玩家计分。传任意其他字符串就是一个"假玩家"——这是原版计分板做全局变量的标准手法：

```rust
sb.set_score("config", "#pvp_enabled", 1)?;
```

以 `#` 开头是社区惯例，这样的名字不会和真实玩家冲突，侧边栏里也一眼能认出来。
