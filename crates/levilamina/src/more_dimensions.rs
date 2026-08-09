//! Custom dimensions — a Rust reimplementation of
//! [LiteLDev/MoreDimensions](https://github.com/LiteLDev/MoreDimensions),
//! built into every server build of the loader unconditionally (no xmake
//! flag required) and exposed through the `more_dimensions` cargo feature
//! (server only). The C++ side self-initializes at loader startup — its
//! hooks and dimension config are live whether or not Rust ever calls in.
//! The `more_dimensions` feature is only the *entry point* that surfaces
//! these FFI calls to Rust mods; it does NOT switch the C++ feature on or
//! off. Use [`is_available`] only as a defensive probe, never as an enable
//! gate.
//!
//! A custom dimension is a `Dimension` subclass registered past the vanilla
//! ids (overworld=0, nether=1, the_end=2), so new ids start at 3. Because the
//! Bedrock client only knows about the three vanilla dimensions, the loader
//! transparently rewrites the dimension id inside `ChangeDimensionPacket` (and
//! a few related packets) to a fake vanilla id on the wire, so the client
//! renders the world without complaints. Dimension metadata persists across
//! restarts in `configs/levilamina-rust-loader/dimensions.json`, keeping ids
//! stable.
//!
//! # Register unconditionally — do not probe first
//!
//! `add_simple_dimension` / [`add_plot_dimension`] are **idempotent**: calling
//! them again with a name that was registered on an earlier boot returns the
//! same persisted id. So the correct startup sequence is simply:
//!
//! ```no_run
//! # use levilamina::more_dimensions::{self, GeneratorType};
//! // every boot, for every dimension you own:
//! let id = more_dimensions::add_simple_dimension("skylands", 12345, GeneratorType::Overworld)?;
//! # Ok::<(), levilamina::Error>(())
//! ```
//!
//! Do **not** write `if let Some(id) = get_dimension_id(name) { return id }`
//! as an "already exists?" probe. [`get_dimension_id`] answers a different
//! question (it also resolves vanilla names), and getting that wrong is how
//! a mod once attached its whole world layout to dimension 0 — see that
//! function's docs.
//!
//! # Threading
//! All calls run on the server thread, **after the level is open**. Register
//! from `on_enable` or a scheduled task — never from `on_load` (the level is
//! still null then and registration throws) and never from a background
//! thread.

use crate::error::{Error, Result};
use crate::ffi::s;
use crate::rt;

/// World generator a [`add_simple_dimension`] dimension is populated with.
///
/// # These discriminants are the *engine's* values, not ours
///
/// The numbers below are `::GeneratorType` from `mc/world/level/GeneratorType.h`
/// verbatim, and they are handed to the C++ side as a raw `i32` that ends up in
/// a `switch` over that enum. They are therefore **ABI, not an arbitrary
/// ordering** — renumbering them shifts every world one generator sideways.
///
/// The engine's `Legacy = 0` and `Undefined = 6` are deliberately not exposed:
/// neither has a generator arm on the C++ side, so both would silently fall
/// through to the void generator.
///
/// For plot worlds use [`add_plot_dimension`] instead — it has a real chunk
/// generator and ignores this enum entirely.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(i32)]
pub enum GeneratorType {
    /// Surface terrain with caves, biomes, and structures (vanilla overworld).
    Overworld = 1,
    /// Superflat — a few flat layers, no terrain noise.
    Flat = 2,
    /// Nether-style terrain with netherrack and lava seas.
    Nether = 3,
    /// End-style floating islands of end stone.
    TheEnd = 4,
    /// Empty void world; only the spawn platform exists.
    Void = 5,
}

impl GeneratorType {
    pub fn from_i32(v: i32) -> Option<Self> {
        match v {
            1 => Some(Self::Overworld),
            2 => Some(Self::Flat),
            3 => Some(Self::Nether),
            4 => Some(Self::TheEnd),
            5 => Some(Self::Void),
            _ => None,
        }
    }

    /// The `magic_enum` name the C++ side persists into `dimensions.json`.
    ///
    /// Exposed so callers can check what an already-created dimension was
    /// actually built with — the stored name, not this enum, is what the
    /// engine replays on every subsequent boot.
    pub fn engine_name(self) -> &'static str {
        match self {
            Self::Overworld => "Overworld",
            Self::Flat => "Flat",
            Self::Nether => "Nether",
            Self::TheEnd => "TheEnd",
            Self::Void => "Void",
        }
    }
}

#[cfg(test)]
mod generator_type_tests {
    use super::GeneratorType::*;

    // 这几个数字是从 mc/world/level/GeneratorType.h 抄来的，不是我们自己排的。
    // 谁要是想「顺手把 Overworld 挪回 0」，先让这个测试拦下来。
    #[test]
    fn discriminants_match_engine_header() {
        assert_eq!(Overworld as i32, 1);
        assert_eq!(Flat as i32, 2);
        assert_eq!(Nether as i32, 3);
        assert_eq!(TheEnd as i32, 4);
        assert_eq!(Void as i32, 5);
    }

    #[test]
    fn from_i32_round_trips() {
        for g in [Overworld, Flat, Nether, TheEnd, Void] {
            assert_eq!(super::GeneratorType::from_i32(g as i32), Some(g));
        }
        // Legacy(0) 和 Undefined(6) 不在映射里，必须是 None 而不是某个默认值。
        assert_eq!(super::GeneratorType::from_i32(0), None);
        assert_eq!(super::GeneratorType::from_i32(6), None);
    }
}

/// Geometry of a plot world, handed to [`add_plot_dimension`].
///
/// # Grid convention
///
/// With `cell = plot_size + road_width`, a column at world `(x, z)` is:
///
/// * **road** when `x.rem_euclid(cell) >= plot_size || z.rem_euclid(cell) >= plot_size`
/// * **border** when it's within `border_width` of the plot's edge
/// * **plot interior** otherwise
///
/// That is: plots occupy the low part of every cell, `[0, plot_size)`, and
/// roads the high part, `[plot_size, cell)`. The border is counted *inside*
/// `plot_size`. Your ownership / protection logic must use the same formula
/// or the blocks players see won't match the plot they're standing in.
///
/// # Vertical layout
///
/// | y | block |
/// |---|---|
/// | `-64` | bedrock |
/// | `-63 .. floor_y` | `fill_block` |
/// | `floor_y` | `floor_block` (or `road_block` on roads) |
/// | `floor_y + 1` | `border_block`, on border columns only |
/// | above | air |
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PlotLayout {
    pub plot_size: i32,
    pub road_width: i32,
    pub border_width: i32,
    pub floor_y: i32,
    pub floor_block: String,
    pub fill_block: String,
    pub road_block: String,
    pub border_block: String,
    pub biome: String,
}

impl Default for PlotLayout {
    fn default() -> Self {
        PlotLayout {
            plot_size: 64,
            road_width: 7,
            border_width: 1,
            floor_y: 64,
            floor_block: "minecraft:grass_block".into(),
            fill_block: "minecraft:dirt".into(),
            road_block: "minecraft:birch_planks".into(),
            border_block: "minecraft:stone_block_slab".into(),
            biome: "minecraft:plains".into(),
        }
    }
}

impl PlotLayout {
    /// Cell pitch — plot plus road.
    pub fn cell_size(&self) -> i32 {
        self.plot_size + self.road_width
    }

    /// Encode as the SNBT the bridge expects. Block ids are emitted with
    /// escaped quotes; they're plain resource ids so no other escaping is
    /// needed, but we still reject embedded quotes defensively.
    pub fn to_snbt(&self) -> String {
        fn q(v: &str) -> String {
            v.replace('\\', "").replace('"', "")
        }
        format!(
            "{{plotSize:{},roadWidth:{},borderWidth:{},floorY:{},\
             floorBlock:\"{}\",fillBlock:\"{}\",roadBlock:\"{}\",\
             borderBlock:\"{}\",biome:\"{}\"}}",
            self.plot_size,
            self.road_width,
            self.border_width,
            self.floor_y,
            q(&self.floor_block),
            q(&self.fill_block),
            q(&self.road_block),
            q(&self.border_block),
            q(&self.biome),
        )
    }
}

/// Whether the loader was built with MoreDimensions support
/// (`LEVI_RS_FEATURE_MORE_DIMENSIONS`). Always `true` when this module
/// compiles; kept as a runtime probe for defensive callers.
pub fn is_available() -> bool {
    unsafe { (rt().api.md_is_available)() }
}

/// Register a [`SimpleCustomDimension`] and return its assigned id (>= 3).
///
/// `seed` drives terrain generation independently of the world seed. The id is
/// persisted to the dimension config file, so the same `name` resolves to the
/// same id after a restart — **call this unconditionally on every startup**
/// rather than probing with [`get_dimension_id`] first.
///
/// Returns `Err` if registration threw on the C++ side (level not open yet,
/// factory failure, or the loader wasn't built with the feature).
pub fn add_simple_dimension(name: &str, seed: u32, generator: GeneratorType) -> Result<i32> {
    let id = unsafe { (rt().api.md_add_simple_dimension)(s(name), seed, generator as i32) };
    check_id(id, name, "add_simple_dimension")
}

/// Register a plot-world dimension whose **chunk generator** lays out the plot
/// grid — roads, borders and floor are produced at generation time, so the
/// world is infinite and costs nothing to "build".
///
/// The layout is persisted alongside the dimension, so it stays fixed across
/// restarts even if your config changes later. (Changing the geometry of a
/// world players have already built in would misalign every plot, so this is
/// deliberate — create a new world instead.)
///
/// Idempotent, exactly like [`add_simple_dimension`].
///
/// ```no_run
/// use levilamina::more_dimensions::{self, PlotLayout};
///
/// let layout = PlotLayout { plot_size: 96, road_width: 9, ..Default::default() };
/// let dim = more_dimensions::add_plot_dimension("plot_main", 42, &layout)?;
/// # Ok::<(), levilamina::Error>(())
/// ```
pub fn add_plot_dimension(name: &str, seed: u32, layout: &PlotLayout) -> Result<i32> {
    let snbt = layout.to_snbt();
    let id = unsafe { (rt().api.md_add_plot_dimension)(s(name), seed, s(&snbt)) };
    check_id(id, name, "add_plot_dimension")
}

fn check_id(id: i32, name: &str, what: &str) -> Result<i32> {
    if id >= 3 {
        Ok(id)
    } else {
        Err(Error(format!(
            "more_dimensions::{what}('{name}') failed (returned {id}; custom dimensions must be \
             >= 3). Common causes: called before the level was open (register from on_enable, \
             not on_load), a duplicate/invalid name, or a loader built without \
             LEVI_RS_FEATURE_MORE_DIMENSIONS."
        )))
    }
}

/// Resolve a dimension name to its numeric id, or `None` when the name isn't
/// registered.
///
/// Works for vanilla names (`"overworld"` → 0, `"nether"` → 1, `"the_end"` →
/// 2) and for custom names registered via [`add_simple_dimension`] /
/// [`add_plot_dimension`] (>= 3).
///
/// # This is probably not the function you want
///
/// It answers "what id does this name have", not "does my custom dimension
/// exist yet". Since registration is idempotent, just register again. Using
/// this as an existence probe is how a mod ended up treating dimension 0
/// (the players' overworld) as its own world: the old bridge returned
/// `VanillaDimensions::Undefined()` verbatim for unknown names, and that
/// value is mutated at runtime so it always looks like a real id. The bridge
/// now validates by round-tripping the name, but the API is still the wrong
/// tool for that job.
///
/// If you must check, prefer [`get_custom_dimension_id`], which refuses to
/// return a vanilla id.
pub fn get_dimension_id(name: &str) -> Option<i32> {
    let id = unsafe { (rt().api.md_get_dimension_id)(s(name)) };
    (id >= 0).then_some(id)
}

/// Like [`get_dimension_id`] but only resolves **custom** dimensions (>= 3),
/// so a mistaken lookup can never hand you the overworld.
pub fn get_custom_dimension_id(name: &str) -> Option<i32> {
    get_dimension_id(name).filter(|&id| id >= 3)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn layout_snbt_roundtrip_shape() {
        let snbt = PlotLayout::default().to_snbt();
        assert!(snbt.starts_with("{plotSize:64,roadWidth:7,borderWidth:1,floorY:64,"));
        assert!(snbt.contains("floorBlock:\"minecraft:grass_block\""));
        assert!(snbt.ends_with("biome:\"minecraft:plains\"}"));
    }

    #[test]
    fn layout_snbt_strips_quotes() {
        let layout = PlotLayout {
            floor_block: "minecraft:\"evil\"".into(),
            ..Default::default()
        };
        let snbt = layout.to_snbt();
        assert!(snbt.contains("floorBlock:\"minecraft:evil\""));
    }

    #[test]
    fn cell_size() {
        assert_eq!(PlotLayout::default().cell_size(), 71);
    }
}

/// 按维度生效的行为规则。
///
/// # 和 gamerule 的区别
///
/// 基岩版的 gamerule 是**全服一份**的。想让创造用的地皮世界不刷怪，只能
/// `doMobSpawning=false`，而这会连带把同一个服务器上的生存世界也变空。
///
/// 这里的规则是 loader 钩在**真正干活的函数**上判定的（`Spawner::spawnMob`
/// 等），那些函数带着 `BlockSource`，从中拿得到维度 id，所以真正做到按维度隔离。
///
/// # 没设过的维度完全不受影响
///
/// 规则表是稀疏的：查不到就走原版逻辑。这些 hook 是全局装上的，所以"默认放行"
/// 是硬性约束——原版维度必须完全感觉不到它们的存在。
///
/// 判别式是 ABI，和 C++ 侧的 `LeviRsDimRule` 逐值对应，**只能追加**。
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(i32)]
pub enum DimensionRule {
    /// 自然生成的敌对生物
    SpawnMonster = 0,
    /// 自然生成的友好生物
    SpawnAnimal = 1,
    /// 刷怪笼产出的生物
    SpawnSpawner = 2,
    /// 爆炸破坏地形
    ExplodeBlocks = 3,
    /// 火焰蔓延到相邻方块
    FireSpread = 4,
    /// 生物改变方块（苦力怕炸坑、末影人搬方块等）
    MobGriefing = 5,
    /// 弹射物生成
    Projectile = 6,
    /// 活塞推动方块（红石照常工作，只是搬不动方块）
    PistonPush = 7,
    /// 液体蔓延（已放下的源还在，只是不往外爬）
    LiquidFlow = 8,
    /// 耕地被踩回泥土
    FarmlandDecay = 9,
    /// 乘坐载具 / 骑乘生物
    Ride = 10,
    /// 活塞把方块推**过地皮边界**。
    ///
    /// 和 [`DimensionRule::PistonPush`] 是两件事：那个是整维度关掉活塞搬运，
    /// 这个是地皮内部照常推、跨界才拦。两条都设时任意一条禁止就推不动。
    ///
    /// 需要先用 [`set_plot_grid`] 注册网格，否则恒等于放行。
    PistonCrossPlot = 11,
    /// 实体越过地皮边界。玩家和**载人的**载具永远不受此限。
    EntityCrossPlot = 12,
}

impl DimensionRule {
    pub const ALL: &'static [DimensionRule] = &[
        DimensionRule::SpawnMonster,
        DimensionRule::SpawnAnimal,
        DimensionRule::SpawnSpawner,
        DimensionRule::ExplodeBlocks,
        DimensionRule::FireSpread,
        DimensionRule::MobGriefing,
        DimensionRule::Projectile,
        DimensionRule::PistonPush,
        DimensionRule::LiquidFlow,
        DimensionRule::FarmlandDecay,
        DimensionRule::Ride,
        DimensionRule::PistonCrossPlot,
        DimensionRule::EntityCrossPlot,
    ];
}

/// 设一条规则。`allow = false` 表示在这个维度里禁止该行为。
///
/// 对 loader 不认识的维度调用也是无害的——规则表按原始维度 id 存，
/// 只有那个 id 在 hook 里出现时才会被查到。
pub fn set_dimension_rule(dimension: i32, rule: DimensionRule, allow: bool) {
    unsafe { (rt().api.md_set_dimension_rule)(dimension, rule as i32, allow) }
}

/// 读一条规则。返回 `None` 表示这个维度没有显式设过该规则（= 按原版走）。
pub fn get_dimension_rule(dimension: i32, rule: DimensionRule) -> Option<bool> {
    let mut out = false;
    let found = unsafe { (rt().api.md_get_dimension_rule)(dimension, rule as i32, &mut out) };
    found.then_some(out)
}

/// 清掉一个维度的全部规则。世界被删除时调用。
pub fn clear_dimension_rules(dimension: i32) {
    unsafe { (rt().api.md_clear_dimension_rules)(dimension) }
}

#[cfg(test)]
mod dimension_rule_tests {
    use super::DimensionRule::*;

    /// 这几个数字和 C++ 侧的 LeviRsDimRule / DimRule 是同一份 ABI。
    /// 谁要是想重排，先让这个测试拦下来。
    #[test]
    fn discriminants_are_abi() {
        assert_eq!(SpawnMonster as i32, 0);
        assert_eq!(SpawnAnimal as i32, 1);
        assert_eq!(SpawnSpawner as i32, 2);
        assert_eq!(ExplodeBlocks as i32, 3);
        assert_eq!(FireSpread as i32, 4);
        assert_eq!(MobGriefing as i32, 5);
        assert_eq!(Projectile as i32, 6);
        assert_eq!(PistonPush as i32, 7);
        assert_eq!(LiquidFlow as i32, 8);
        assert_eq!(FarmlandDecay as i32, 9);
        assert_eq!(Ride as i32, 10);
        assert_eq!(PistonCrossPlot as i32, 11);
        assert_eq!(EntityCrossPlot as i32, 12);
    }

    /// ALL 必须真的是全部：漏一条的表现是「表单里有这个开关、推规则时不推它」，
    /// 也就是服主关掉了但完全没生效。
    #[test]
    fn all_covers_every_discriminant() {
        let mut seen: Vec<i32> = super::DimensionRule::ALL
            .iter()
            .map(|r| *r as i32)
            .collect();
        seen.sort_unstable();
        assert_eq!(seen, (0..=12).collect::<Vec<_>>());
    }
}

// ─────────────────────── 地皮边界约束的数据推送 ───────────────────────

/// 一块地皮的合并标记，推给 loader 用。
///
/// `mask` 的位序和插件侧 `Plot::merged` 的下标一致：
/// `1 = 北(z-)`、`2 = 东(x+)`、`4 = 南(z+)`、`8 = 西(x-)`。
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PlotMerge {
    pub x: i32,
    pub z: i32,
    pub mask: u32,
}

impl PlotMerge {
    pub const NORTH: u32 = 1;
    pub const EAST: u32 = 2;
    pub const SOUTH: u32 = 4;
    pub const WEST: u32 = 8;

    /// 由四个方向的布尔值组装。`dirs` 的下标就是 0=N 1=E 2=S 3=W。
    pub fn from_dirs(x: i32, z: i32, dirs: [bool; 4]) -> PlotMerge {
        let mut mask = 0u32;
        for (i, on) in dirs.iter().enumerate() {
            if *on {
                mask |= 1u32 << i;
            }
        }
        PlotMerge { x, z, mask }
    }

    pub fn is_empty(&self) -> bool {
        self.mask == 0
    }
}

/// 注册（或更新）一个维度的地皮网格。
///
/// 这是 [`DimensionRule::PistonCrossPlot`] / [`DimensionRule::EntityCrossPlot`]
/// 的前提：没有网格的维度，那两条规则恒等于放行。
///
/// `plot_size <= 0` 等价于 [`clear_plot_grid`]。几何变化会清掉合并表的缓存，
/// 所以改了网格之后要重推一次 [`set_plot_merges`]。
///
/// **几何必须和世界实际使用的一致。** loader 侧用它复刻插件的 `owning_plot`；
/// 对不上不会表现成「判错一格」，而是「主人能手放方块、活塞就是推不过去」。
pub fn set_plot_grid(dimension: i32, plot_size: i32, road_width: i32) {
    unsafe { (rt().api.md_set_plot_grid)(dimension, plot_size, road_width) }
}

/// 清掉一个维度的网格和合并表。世界被删除、或者改成不用地皮模型时调。
pub fn clear_plot_grid(dimension: i32) {
    unsafe { (rt().api.md_clear_plot_grid)(dimension) }
}

/// **整表替换**一个维度的合并标记。
///
/// 只需要传真的有标记的地皮 —— 没有条目的按「四面都没合并」处理，所以几千块
/// 地皮、十来处合并的服务器，这张表也就几十个整数。
///
/// 整表替换而不是增量：增量要求两侧对「现在有哪些条目」的看法永远一致，而拆分
/// 是先清邻居再存自己、中途可能失败的。一旦对不上，增量再也没有自愈的机会。
pub fn set_plot_merges(dimension: i32, merges: &[PlotMerge]) {
    // 摊平成三元组。空表也要发 —— 「这个世界现在一处合并都没有」是有效信息，
    // 跳过发送等于让 loader 一直用着上一次的表。
    let mut flat: Vec<i32> = Vec::with_capacity(merges.len() * 3);
    for m in merges {
        if m.is_empty() {
            continue;
        }
        flat.push(m.x);
        flat.push(m.z);
        flat.push(m.mask as i32);
    }
    let count = (flat.len() / 3) as i32;
    unsafe { (rt().api.md_set_plot_merges)(dimension, flat.as_ptr(), count) }
}

#[cfg(test)]
mod plot_merge_tests {
    use super::PlotMerge;

    #[test]
    fn mask_bits_match_the_direction_indices() {
        let m = PlotMerge::from_dirs(1, 2, [true, false, true, false]);
        assert_eq!(m.mask, PlotMerge::NORTH | PlotMerge::SOUTH);
        let all = PlotMerge::from_dirs(0, 0, [true; 4]);
        assert_eq!(all.mask, 0b1111);
        assert!(PlotMerge::from_dirs(0, 0, [false; 4]).is_empty());
    }
}
