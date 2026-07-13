//! Player inventory and ender-chest access.

use super::*;
use crate::container::Container;
use crate::error::{Error, Result};
use crate::ffi::{call_out_str, collect_strs, s};
use crate::nbt::NbtValue;
use crate::{rt, sys};

impl Player {
    /// The player's main inventory.
    pub fn inventory(&self) -> Container {
        Container::player_inventory(self.clone())
    }

    /// The player's ender chest.
    pub fn ender_chest(&self) -> Container {
        Container::player_ender_chest(self.clone())
    }
}
