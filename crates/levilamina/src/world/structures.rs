//! Read-only world-data types from `Server::villages` / `structures_near`:
//! [`VillageInfo`] and [`StructureInfo`], with SNBT parsing helpers.

use crate::types::PositionF64;
use super::*;

/// One village, from [`Server::villages`](crate::server::Server::villages).
#[derive(Debug, Clone, PartialEq)]
pub struct VillageInfo {
    /// Stable village UUID string.
    pub uuid: String,
    /// Approximate village centre.
    pub center: PositionF64,
    /// Village bounding box.
    pub bounds: Bounds,
    /// Number of points of interest (claimed + unclaimed) tracked by the
    /// village — a stable proxy for village size / dweller capacity.
    pub poi_count: u64,
}

/// One hardcoded spawn area (HSA), from
/// [`Server::structures_near`](crate::server::Server::structures_near).
#[derive(Debug, Clone, PartialEq)]
pub struct StructureInfo {
    /// Structure kind: `nether_fortress`, `witch_hut`, `ocean_monument`,
    /// `pillager_outpost`, or `village_deprecated`.
    pub kind: String,
    /// The spawn-override box.
    pub bounds: Bounds,
}

fn triple_i64(v: &crate::nbt::NbtValue) -> Option<PositionI32> {
    let l = v.as_list()?;
    Some((
        l.first()?.as_i64()? as i32,
        l.get(1)?.as_i64()? as i32,
        l.get(2)?.as_i64()? as i32,
    ))
}

fn triple_f64(v: &crate::nbt::NbtValue) -> Option<PositionF64> {
    let l = v.as_list()?;
    Some((
        l.first()?.as_f64()?,
        l.get(1)?.as_f64()?,
        l.get(2)?.as_f64()?,
    ))
}

fn parse_bounds(v: &crate::nbt::NbtValue) -> Option<Bounds> {
    Some(Bounds {
        min: triple_i64(v.get("min")?)?,
        max: triple_i64(v.get("max")?)?,
    })
}

impl VillageInfo {
    pub(crate) fn from_snbt(snbt: &str) -> Option<VillageInfo> {
        let v = crate::nbt::NbtValue::parse(snbt).ok()?;
        Some(VillageInfo {
            uuid: v.get("uuid")?.as_str()?.to_string(),
            center: triple_f64(v.get("center")?)?,
            bounds: parse_bounds(v.get("bounds")?)?,
            poi_count: v.get("poi_count")?.as_i64()? as u64,
        })
    }
}

impl StructureInfo {
    pub(crate) fn from_snbt(snbt: &str) -> Option<StructureInfo> {
        let v = crate::nbt::NbtValue::parse(snbt).ok()?;
        Some(StructureInfo {
            kind: v.get("type")?.as_str()?.to_string(),
            bounds: parse_bounds(v.get("bounds")?)?,
        })
    }
}
