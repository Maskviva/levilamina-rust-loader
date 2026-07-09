# World — 世界 / 扫描

方块读写、粒子与区域扫描。**仅限服务器线程**。世界/维度未就绪时返回 `Err`。

> 已支持的四项对应桥接自己实现的坐标化封装（内部调用 `Dimension::getBlockSourceFromMainChunkSource` + `BlockSource::getBlock`/`Level::spawnParticleEffect` 等，见 [Entity](/api/entity)/[Block](/api/block) 页对同一批原生方法的说明）；本页新增的「爆炸」直接对应 `Level::explode`（`mc/world/level/Level.h`）。难度/种子/游戏规则等**不带坐标**的全局世界设置放在 [Server](/api/server) 页，和已有的时间/天气归为一类。

| API | 作用 | 状态 |
| --- | --- | :---: |
| `World::get_block(dim, x, y, z)` | 读取单个方块（方块名 + 状态 SNBT） | ✅ |
| `World::set_block(dim, x, y, z, block)` | 放置方块（方块 id 或带状态字符串） | ✅ |
| `World::spawn_particle(dim, effect, x, y, z)` | 在世界坐标生成粒子效果 | ✅ |
| `World::scan_region(dim, a, b)` | 扫描立方体区域，逐层逐格返回方块与实体 | ✅ |

## 爆炸

> 状态：🧩 规划。

| API | 作用 | 原生对应 |
| --- | --- | --- |
| `World::explode(dim, pos, radius, max_resistance, source?, fire=false, breaks_blocks=true, allow_underwater=false)` | 在坐标制造一次爆炸 | `Level::explode(region, source, pos, radius, fire, breaksBlocks, maxResistance, allowUnderwater)` |

> 原生 `Level::explode` 还有另一重载 `explode(Explosion&)`，接受一个预先配置好的 `Explosion` 对象（用于更复杂的自定义爆炸行为，如自定义粒子/音效），暂未纳入简化层。

## 相关类型

| 类型 | 说明 |
| --- | --- |
| `BlockInfo` | 方块：`name` 与 `snbt`；`is_air()` |
| `EntityInfo` | 实体：`kind` 与 `snbt` |
| `Cell` | 一个格子：`block` 及 `entities` |
| `ScanLayer` | 一个 Y 层：`y` 与 `cells[dx][dz]` |
| `Scan` | 扫描结果：`min` / `max` / `layers`，附 `size()` / `entity_count()` |
