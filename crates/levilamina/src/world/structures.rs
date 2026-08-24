use super::*;
use crate::types::PositionF64;

#[derive(Debug, Clone, PartialEq)]
pub struct VillageInfo {
    pub uuid: String,

    pub center: PositionF64,

    pub bounds: Bounds,

    pub poi_count: u64,
}

#[derive(Debug, Clone, PartialEq)]
pub struct StructureInfo {
    pub kind: String,

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
