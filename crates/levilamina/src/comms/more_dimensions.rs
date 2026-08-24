use crate::error::{Error, Result};
use crate::ffi::s;
use crate::rt;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(i32)]
pub enum GeneratorType {
    Overworld = 1,

    Flat = 2,

    Nether = 3,

    TheEnd = 4,

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

        assert_eq!(super::GeneratorType::from_i32(0), None);
        assert_eq!(super::GeneratorType::from_i32(6), None);
    }
}

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
    pub fn cell_size(&self) -> i32 {
        self.plot_size + self.road_width
    }

    pub fn to_snbt(&self) -> String {
        fn q(v: &str) -> String {
            v.replace(['\\', '"'], "")
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

pub fn is_available() -> bool {
    unsafe { (rt().api.md_is_available)() }
}

pub fn add_simple_dimension(name: &str, seed: u32, generator: GeneratorType) -> Result<i32> {
    let id = unsafe { (rt().api.md_add_simple_dimension)(s(name), seed, generator as i32) };
    check_id(id, name, "add_simple_dimension")
}

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

pub fn get_dimension_id(name: &str) -> Option<i32> {
    let id = unsafe { (rt().api.md_get_dimension_id)(s(name)) };
    (id >= 0).then_some(id)
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ExistingDimension {
    pub name: String,

    pub dim: i32,

    pub snbt: String,
}

pub fn list_dimensions() -> Vec<ExistingDimension> {
    let lines =
        crate::ffi::collect_strs(|ctx, sink| unsafe { (rt().api.md_list_dimensions)(ctx, sink) });
    lines
        .iter()
        .filter_map(|raw| {
            let v = crate::nbt::NbtValue::parse(raw).ok()?;
            Some(ExistingDimension {
                name: v.get("name")?.as_str()?.to_string(),
                dim: i32::try_from(v.get("dim")?.as_i64()?).ok()?,
                snbt: v
                    .get("snbt")
                    .and_then(|x| x.as_str())
                    .unwrap_or("")
                    .to_string(),
            })
        })
        .collect()
}

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

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(i32)]
pub enum DimensionRule {
    SpawnMonster = 0,

    SpawnAnimal = 1,

    SpawnSpawner = 2,

    ExplodeBlocks = 3,

    FireSpread = 4,

    MobGriefing = 5,

    Projectile = 6,

    PistonPush = 7,

    LiquidFlow = 8,

    FarmlandDecay = 9,

    Ride = 10,

    PistonCrossPlot = 11,

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

pub fn set_dimension_rule(dimension: i32, rule: DimensionRule, allow: bool) {
    unsafe { (rt().api.md_set_dimension_rule)(dimension, rule as i32, allow) }
}

pub fn get_dimension_rule(dimension: i32, rule: DimensionRule) -> Option<bool> {
    let mut out = false;
    let found = unsafe { (rt().api.md_get_dimension_rule)(dimension, rule as i32, &mut out) };
    found.then_some(out)
}

pub fn clear_dimension_rules(dimension: i32) {
    unsafe { (rt().api.md_clear_dimension_rules)(dimension) }
}

#[cfg(test)]
mod dimension_rule_tests {
    use super::DimensionRule::*;

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

pub fn set_plot_grid(dimension: i32, plot_size: i32, road_width: i32) {
    unsafe { (rt().api.md_set_plot_grid)(dimension, plot_size, road_width) }
}

pub fn clear_plot_grid(dimension: i32) {
    unsafe { (rt().api.md_clear_plot_grid)(dimension) }
}

pub fn set_plot_merges(dimension: i32, merges: &[PlotMerge]) {
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
