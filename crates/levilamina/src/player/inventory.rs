//! Player inventory and ender-chest access.

use super::*;
use crate::container::Container;

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
