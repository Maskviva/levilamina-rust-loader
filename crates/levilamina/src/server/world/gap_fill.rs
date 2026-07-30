//! Level gap-fill API: biome, spawn, save, weather, path, sleep (v5).

use super::*;
use crate::entity::Entity;
use crate::error::{Error, Result};
use crate::ffi::call_out_str;
use crate::rt;
use crate::types::PositionI32;

impl Server {
    /// Biome name at `(x, y, z)` in `dim`, or `Err` if the chunk is unloaded.
    pub fn get_biome(&self, dim: i32, x: i32, y: i32, z: i32) -> Result<String> {
        call_out_str(|ctx, sink| unsafe { (rt().api.level_get_biome)(dim, x, y, z, ctx, sink) })
            .ok_or_else(|| Error("biome lookup failed (chunk unloaded?)".into()))
    }

    /// The world's default spawn point.
    pub fn default_spawn(&self) -> Result<PositionI32> {
        let (mut x, mut y, mut z) = (0i32, 0i32, 0i32);
        let ok = unsafe { (rt().api.level_get_default_spawn)(&mut x, &mut y, &mut z) };
        if ok {
            Ok((x, y, z))
        } else {
            Err(Error("level not ready".into()))
        }
    }

    /// Set the world's default spawn point.
    pub fn set_default_spawn(&self, x: i32, y: i32, z: i32) -> Result<()> {
        let ok = unsafe { (rt().api.level_set_default_spawn)(x, y, z) };
        if ok {
            Ok(())
        } else {
            Err(Error("level not ready".into()))
        }
    }

    /// Save all loaded chunks and player data.
    pub fn save_level(&self) -> Result<()> {
        let ok = unsafe { (rt().api.level_save)() };
        if ok {
            Ok(())
        } else {
            Err(Error("level save failed".into()))
        }
    }

    /// Sleep status as SNBT: `{sleeping, total_players, active_sleeping}`
    pub fn sleep_status(&self) -> Result<String> {
        call_out_str(|ctx, sink| unsafe { (rt().api.level_get_sleep_status)(ctx, sink) })
            .ok_or_else(|| Error("level not ready".into()))
    }

    /// Update weather levels. Set rain/lightning to 0 and times to 0 for clear.
    pub fn update_weather(
        &self,
        rain_level: f32,
        rain_time: i32,
        lightning_level: f32,
        lightning_time: i32,
    ) -> Result<()> {
        let ok = unsafe {
            (rt().api.level_update_weather)(rain_level, rain_time, lightning_level, lightning_time)
        };
        if ok {
            Ok(())
        } else {
            Err(Error("level not ready".into()))
        }
    }

    /// Find a path from `entity` to `(x, y, z)` as SNBT:
    /// `{nodes:[{x,y,z}, …], reached:1b/0b}`
    pub fn find_path(&self, entity: Entity, x: i32, y: i32, z: i32) -> Result<String> {
        call_out_str(|ctx, sink| unsafe {
            (rt().api.level_find_path)(entity.id(), x, y, z, ctx, sink)
        })
        .ok_or_else(|| Error("pathfinding failed (entity gone or unsupported)".into()))
    }
}
