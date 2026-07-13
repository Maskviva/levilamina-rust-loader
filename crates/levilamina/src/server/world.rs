//! `Server` world read/write: blocks, region scan, particles, spawning, explosions.

use super::*;
use crate::entity::Entity;
use crate::error::{Error, Result};
use crate::ffi::{r, s};
use crate::world::{BlockInfo, EntityInfo, PlayerPos, Scan};
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

    /// Scan a cuboid region (corners inclusive, order-independent) into a
    /// [`Scan`]: one [`crate::ScanLayer`] per Y level, each a 2-D grid of
    /// [`crate::Cell`]s holding the block and any entities in that cell.
    /// Server thread only.
    pub fn scan_region(&self, dim: i32, a: (i32, i32, i32), b: (i32, i32, i32)) -> Result<Scan> {
        let min = (a.0.min(b.0), a.1.min(b.1), a.2.min(b.2));
        let max = (a.0.max(b.0), a.1.max(b.1), a.2.max(b.2));
        let mut scan = Scan::new(min, max);

        // The sinks push into `scan` via a raw pointer valid only for this call.
        unsafe extern "C" fn block_sink(
            ctx: *mut c_void,
            x: i32,
            y: i32,
            z: i32,
            name: sys::LeviRsStr,
            snbt: sys::LeviRsStr,
        ) {
            let scan = &mut *ctx.cast::<Scan>();
            if let Some(cell) = scan.cell_mut(x, y, z) {
                cell.block = BlockInfo {
                    name: r(name).to_owned(),
                    snbt: r(snbt).to_owned(),
                };
            }
        }
        unsafe extern "C" fn entity_sink(
            ctx: *mut c_void,
            x: i32,
            y: i32,
            z: i32,
            kind: sys::LeviRsStr,
            snbt: sys::LeviRsStr,
        ) {
            let scan = &mut *ctx.cast::<Scan>();
            if let Some(cell) = scan.cell_mut(x, y, z) {
                cell.entities.push(EntityInfo {
                    kind: r(kind).to_owned(),
                    snbt: r(snbt).to_owned(),
                });
            }
        }

        let ok = unsafe {
            (rt().api.scan_region)(
                dim,
                min.0,
                min.1,
                min.2,
                max.0,
                max.1,
                max.2,
                (&mut scan as *mut Scan).cast(),
                block_sink,
                entity_sink,
            )
        };
        if ok {
            Ok(scan)
        } else {
            Err(Error("level/dimension not ready".into()))
        }
    }

    /// Read one block: `(type_name, serialization SNBT)`. Prefer
    /// [`crate::Block::at`] for the full property surface.
    pub fn get_block(&self, dim: i32, x: i32, y: i32, z: i32) -> Result<(String, String)> {
        unsafe extern "C" fn sink(
            ctx: *mut c_void,
            _x: i32,
            _y: i32,
            _z: i32,
            name: sys::LeviRsStr,
            snbt: sys::LeviRsStr,
        ) {
            *ctx.cast::<Option<(String, String)>>() =
                Some((r(name).to_owned(), r(snbt).to_owned()));
        }
        let mut out: Option<(String, String)> = None;
        let ok = unsafe {
            (rt().api.get_block)(
                dim,
                x,
                y,
                z,
                (&mut out as *mut Option<(String, String)>).cast(),
                sink,
            )
        };
        if !ok {
            return Err(Error("get_block: level/dimension not ready".into()));
        }
        out.ok_or_else(|| Error("get_block: no result".into()))
    }

    /// Replace one block. `spec` is anything `/setblock` accepts.
    pub fn set_block(&self, dim: i32, x: i32, y: i32, z: i32, spec: &str) -> Result<()> {
        let ok = unsafe { (rt().api.set_block)(dim, x, y, z, s(spec)) };
        if ok {
            Ok(())
        } else {
            Err(Error(format!("set_block failed for '{spec}'")))
        }
    }

    /// Spawn a mob; returns its [`Entity`] handle.
    /// `type_name`: e.g. `"minecraft:zombie"`.
    pub fn spawn_mob(&self, dim: i32, type_name: &str, x: f64, y: f64, z: f64) -> Result<Entity> {
        let mut id: sys::LeviRsActorId = 0;
        let ok = unsafe { (rt().api.spawn_mob)(dim, s(type_name), x, y, z, &mut id) };
        if ok {
            Ok(Entity::from_id(id))
        } else {
            Err(Error(format!("spawn_mob('{type_name}') failed")))
        }
    }

    /// Create an explosion. `source`: optional entity credited with the blast.
    #[allow(clippy::too_many_arguments)]
    pub fn explode(
        &self,
        dim: i32,
        x: f64,
        y: f64,
        z: f64,
        radius: f32,
        source: Option<&Entity>,
        fire: bool,
        breaks_blocks: bool,
    ) -> Result<()> {
        let ok = unsafe {
            (rt().api.explode)(
                dim,
                x,
                y,
                z,
                radius,
                f32::MAX, // max_resistance: no artificial cap
                source.map(|e| e.id()).unwrap_or(0),
                fire,
                breaks_blocks,
                false,
            )
        };
        if ok {
            Ok(())
        } else {
            Err(Error("explode failed (level/dimension not ready?)".into()))
        }
    }
}
