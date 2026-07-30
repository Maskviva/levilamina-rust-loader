//! Particle effects and player position lookups.

use super::*;
use crate::error::{Error, Result};
use crate::ffi::s;
use crate::world::PlayerPos;
use crate::{rt, sys};

impl Server {
    /// Spawn a particle effect at a world coordinate. Server thread only.
    /// `dim`: 0 = overworld, 1 = nether, 2 = the end.
    pub fn spawn_particle(&self, dim: i32, effect: &str, x: f64, y: f64, z: f64) -> Result<()> {
        let ok = unsafe { (rt().api.spawn_particle)(dim, s(effect), x, y, z) };
        if ok {
            Ok(())
        } else {
            Err(Error("level/dimension not ready".into()))
        }
    }

    /// Spawn a particle effect **visible only to one player** (by name).
    ///
    /// Unlike [`spawn_particle`](Self::spawn_particle), which broadcasts to
    /// every client in the dimension, this sends a single
    /// `SpawnParticleEffectPacket` to that player's connection — nobody else
    /// receives it. Ideal for personal visual toggles (chunk outlines,
    /// region previews, CUI boxes).
    ///
    /// `dim` is the dimension the coordinates refer to (normally the
    /// player's own — clients don't render particles for another dimension).
    /// Errors if the player is offline. Server thread only.
    ///
    /// Requires a loader with the `spawn_particle_for` ABI slot (additive,
    /// `struct_size`-gated: a mod built against this crate refuses to load
    /// on older loaders, so reaching this call implies the slot exists).
    pub fn spawn_particle_for(
        &self,
        player: &str,
        dim: i32,
        effect: &str,
        x: f64,
        y: f64,
        z: f64,
    ) -> Result<()> {
        let sel = sys::LeviRsPlayerSel {
            kind: 0,
            value: s(player),
        };
        let ok = unsafe { (rt().api.spawn_particle_for)(sel, dim, s(effect), x, y, z) };
        if ok {
            Ok(())
        } else {
            Err(Error(format!("player not online: {player}")))
        }
    }

    /// Feet position + dimension of a connected player, by name.
    /// Returns `None` if no such player is online. Server thread only.
    pub fn player_position(&self, name: &str) -> Option<PlayerPos> {
        let p = unsafe { (rt().api.get_player_position)(s(name)) };
        if p.found {
            Some(PlayerPos {
                x: p.x,
                y: p.y,
                z: p.z,
                dim: p.dimension,
            })
        } else {
            None
        }
    }
}
