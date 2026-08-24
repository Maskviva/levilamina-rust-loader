use super::*;
use crate::container::Container;

impl Player {
    pub fn inventory(&self) -> Container {
        Container::player_inventory(self.clone())
    }

    pub fn ender_chest(&self) -> Container {
        Container::player_ender_chest(self.clone())
    }

    pub fn armor(&self) -> Container {
        Container::player_armor(self.clone())
    }

    pub fn hands(&self) -> Container {
        Container::player_hands(self.clone())
    }

    pub fn offhand(&self) -> Result<crate::ItemStack> {
        self.hands().item(1)
    }

    pub fn set_offhand(&self, item: &crate::ItemStack) -> Result<()> {
        self.hands().set_item(1, item)
    }
}
