# Server — 服务端

服务器 / 存档级别的状态、时间、天气与设置。除 `Server::status()` 线程安全外，其余**仅限服务器线程**；未就绪时相关查询返回 `Err`。

> **接口来源**：本页"时间与天气"以下的世界级设置对应原生 C++ 类 `Level`（`mc/world/level/Level.h`）与 `GameRules`（`mc/world/level/storage/GameRules.h`）的公开方法（均为 `virtual`，排除 `$` 前缀的内部虚函数插桩）；"设置与信息"是服务器进程级配置，暂无一一对应的原生查询点，属规划设计。

## 状态

| API | 作用 | 状态 |
| --- | --- | :---: |
| `Server::status()` | 运行状态（Default/Starting/Running/Stopping），线程安全 | ✅ |
| `Server::current_tick()` | 当前 tick 编号 | ✅ |
| `Server::tick_delta_time()` | 上一 tick 耗时（秒） | ✅ |
| `Server::tps()` | 计算 TPS（上限 20.0） | ✅ |
| `Server::player_count()` | 在线玩家数 | ✅ |
| `Server::is_sim_paused()` | 模拟是否暂停 | ✅ |

## 时间与天气

| API | 作用 | 原生对应 | 状态 |
| --- | --- | --- | :---: |
| `Server::get_time()` | 读取世界时间 | `Level::getTime`（直接原生调用） | ✅ |
| `Server::set_time(value)` | 设置世界时间 | 语义等价 `Level::setTime`；实现走 `/time set` 命令（见[设计取舍记录](/advanced/decisions)第 3 条） | ✅ |
| `Server::set_weather(weather)` | 设置天气（Clear/Rain/Thunder） | 走 `/weather` 命令 | ✅ |

> 原生还有 `Level::updateWeather(rainLevel, rainTime, lightningLevel, lightningTime)`，可连续数值（而非三态枚举）精确控制雨/雷强度与持续时间，暂未纳入简化层，是比当前 `set_weather` 更精细的升级路径。

## 难度与随机种子

> 状态：🧩 规划。

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `Server::difficulty()` | 读取当前难度 | `Level::getDifficulty` |
| `Server::set_difficulty(difficulty)` | 设置难度 | `Level::setDifficulty` |
| `Server::seed()` | 世界种子 | `Level::getSeed` / `getLevelSeed64`（64 位版本） |

## 游戏规则 Game Rules

> 状态：🧩 规划。

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `Server::game_rule_bool(name)` | 读取一个布尔型游戏规则（如 `"doDaylightCycle"`） | `GameRules::getBool`（配合 `nameToGameRuleIndex` 按名查找） |
| `Server::game_rule_int(name)` | 读取一个整数型游戏规则（如 `"randomTickSpeed"`） | `GameRules::getInt` |
| `Server::set_game_rule(name, value)` | 设置一个游戏规则 | `GameRules::setRule`（简化层只暴露"值 + 是否成功"，原生签名还带若干校验用的输出参数） |

## 设置与信息

| API | 作用 | 状态 |
| --- | --- | :---: |
| `Server::set_motd(motd)` | 设置服务器 MOTD | 🧩 |
| `Server::set_max_players(n)` | 设置最大玩家数 | 🧩 |
| `Server::bds_version()` | BDS 版本 | 🧩 |
| `Server::protocol_version()` | 协议版本号 | 🧩 |

## 相关类型

| 类型 | 说明 |
| --- | --- |
| `GamingStatus` | `Default` / `Starting` / `Running` / `Stopping` |
| `Weather` | `Clear` / `Rain` / `Thunder` |
| `Difficulty` | `Peaceful` / `Easy` / `Normal` / `Hard` |

