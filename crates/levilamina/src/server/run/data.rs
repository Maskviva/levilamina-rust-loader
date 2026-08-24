use crate::error::{Error, Result};
use crate::ffi::{call_out_str, collect_strs};
use crate::player::PlayerInfo;
use crate::server::*;
use crate::{rt, sys};

impl Server {
    pub fn villages(&self, dim: i32) -> Vec<crate::world::VillageInfo> {
        collect_strs(|ctx, sink| unsafe { (rt().api.villages)(dim, ctx, sink) })
            .iter()
            .filter_map(|s| crate::world::VillageInfo::from_snbt(s))
            .collect()
    }

    pub fn structures_near(
        &self,
        dim: i32,
        x: i32,
        y: i32,
        z: i32,
        radius: i32,
    ) -> Vec<crate::world::StructureInfo> {
        collect_strs(|ctx, sink| unsafe {
            (rt().api.structures_near)(dim, x, y, z, radius, ctx, sink)
        })
        .iter()
        .filter_map(|s| crate::world::StructureInfo::from_snbt(s))
        .collect()
    }

    pub fn list_players(&self) -> Vec<PlayerInfo> {
        crate::player::Player::list()
    }

    pub fn bds_version(&self) -> Result<String> {
        call_out_str(|ctx, sink| unsafe {
            (rt().api.server_info_str)(sys::SRV_BDS_VERSION, ctx, sink)
        })
        .ok_or_else(|| Error("server_info unavailable".into()))
    }

    pub fn protocol_version(&self) -> Result<i32> {
        call_out_str(|ctx, sink| unsafe {
            (rt().api.server_info_str)(sys::SRV_PROTOCOL_VERSION, ctx, sink)
        })
        .and_then(|v| v.parse().ok())
        .ok_or_else(|| Error("server_info unavailable".into()))
    }
}
