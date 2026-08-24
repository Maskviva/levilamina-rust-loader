# Dimensions — 自定义维度

用 Rust 重新实现的 [MoreDimensions](https://github.com/LiteLDev/MoreDimensions)。注册原版三个维度之外的新维度，id 从 **3** 开始。

需要开启 `more_dimensions` 特性（仅服务端）：

```toml
[dependencies]
levilamina = { version = "26.20.4", features = ["more_dimensions"] }
```

::: tip 这个特性只是「入口」
C++ 侧是无条件编译进加载器的，启动时自行初始化，钩子和维度配置一直是活的。cargo 特性只决定 Rust 这边能不能调到它，**不是开关**。`is_available()` 只当防御性探针用，别拿来当启用判断。
:::

## 客户端怎么认这些维度

基岩版客户端只知道三个原版维度。加载器在 `ChangeDimensionPacket` 等几个包里把维度 id **在线路上改写成一个假的原版 id**，客户端照常渲染，不会报错。

维度元数据存在 `configs/levilamina-rust-loader/dimensions.json`，所以 id 跨重启稳定。

## 注册

```rust
use levilamina::more_dimensions::{self, GeneratorType, PlotLayout};

// 每次启动都无条件调用 —— 幂等，同名返回同一个 id
let id = more_dimensions::add_simple_dimension("skylands", 12345, GeneratorType::Overworld)?;
```

| API | 说明 |
| --- | --- |
| `add_simple_dimension(name, seed, generator)` | 普通维度，返回 `Result<i32>` |
| `add_plot_dimension(name, seed, &layout)` | 地皮维度，生成器直接铺出网格 |
| `get_dimension_id(name)` | 名字 → id（**也认原版名字**） |
| `get_custom_dimension_id(name)` | 只认自定义维度（>= 3） |
| `is_available()` | 防御性探针 |

`GeneratorType`：`Overworld=1`、`Flat=2`、`Nether=3`、`TheEnd=4`、`Void=5`。

::: danger 不要拿 get_dimension_id 当「存在性检查」
```rust
// ❌ 错的
if let Some(id) = get_dimension_id("myworld") { return id; }
let id = add_simple_dimension("myworld", 0, GeneratorType::Void)?;
```
`get_dimension_id` 回答的是"这个名字对应哪个 id"，不是"我的维度建好没有"，而且它**连原版名字一起认**。旧版桥接对未知名字返回 `VanillaDimensions::Undefined()`，于是有模组把整个世界布局挂到了维度 0——玩家的主世界上。

注册本身就是幂等的，**无条件调用即可**：
```rust
// ✅ 对的
let id = add_simple_dimension("myworld", 0, GeneratorType::Void)?;
```
:::

## 何时注册

必须在**关卡已经打开之后**，也就是 `on_enable` 里或某个调度任务里。

`on_load` 阶段 Level 还是 null，注册会抛异常。也不要在后台线程调。

## 地皮维度

生成器在**区块生成时**直接铺出道路、边框和地板，所以世界是无限的，"建造"不花任何时间。

```rust
use levilamina::more_dimensions::PlotLayout;

let layout = PlotLayout {
    plot_size: 32,
    road_width: 7,
    border_width: 1,
    floor_y: 64,
    floor_block: "minecraft:grass_block".into(),
    fill_block: "minecraft:dirt".into(),
    road_block: "minecraft:smooth_stone".into(),
    border_block: "minecraft:stone_bricks".into(),
    biome: "plains".into(),
};
let id = more_dimensions::add_plot_dimension("plots", 0, &layout)?;
```

### 网格约定

设 `cell = plot_size + road_width`，世界坐标 `(x, z)` 这一列是：

- **道路**：`x.rem_euclid(cell) >= plot_size || z.rem_euclid(cell) >= plot_size`
- **边框**：在地皮边缘 `border_width` 格以内
- **地皮内部**：其余

也就是说地皮占每个格子的低位 `[0, plot_size)`，道路占高位 `[plot_size, cell)`，边框算在 `plot_size` **里面**。

::: warning 你的归属判定必须用同一个公式
对不上的话，玩家看到的方块和他站着的那块地皮就不是一回事。

另外布局是**跟着维度一起持久化**的——后来改配置也不会改已建好的世界。改几何会让所有已有地皮错位，所以要换布局就新建一个世界。
:::

## 按维度的行为规则

基岩版的 gamerule 是全服一份的：想让创造用的地皮世界不刷怪，只能 `doMobSpawning=false`，而这会把同一台服务器上的生存世界也变空。

这里的规则钩在**真正干活的函数**上（`Spawner::spawnMob` 等），那些函数带着 `BlockSource`，能拿到维度 id，所以真正做到按维度隔离。

```rust
use levilamina::more_dimensions::{self, DimensionRule};

more_dimensions::set_dimension_rule(plot_dim, DimensionRule::SpawnMonster, false);
more_dimensions::set_dimension_rule(plot_dim, DimensionRule::ExplodeBlocks, false);
```

| 规则 | 值 | 管什么 |
| --- | :---: | --- |
| `SpawnMonster` | 0 | 怪物自然生成 |
| `SpawnAnimal` | 1 | 动物自然生成 |
| `SpawnSpawner` | 2 | 刷怪笼 |
| `ExplodeBlocks` | 3 | 爆炸破坏方块 |
| `FireSpread` | 4 | 火焰蔓延 |
| `MobGriefing` | 5 | 生物破坏方块 |
| `Projectile` | 6 | 弹射物 |
| `PistonPush` | 7 | 活塞推动 |
| `LiquidFlow` | 8 | 液体流动 |
| `FarmlandDecay` | 9 | 耕地被踩坏 |
| `Ride` | 10 | 骑乘 |
| `PistonCrossPlot` | 11 | 活塞跨地皮推动 |
| `EntityCrossPlot` | 12 | 实体跨地皮移动 |

| API | 说明 |
| --- | --- |
| `set_dimension_rule(dim, rule, allow)` | `allow = false` 表示禁止 |
| `get_dimension_rule(dim, rule)` | `None` = 没设过，走原版逻辑 |
| `clear_dimension_rules(dim)` | 清掉一个维度的全部规则 |

::: tip 没设过的维度完全不受影响
规则表是稀疏的，查不到就走原版。钩子虽然是全局装的，但"默认放行"是硬性约束——原版维度必须感觉不到它们存在。
:::

## 地皮网格与合并

`PistonCrossPlot` / `EntityCrossPlot` 需要加载器知道网格几何，否则那两条规则恒等于放行。

```rust
more_dimensions::set_plot_grid(dim, 32, 7);      // plot_size, road_width
more_dimensions::clear_plot_grid(dim);
```

合并（把相邻地皮打通）用整表替换：

```rust
use levilamina::more_dimensions::PlotMerge;

let merges = vec![
    PlotMerge::from_dirs(0, 0, [false, true, false, false]),  // 0=北 1=东 2=南 3=西
];
more_dimensions::set_plot_merges(dim, &merges);
```

位掩码：`NORTH=1`（z-）、`EAST=2`（x+）、`SOUTH=4`（z+）、`WEST=8`（x-）。

只需要传真的有合并标记的地皮——没条目的按"四面都没合并"处理。几千块地皮、十来处合并的服务器，这张表也就几十个整数。

::: warning 为什么是整表替换而不是增量
增量要求两边对"现在有哪些条目"的看法永远一致，而拆分是先清邻居、再存自己、中途可能失败的。一旦对不上，增量再也没有自愈的机会。整表替换每次都把状态拉回一致。

另外改了网格几何之后要**重推一次** `set_plot_merges`——几何变化会清掉合并缓存。
:::

## 注意维度 id 不再只有 0/1/2

一旦服务器上有自定义维度，`player.dimension()`、`actor.dimension_id()`、事件载荷里的 `dim` 都可能是 >= 3 的值。

```rust
// ❌ 会漏掉自定义维度
match dim { 0 => "主世界", 1 => "下界", _ => "末地" }

// ✅
match dim { 0 => "主世界", 1 => "下界", 2 => "末地", n => &format!("自定义 {n}") }
```
