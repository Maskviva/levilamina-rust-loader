//! `Server` simulated-player spawning and lookup.

use super::*;
use crate::error::{Error, Result};
use crate::ffi::{collect_strs, s};
use crate::{rt, sys};

impl Server {
    /// Spawn a simulated ("fake") player and return a
    /// [`SimPlayer`](crate::sim::SimPlayer) handle — carpet-style `/self`.
    ///
    /// The bot is a real player under the hood, so the whole [`Player`] API
    /// works on it (via [`SimPlayer::player`](crate::sim::SimPlayer::player));
    /// the handle adds the `simulate*` verbs (move, mine, use, attack, …).
    /// Errors if the level isn't ready. Server thread only.
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

    /// Get a handle to an **already-existing** simulated player by name —
    /// the way to re-acquire a bot whose spawn-time handle was lost (e.g.
    /// after a server restart: the bot persists in the world, in-memory
    /// handles don't). The handle is unchecked; confirm it's a live bot with
    /// [`is_simulated`](Self::is_simulated) or find valid names via
    /// [`list_sim_players`](Self::list_sim_players). Server thread only.
    pub fn sim_player(&self, name: &str) -> crate::sim::SimPlayer {
        crate::sim::SimPlayer::by_name(name)
    }

    /// Is `name` currently a live simulated player? Backed by the same
    /// `isSimulatedPlayer()` check the action dispatcher uses. Server thread
    /// only.
    pub fn is_simulated(&self, name: &str) -> bool {
        let sel = sys::LeviRsPlayerSel {
            kind: 0,
            value: s(name),
        };
        unsafe { (rt().api.sim_is)(sel) }
    }

    /// Names of all live simulated players — enumerate bots that outlived the
    /// session that spawned them, then rebuild handles with
    /// [`sim_player`](Self::sim_player). Server thread only.
    pub fn list_sim_players(&self) -> Vec<crate::sim::SimPlayer> {
        collect_strs(|ctx, sink| unsafe { (rt().api.sim_list)(ctx, sink) })
            .into_iter()
            .map(crate::sim::SimPlayer::by_name)
            .collect()
    }
}
