# World — 世界扫描

一次拿回一整块区域的方块和实体，比逐格 `Block::at` 快得多。

```rust
use levilamina::prelude::*;

let scan = ctx.server().scan_region(0, (0, 60, 0), (15, 70, 15))?;
println!("{:?} 格，{} 个非空，{} 个实体",
    scan.size(), scan.non_empty_count(), scan.entity_count());
```

## Scan 的结构

```rust
pub struct Scan {
    pub min: (i32, i32, i32),
    pub max: (i32, i32, i32),
    pub layers: Vec<ScanLayer>,     // 从底层往上，一个 Y 一层
}

pub struct ScanLayer {
    pub y: i32,
    pub cells: Vec<Vec<Cell>>,      // cells[x偏移][z偏移]
}

pub struct Cell {
    pub block: BlockInfo,
    pub entities: Vec<EntityInfo>,
}

pub struct BlockInfo { pub name: String, pub snbt: String }
pub struct EntityInfo { pub kind: String, pub snbt: String }
```

索引是**相对最小角的偏移**，不是世界坐标。世界坐标 = `min + 偏移`。

| API | 说明 |
| --- | --- |
| `scan.size()` | `(size_x, size_y, size_z)` |
| `scan.non_empty_count()` | 非空格子数（非空气，或有实体） |
| `scan.entity_count()` | 实体总数 |
| `cell.is_empty()` | 空气且无实体 |
| `block_info.is_air()` | 是空气 |

## 遍历

```rust
for layer in &scan.layers {
    for (dx, column) in layer.cells.iter().enumerate() {
        for (dz, cell) in column.iter().enumerate() {
            if cell.block.is_air() { continue; }
            let (x, y, z) = (scan.min.0 + dx as i32, layer.y, scan.min.2 + dz as i32);
            println!("{x} {y} {z} = {}", cell.block.name);
        }
    }
}
```

::: warning 别扫太大
一次扫描的内存开销是 `x * y * z * (方块名 + SNBT)`。100×100×100 就是一百万个 `Cell`，每个带两个 `String`。分块扫，或者只扫真正需要的 Y 范围。
:::

## 玩家位置

```rust
if let Some(p) = ctx.server().player_position("Steve") {
    let (bx, by, bz) = p.block();     // 取整到方块格
    println!("{} {} {} dim={}", p.x, p.y, p.z, p.dim);
}
```

```rust
pub struct PlayerPos { pub x: f64, pub y: f64, pub z: f64, pub dim: i32 }
```

## 村庄

```rust
for v in ctx.server().villages(0) {
    println!("{} 中心 {:?} POI {} 个", v.uuid, v.center, v.poi_count);
}
```

```rust
pub struct VillageInfo {
    pub uuid: String,
    pub center: (f64, f64, f64),
    pub bounds: Bounds,
    pub poi_count: u64,
}
```

## 结构

```rust
for s in ctx.server().structures_near(0, 0, 64, 0, 512) {
    println!("{} {:?}", s.kind, s.bounds);
}
```

```rust
pub struct StructureInfo { pub kind: String, pub bounds: Bounds }
pub struct Bounds { pub min: (i32,i32,i32), pub max: (i32,i32,i32) }   // 闭区间
```

拿到的是硬编码生成区（HSA）——要塞、林地府邸这一类。

## 写世界

这一页只管读。写走这几条路：

- 单个方块 → [`Block::set`](/api/block)
- 大范围 → `Server::execute_command("fill …")`，引擎内部的批量路径快得多
- 生成实体 → `Server::spawn_mob`
