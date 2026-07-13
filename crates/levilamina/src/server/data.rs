//! `Server` read-only data: villages, structures, player list, version info.

use super::*;
use crate::error::{Error, Result};
use crate::ffi::{call_out_str, collect_strs};
use crate::player::PlayerInfo;
use crate::{rt, sys};

impl Server {
    /// Enumerate the villages in a dimension — bounds, centre, POI count.
    ///
    /// Reads the dimension's live `VillageManager`; there is no vanilla
    /// command or event that surfaces this. Server thread only. Villages that
    /// fail to parse are skipped rather than failing the whole call.
    pub fn villages(&self, dim: i32) -> Vec<crate::world::VillageInfo> {
        collect_strs(|ctx, sink| unsafe { (rt().api.villages)(dim, ctx, sink) })
            .iter()
            .filter_map(|s| crate::world::VillageInfo::from_snbt(s))
            .collect()
    }

    /// Hardcoded spawn areas (nether fortress, witch hut, ocean monument,
    /// pillager outpost) whose chunks intersect a `radius`-block square around
    /// `(x, y, z)` — the mob-spawn overrides `/hsa` visualises.
    ///
    /// Only **loaded** chunks are inspected: a read-only query never
    /// force-loads terrain, so areas in unloaded chunks won't appear (stand
    /// near them, or increase view distance). Server thread only.
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

    /// Every online player (identity + position). Same data as
    /// [`crate::Player::list`].
    pub fn list_players(&self) -> Vec<PlayerInfo> {
        crate::player::Player::list()
    }

    /// BDS game version, e.g. `1.26.x`.
    pub fn bds_version(&self) -> Result<String> {
        call_out_str(|ctx, sink| unsafe {
            (rt().api.server_info_str)(sys::SRV_BDS_VERSION, ctx, sink)
        })
        .ok_or_else(|| Error("server_info unavailable".into()))
    }

    /// Network protocol version.
    pub fn protocol_version(&self) -> Result<i32> {
        call_out_str(|ctx, sink| unsafe {
            (rt().api.server_info_str)(sys::SRV_PROTOCOL_VERSION, ctx, sink)
        })
        .and_then(|v| v.parse().ok())
        .ok_or_else(|| Error("server_info unavailable".into()))
    }
}
