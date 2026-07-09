# 世界与玩家

本页汇总当前 ABI 已提供的世界/玩家能力：服务器状态、玩家坐标、粒子、区域扫描——以及"其余写操作走命令"的组合拳。目标 API 的完整面貌见 [World](/api/world)、[Player](/api/player)、[Server](/api/server) 参考页。

## 服务器状态

这组查询用于健康监控、按 tick 节流等场景：

```rust
let server = Server::get();

server.gaming_status();              // Default / Starting / Running / Stopping (线程安全)
let tick  = server.get_current_tick()?;        // 当前 tick 编号
let delta = server.get_tick_delta_time()?;     // 上一 tick 耗时(秒)
let tps   = server.get_tps()?;                 // TPS(上限 20.0)
let n     = server.get_active_player_count()?; // 在线人数
let pause = server.is_sim_paused()?;           // 模拟是否暂停
```

除 `gaming_status` 外都**仅限服务器线程**，世界未就绪时返回 `Err`。

## 玩家坐标

```rust
if let Some(p) = server.player_position("Steve") {
    let (x, y, z) = p.block();       // 所在方块的整数坐标
    logger.info(&format!("Steve 在维度 {} 的 {:?}", p.dim, p.block()));
}
```

按名字查询在线玩家的脚底坐标 + 维度；不在线返回 `None`。

## 粒子

```rust
// 在主世界 (0.5, 65.0, 0.5) 处生成一个红石粒子
server.spawn_particle(0, "minecraft:redstone_wire_dust_particle", 0.5, 65.0, 0.5)?;
```

原生只有"单点生成"这一个粒子能力；画线、画框这类几何效果就是在 Rust 侧按几何取一串点、逐点生成——`region-scan` 示例用这个办法给选区画出动画外框，照抄即可，无需等待专门的 API（详见 [Objects 参考的 Particle 一节](/api/objects)）。

## 区域扫描

`scan_region` 一次性遍历一个立方体区域，逐层逐格返回方块与实体：

```rust
let scan = server.scan_region(0, (10, 60, 10), (20, 66, 20))?;

let (w, h, d) = scan.size();
logger.info(&format!("扫描 {w}×{h}×{d}, 非空格 {}, 实体 {}",
    scan.non_empty_count(), scan.entity_count()));

for layer in &scan.layers {                 // 自底向上,每个 Y 层一个二维网格
    for row in &layer.cells {               // dx: 西→东
        for cell in row {                   // dz: 北→南
            if !cell.block.is_air() {
                // cell.block.name / cell.block.snbt(完整方块状态)
            }
            for e in &cell.entities {
                // e.kind / e.snbt(实体完整 NBT)
            }
        }
    }
}
```

数据模型：`Scan { min, max, layers }` → `ScanLayer { y, cells[dx][dz] }` → `Cell { block: BlockInfo, entities: Vec<EntityInfo> }`。一个 6 格高的选区就是 6 个 `ScanLayer`。

> 扫描是一次性把数据整体拷出来，之后随便在任何线程分析这份数据（它是纯 Rust 值）；但发起 `scan_region` 调用本身必须在服务器线程。大区域注意成本——`region-scan` 示例把实时自动扫描上限设在 32³ 格，超过就改用一次性收集。

## 写操作：当前走命令

单方块读写、传送、给物品、改游戏模式……这些写操作当前统一通过 [`execute_command`](/guide/commands) 落地：

```rust
server.execute_command("setblock 10 64 10 minecraft:diamond_block")?;
server.execute_command(&format!("tp \"{name}\" 0 100 0"))?;
server.execute_command(&format!("give \"{name}\" minecraft:apple 5"))?;
```

命令行语法由 Mojang 保持向后兼容，反而比直接调底层引擎方法更抗版本变动——这是桥接自己也在用的策略（见[设计取舍记录](/advanced/decisions)第 3 条）。强类型的 `World::set_block`、`player.teleport` 等 API 的目标设计见各参考页。

## 组合示例：谁站在钻石块上

```rust
fn check_players(server: &Server, logger: &Logger, names: &[String]) {
    for name in names {
        let Some(p) = server.player_position(name) else { continue };
        let (x, y, z) = p.block();
        // 扫脚下那一格
        if let Ok(scan) = server.scan_region(p.dim, (x, y - 1, z), (x, y - 1, z)) {
            let block = &scan.layers[0].cells[0][0].block;
            if block.name == "minecraft:diamond_block" {
                let _ = server.execute_command(&format!("title \"{name}\" actionbar 你站在钻石上!"));
            }
        }
    }
}
```
