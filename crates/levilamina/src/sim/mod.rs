//! Simulated ("fake") players — carpet-style `/self` bots.
//!
//! [`Server::spawn_sim_player`](crate::server::Server::spawn_sim_player)
//! creates one and hands back a [`SimPlayer`]. Under the hood it is a real
//! `ServerPlayer` with that name, so the whole existing [`Player`] API
//! (teleport, health, inventory, position, kick, …) works on it — reach it
//! via [`SimPlayer::player`]. This type only adds the `simulate*` verbs.
//!
//! All methods are server-thread only, like the rest of the SDK. Actions on
//! a despawned/offline sim player return `Err`. The verb layer is
//! deliberately multiplexed over a single ABI entry, so new verbs appear
//! bridge-side without an ABI bump.

use crate::error::{Error, Result};
use crate::ffi::s;
use crate::player::Player;
use crate::{rt, sys};

/// Handle to a simulated player (see module docs).
#[derive(Debug, Clone)]
pub struct SimPlayer {
    name: String,
}

mod actions;

impl SimPlayer {
    fn sel(&self) -> sys::LeviRsPlayerSel {
        sys::LeviRsPlayerSel {
            kind: 0,
            value: s(&self.name),
        }
    }

    fn act(&self, verb: &str, args: &str) -> Result<()> {
        let ok = unsafe { (rt().api.sim_do)(self.sel(), s(verb), s(args)) };
        if ok {
            Ok(())
        } else {
            Err(Error(format!(
                "sim '{}': '{verb}' rejected (offline/despawned, bad args, or unsupported)",
                self.name
            )))
        }
    }

    fn pos_args(x: f64, y: f64, z: f64, extra: &[(&str, f64)]) -> String {
        let mut out = format!("{{\"x\":{x},\"y\":{y},\"z\":{z}");
        for (k, v) in extra {
            out.push_str(&format!(",\"{k}\":{v}"));
        }
        out.push('}');
        out
    }

    /// Rebuild a handle for an **already-existing** simulated player by name —
    /// e.g. after a server restart, when the bot persists in the world but the
    /// handle from [`Server::spawn_sim_player`](crate::server::Server::spawn_sim_player)
    /// is gone. Prefer [`Server::sim_player`](crate::server::Server::sim_player),
    /// which reads the same. No validation here: the name may not be a live
    /// bot — check with [`Server::is_simulated`](crate::server::Server::is_simulated)
    /// or enumerate via [`Server::list_sim_players`](crate::server::Server::list_sim_players).
    pub fn by_name(name: impl Into<String>) -> SimPlayer {
        SimPlayer { name: name.into() }
    }

    /// The bot's player name (how every per-player API addresses it).
    pub fn name(&self) -> &str {
        &self.name
    }

    /// The ordinary [`Player`] handle for this bot — health, inventory,
    /// position, teleport and the rest of the player surface live there.
    pub fn player(&self) -> Player {
        Player::by_name(&self.name)
    }
}
