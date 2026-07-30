//! Player inventory, ender chest, armour and hand slots.
//!
//! All four are real `Container`s in this engine. Armour and the hand slots
//! aren't reached through `Player` — they live on the entity's
//! `ActorEquipment` provider — but the bridge resolves them per call just like
//! the others, so the same read/write path applies.
//!
//! Everything here reads **live engine state**, never the save file. The save
//! only holds whatever was there at the last flush, so anything derived from it
//! silently loses every change the player has made since logging in.

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

    /// The armour slots, as a container.
    ///
    /// Slot order matches `ArmorSlot`: 0 head, 1 torso, 2 legs, 3 feet.
    /// Backed by `ActorEquipment::getArmorContainer` — the same entry point
    /// LegacyScriptEngine's `player.getArmor()` uses.
    pub fn armor(&self) -> Container {
        Container::player_armor(self.clone())
    }

    /// The hand slots, as a container: slot 0 is the main hand, slot 1 the
    /// offhand. Backed by `ActorEquipment::getHandContainer`.
    ///
    /// For just the offhand item, [`Player::offhand`] is the convenient form.
    pub fn hands(&self) -> Container {
        Container::player_hands(self.clone())
    }

    /// The offhand item — hand-slot 1.
    pub fn offhand(&self) -> Result<crate::ItemStack> {
        self.hands().item(1)
    }

    /// Replace the offhand item.
    pub fn set_offhand(&self, item: &crate::ItemStack) -> Result<()> {
        self.hands().set_item(1, item)
    }
}
