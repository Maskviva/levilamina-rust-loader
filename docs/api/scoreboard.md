# ScoreBoard — 计分板

> 状态：🧩 规划。
>
> **接口来源**：本页方法对应原生 C++ 类 `Scoreboard`（`mc/world/scores/Scoreboard.h`，服务端实际用的是其子类 `ServerScoreboard`）与 `Objective`（`mc/world/scores/Objective.h`）。入口是 `Level::getScoreboard()`（真实存在的公开虚方法，和取 `Player`/`Block` 走的是同一个 `level` 对象）。命名沿用 LSE 风格（snake_case）。

## 获取

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `ScoreBoard::get()` | 获取全局计分板 | `Level::getScoreboard` |

## 计分项 Objective

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `ScoreBoard::add_objective(name, display_name, criteria?)` | 新建计分项 | `Scoreboard::addObjective`（`criteria` 默认走 `Scoreboard::createObjectiveCriteria`/`DEFAULT_CRITERIA`） |
| `ScoreBoard::get_objective(name)` | 按名获取计分项 | `Scoreboard::getObjective` |
| `ScoreBoard::remove_objective(objective)` | 删除计分项 | `Scoreboard::removeObjective` |
| `ScoreBoard::objective_names()` | 全部计分项的名字 | `Scoreboard::getObjectiveNames` |
| `ScoreBoard::objectives()` | 全部计分项 | `Scoreboard::getObjectives` |

## 分数读写

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `objective.player_score(id)` | 读取某目标在该计分项下的分数 | `Objective::getPlayerScore` |
| `ScoreBoard::modify_score(id, objective, value, op)` | 按操作类型（设置/加/减）修改分数，一步到位 | `Scoreboard::modifyPlayerScore`（LeviLamina 自带的便捷封装） |
| `ScoreBoard::reset_player_score(id, objective)` | 清除某目标在该计分项下的分数 | `Scoreboard::resetPlayerScore` |
| `ScoreBoard::id_scores(id)` | 某目标在所有计分项下的分数 | `Scoreboard::getIdScores` |
| `ScoreBoard::scoreboard_id(actor)` | 某实体/玩家对应的计分板 id | `Scoreboard::getScoreboardId` |

## 显示位置

原生三个显示槽位是固定的字符串常量，不是自由文本：

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `ScoreBoard::SIDEBAR` / `LIST` / `BELOW_NAME` | 三个显示槽位的名字常量 | `Scoreboard::DISPLAY_SLOT_SIDEBAR` / `DISPLAY_SLOT_LIST` / `DISPLAY_SLOT_BELOWNAME` |
| `ScoreBoard::set_display(slot, objective, sort_order?)` | 把计分项显示到指定槽位 | `Scoreboard::setDisplayObjective` |
| `ScoreBoard::clear_display(slot)` | 清空指定显示槽位 | `Scoreboard::clearDisplayObjective` |
| `ScoreBoard::get_display(slot)` | 读取指定槽位当前显示的计分项 | `Scoreboard::getDisplayObjective` |
| `ScoreBoard::display_slot_names()` | 全部显示槽位名字 | `Scoreboard::getDisplayObjectiveSlotNames` |
| `ScoreBoard::display_scores(slot)` | 指定槽位当前显示的全部分数（已按规则过滤） | `Scoreboard::getDisplayInfoFiltered` |

## 分数变化监听

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `ScoreBoard::add_score_listener(player, objective_name)` | 监听某玩家在某计分项下的分数变化（用于自动刷新客户端显示） | `Scoreboard::addScoreListener` |
| `ScoreBoard::remove_score_listener(player, objective_name)` | 取消监听 | `Scoreboard::removeScoreListener` |

## 附录：其余原生方法

**Scoreboard / Objective / ServerScoreboard**：

| 原生方法 | 作用 |
| --- | --- |
| `createObjectiveCriteria` | 创建一种计分项的评判标准（决定分数如何计算，如是否只读） |
| `getCriteria` | 按名字查找已创建的评判标准 |
| `getDisplayInfoSorted` | 按自定义排序函数取指定槽位的分数列表 |
| `applyPlayerOperation` | 对一批目标批量执行计分板运算符操作（对应 `/scoreboard players operation`） |
| `clearScoreboardIdentity` | 彻底清除一个计分板身份（含其在所有计分项下的分数） |
| `getScoreboardIdentityRefs` | 全部已注册的计分板身份引用 |
| `getTrackedIds` | 全部被追踪的计分板 id |
| `registerScoreboardIdentity` | 从存档数据注册一个计分板身份 |
| `Objective::serialize` / `deserialize` | 计分项的存档读写（静态） |
| `ScoreboardId::INVALID()` | 无效 id 的静态哨兵值 |

> `ServerScoreboard` 上还有一批以 `_` 开头的内部方法（如 `_updateScoreTag`、`_clearAllScoreTagsForObjective`），是 Mojang 自己标记为内部的实现细节，不在此列出。

## 相关类型

| 类型 | 说明 |
| --- | --- |
| `ScoreboardId` | 一个"计分板身份"的引用（玩家或虚拟的"假玩家"条目都算） |
| `PlayerScoreSetFunction` | 分数运算方式：`Set` / `Add` / `Subtract` |
| `ObjectiveSortOrder` | 排序方向：`Ascending` / `Descending` |
| `ObjectiveRenderType` | 显示样式：`Integer`（数字） / `Hearts`（爱心） |
| `ScoreInfo` | 单条分数：所属计分项、是否有效、数值 |
