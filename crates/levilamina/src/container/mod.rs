//! Containers (decision #10): one code path for player inventories, ender
//! chests, and block containers — the bridge resolves an "owner + which"
//! reference through the engine's `Container` virtual interface per call.

use crate::error::{Error, Result};
use crate::ffi::s;
use crate::item::ItemStack;
use crate::player::Player;
use crate::{rt, sys};

#[derive(Debug, Clone)]
enum Target {
    PlayerInventory(Player),
    PlayerEnderChest(Player),
    Block { dim: i32, x: i32, y: i32, z: i32 },
}

/// A container handle. Resolved per call; never dangles.
#[derive(Debug, Clone)]
pub struct Container {
    target: Target,
}

mod ops;

impl Container {
    pub(crate) fn player_inventory(player: Player) -> Container {
        Container {
            target: Target::PlayerInventory(player),
        }
    }

    pub(crate) fn player_ender_chest(player: Player) -> Container {
        Container {
            target: Target::PlayerEnderChest(player),
        }
    }

    fn ffi_ref(&self) -> sys::LeviRsContainerRef {
        // An unused player slot still needs a well-formed (empty) selector.
        let empty = sys::LeviRsPlayerSel {
            kind: 0,
            value: s(""),
        };
        match &self.target {
            Target::PlayerInventory(p) => sys::LeviRsContainerRef {
                which: 0,
                player: p.ffi_sel(),
                dim: 0,
                x: 0,
                y: 0,
                z: 0,
            },
            Target::PlayerEnderChest(p) => sys::LeviRsContainerRef {
                which: 1,
                player: p.ffi_sel(),
                dim: 0,
                x: 0,
                y: 0,
                z: 0,
            },
            Target::Block { dim, x, y, z } => sys::LeviRsContainerRef {
                which: 4,
                player: empty,
                dim: *dim,
                x: *x,
                y: *y,
                z: *z,
            },
        }
    }

    fn gone(&self) -> Error {
        Error("container unreachable (player offline / block has no container)".into())
    }
}
