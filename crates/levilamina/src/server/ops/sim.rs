use crate::error::{Error, Result};
use crate::ffi::{collect_strs, s};
use crate::server::*;
use crate::{rt, sys};

impl Server {
    pub fn spawn_sim_player(
        &self,
        name: &str,
        dim: i32,
        x: f64,
        y: f64,
        z: f64,
    ) -> Result<crate::sim::SimPlayer> {
        let ok = unsafe { (rt().api.sim_spawn)(s(name), dim, x, y, z) };
        if ok {
            Ok(crate::sim::SimPlayer::by_name(name))
        } else {
            Err(Error(format!(
                "failed to spawn sim player '{name}' (level not ready?)"
            )))
        }
    }

    pub fn sim_player(&self, name: &str) -> crate::sim::SimPlayer {
        crate::sim::SimPlayer::by_name(name)
    }

    pub fn is_simulated(&self, name: &str) -> bool {
        let sel = sys::LeviRsPlayerSel {
            kind: 0,
            value: s(name),
        };
        unsafe { (rt().api.sim_is)(sel) }
    }

    pub fn list_sim_players(&self) -> Vec<crate::sim::SimPlayer> {
        collect_strs(|ctx, sink| unsafe { (rt().api.sim_list)(ctx, sink) })
            .into_iter()
            .map(crate::sim::SimPlayer::by_name)
            .collect()
    }
}
