//! Item stacks as pure SNBT value objects (decision #5): an [`ItemStack`]
//! owns its serialized form; queries and transforms round-trip through the
//! engine's `ItemStack::fromTag` / `save`, so game logic (max stack size,
//! enchantments, durability rules) always comes from the engine, never from
//! a Rust re-implementation.

use crate::error::{Error, Result};
use crate::ffi::s;
use crate::nbt::NbtValue;
use crate::{rt, sys};

/// An item stack value object. Cloning clones the value; nothing is shared
/// with the game until you hand it to a container or a player.
#[derive(Debug, Clone, PartialEq)]
pub struct ItemStack {
    snbt: String,
}

mod query;

impl ItemStack {
    fn bad(&self) -> Error {
        Error("item SNBT did not rebuild into a valid ItemStack".into())
    }

    fn get_num(&self, prop: i32) -> Result<f64> {
        let mut out = 0.0f64;
        let ok = unsafe { (rt().api.item_get_num)(s(&self.snbt), prop, &mut out) };
        if ok {
            Ok(out)
        } else {
            Err(self.bad())
        }
    }

    fn get_str(&self, prop: i32) -> Result<String> {
        crate::ffi::call_out_str(|ctx, sink| unsafe {
            (rt().api.item_get_str)(s(&self.snbt), prop, ctx, sink)
        })
        .ok_or_else(|| self.bad())
    }

    fn transform(&mut self, op: i32, sarg: &str, narg: f64) -> Result<()> {
        let new_snbt = crate::ffi::call_out_str(|ctx, sink| unsafe {
            (rt().api.item_transform)(s(&self.snbt), op, s(sarg), narg, ctx, sink)
        })
        .ok_or_else(|| Error("item transform failed".into()))?;
        self.snbt = new_snbt;
        Ok(())
    }

    /// Wrap existing item SNBT (from a container read, an event payload, an
    /// entity snapshot's `Item` field, …).
    pub fn from_snbt(snbt: impl Into<String>) -> ItemStack {
        ItemStack { snbt: snbt.into() }
    }

    /// Build a fresh stack: `ItemStack::create("minecraft:apple", 3)`.
    pub fn create(type_name: &str, count: u8) -> ItemStack {
        let mut v = NbtValue::compound();
        v.insert("Name", NbtValue::String(type_name.to_owned()));
        v.insert("Count", NbtValue::Byte(count as i8));
        v.insert("Damage", NbtValue::Short(0));
        v.insert("WasPickedUp", NbtValue::Byte(0));
        ItemStack { snbt: v.to_snbt() }
    }

    /// The canonical empty stack.
    pub fn empty() -> ItemStack {
        ItemStack::create("", 0)
    }
}
