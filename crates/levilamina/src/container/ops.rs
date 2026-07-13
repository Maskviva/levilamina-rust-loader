//! Container operations: read and modify slots.

use super::*;
use crate::error::{Error, Result};
use crate::item::ItemStack;
use crate::rt;

impl Container {
    /// The container inside the block at `(x, y, z)` — chest, barrel,
    /// furnace, hopper, dispenser, …. Fails at call time if that block has
    /// no container.
    pub fn block(dimension: i32, x: i32, y: i32, z: i32) -> Container {
        Container {
            target: Target::Block {
                dim: dimension,
                x,
                y,
                z,
            },
        }
    }

    /// Number of slots.
    pub fn size(&self) -> Result<i32> {
        let mut out = 0i32;
        let ok = unsafe { (rt().api.container_size)(self.ffi_ref(), &mut out) };
        if ok {
            Ok(out)
        } else {
            Err(self.gone())
        }
    }

    /// The item in `slot` (empty stacks come back as `is_null() == true`).
    pub fn item(&self, slot: i32) -> Result<ItemStack> {
        crate::ffi::call_out_str(|ctx, sink| unsafe {
            (rt().api.container_get_item)(self.ffi_ref(), slot, ctx, sink)
        })
        .map(ItemStack::from_snbt)
        .ok_or_else(|| Error(format!("container_get_item failed for slot {slot}")))
    }

    /// Every slot in order. Convenience over `size` + `item`.
    pub fn items(&self) -> Result<Vec<ItemStack>> {
        let n = self.size()?;
        (0..n).map(|i| self.item(i)).collect()
    }

    pub fn set_item(&self, slot: i32, item: &ItemStack) -> Result<()> {
        let ok = unsafe { (rt().api.container_set_item)(self.ffi_ref(), slot, s(item.snbt())) };
        if ok {
            Ok(())
        } else {
            Err(Error(format!("container_set_item failed for slot {slot}")))
        }
    }

    /// Add wherever it fits; `Ok(false)` when the container is full.
    pub fn add_item(&self, item: &ItemStack) -> Result<bool> {
        // The bridge's false covers both "container gone" and "didn't fit";
        // disambiguate with a size probe so callers get an honest Ok(false).
        let ok = unsafe { (rt().api.container_add_item)(self.ffi_ref(), s(item.snbt())) };
        if ok {
            return Ok(true);
        }
        if self.size().is_ok() {
            Ok(false)
        } else {
            Err(self.gone())
        }
    }

    /// Remove up to `count` items from `slot`.
    pub fn remove_item(&self, slot: i32, count: i32) -> Result<()> {
        let ok = unsafe { (rt().api.container_remove_item)(self.ffi_ref(), slot, count) };
        if ok {
            Ok(())
        } else {
            Err(Error(format!(
                "container_remove_item failed for slot {slot}"
            )))
        }
    }

    /// Empty every slot.
    pub fn clear(&self) -> Result<()> {
        let ok = unsafe { (rt().api.container_clear)(self.ffi_ref()) };
        if ok {
            Ok(())
        } else {
            Err(self.gone())
        }
    }
}
