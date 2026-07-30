//! Item gap-fill API: enchants, matching, user data (ABI v5 additive).

use super::*;
use crate::error::{Error, Result};
use crate::ffi::{call_out_str, s};
use crate::rt;

impl ItemStack {
    /// Enchantments as SNBT: `[{id, level}, …]`
    pub fn enchants(&self) -> Result<String> {
        call_out_str(|ctx, sink| unsafe { (rt().api.item_get_enchants)(s(&self.snbt), ctx, sink) })
            .ok_or_else(|| self.bad())
    }

    /// Replace enchantments; `enchants_snbt = [{id, level}, …]`.
    /// Returns a new `ItemStack` with the updated enchantments.
    pub fn with_enchants(&self, enchants_snbt: &str) -> Result<ItemStack> {
        let new_snbt = call_out_str(|ctx, sink| unsafe {
            (rt().api.item_set_enchants)(s(&self.snbt), s(enchants_snbt), ctx, sink)
        })
        .ok_or_else(|| Error("item_set_enchants failed".into()))?;
        Ok(ItemStack { snbt: new_snbt })
    }

    /// True if two stacks match (type + data + components, ignoring count).
    pub fn matches(&self, other: &ItemStack) -> bool {
        unsafe { (rt().api.item_matches)(s(&self.snbt), s(&other.snbt)) }
    }

    /// Custom user-data tag (NBT) attached to this stack, if any.
    pub fn user_data(&self) -> Result<String> {
        call_out_str(|ctx, sink| unsafe { (rt().api.item_get_user_data)(s(&self.snbt), ctx, sink) })
            .ok_or_else(|| self.bad())
    }
}
