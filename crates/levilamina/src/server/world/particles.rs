use super::*;
use crate::error::{Error, Result};
use crate::ffi::s;
use crate::world::PlayerPos;
use crate::{rt, sys};

impl Server {
    pub fn spawn_particle(&self, dim: i32, effect: &str, x: f64, y: f64, z: f64) -> Result<()> {
        let ok = unsafe { (rt().api.spawn_particle)(dim, s(effect), x, y, z) };
        if ok {
            Ok(())
        } else {
            Err(Error("level/dimension not ready".into()))
        }
    }

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
