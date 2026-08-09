# Server — 服务端

`Server` 是服务端能力的总入口。零大小类型，随便复制。

```rust
let s = ctx.server();       // 生命周期钩子里
let s = Server::get();      // 任何地方
```

除了 `schedule` / `schedule_after` / `gaming_status`，**所有方法都必须在服务器线程调用**。

## 状态与时钟

| API | 返回 | 说明 |
| --- | --- | --- |
| `s.gaming_status()` | `GamingStatus` | `Default` / `Starting` / `Running` / `Stopping`。**线程安全** |
| `s.get_current_tick()` | `Result<u64>` | 当前 tick |
| `s.get_tick_delta_time()` | `Result<f64>` | 上一 tick 耗时，**秒**（健康时 0.05） |
| `s.get_tps()` | `Result<f64>` | TPS，上限截到 20.0 |
| `s.get_active_player_count()` | `Result<i32>` | 在线人数 |
| `s.is_sim_paused()` | `Result<bool>` | 模拟是否暂停中 |
| `s.bds_version()` | `Result<String>` | BDS 版本号 |
| `s.protocol_version()` | `Result<i32>` | 协议版本 |

::: tip TPS 是 1.0/dt 不是 1000.0/dt
`get_tick_delta_time` 的单位是秒。自己算 TPS 时别按毫秒算。另外单个 tick 可能快于 50ms，所以要截顶——游戏本身不会跑超过 20 TPS。
:::

## 时间 / 天气 / 难度 / 规则

| API | 说明 |
| --- | --- |
| `s.time()` / `s.set_time(t)` | 世界时间 |
| `s.set_weather(weather)` | `Weather::Clear` / `Rain` / `Thunder` |
| `s.difficulty()` / `s.set_difficulty(d)` | 难度 |
| `s.seed()` | 世界种子 |
| `s.game_rule(name)` | 读一条 gamerule，返回 `NbtValue` |
| `s.set_game_rule(name, value)` | 写一条 gamerule |
| `s.update_weather(...)` | 细粒度天气控制 |
| `s.sleep_status()` | 睡觉进度（SNBT） |

::: warning gamerule 是全服一份的
想让某个世界不刷怪而其他世界照常，gamerule 做不到——它没有维度维度。要按维度隔离用 [Dimensions](/api/dimensions) 的 `set_dimension_rule`。
:::

## 方块读写

| API | 说明 |
| --- | --- |
| `s.get_block(dim, x, y, z)` | 返回 `(类型名, SNBT)` |
| `s.set_block(dim, x, y, z, spec)` | 放置方块 |
| `s.get_biome(dim, x, y, z)` | 生物群系名 |
| `s.scan_region(dim, a, b)` | 扫描一块区域，见 [World](/api/world) |

单个方块的完整操作（方块状态、方块实体、tag）用 [`Block`](/api/block)。

## 生成与爆炸

| API | 说明 |
| --- | --- |
| `s.spawn_mob(dim, type_name, x, y, z)` | 生成实体，返回 `Result<Entity>` |
| `s.explode(...)` | 制造爆炸 |
| `s.spawn_particle(dim, effect, x, y, z)` | 给所有人放粒子 |
| `s.spawn_particle_for(...)` | 只给指定玩家放 |
| `s.find_path(entity, x, y, z)` | 寻路结果（SNBT） |

## 出生点与存档

| API | 说明 |
| --- | --- |
| `s.default_spawn()` | 世界默认出生点 |
| `s.set_default_spawn(x, y, z)` | 设置默认出生点 |
| `s.save_level()` | 立刻存档 |

## 玩家与世界数据

| API | 说明 |
| --- | --- |
| `s.list_players()` | `Vec<PlayerInfo>`，等价于 `Player::list()` |
| `s.player_position(name)` | `Option<PlayerPos>` |
| `s.villages(dim)` | 村庄列表 |
| `s.structures_near(...)` | 附近的结构（HSA） |

## tick 控制

调试和录制用，正式服慎用：

| API | 说明 |
| --- | --- |
| `s.set_tick_freeze(on)` | 冻结 / 解冻模拟 |
| `s.step_ticks(n)` | 冻结状态下步进 n 刻 |
| `s.set_tick_warp(factor)` | 变速 |

## 性能分析

| API | 说明 |
| --- | --- |
| `s.begin_profile(ticks)` | 开始采样 n 刻 |
| `s.take_profile()` | 取结果，`Result<Option<NbtValue>>` |

## 命令 / 事件 / 调度

这些在各自的页面：

- 执行和注册命令 → [Command](/api/command)
- `subscribe_event` / `list_events` → [Event](/api/event)
- `schedule` / `schedule_after` / `cancel_task` / `pending_tasks` → [Scheduler](/api/scheduler)
- `spawn_sim_player` / `sim_player` / `is_simulated` / `list_sim_players` → [SimPlayer](/api/sim)
