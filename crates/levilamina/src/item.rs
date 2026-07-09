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

impl ItemStack {
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

    /// The stack's serialized form (vanilla `ItemStack::save` layout).
    pub fn snbt(&self) -> &str {
        &self.snbt
    }

    /// Parse into a structured value for field-level inspection.
    pub fn to_nbt(&self) -> Result<NbtValue> {
        NbtValue::parse(&self.snbt)
    }

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

    // ── queries (engine-backed) ──

    pub fn count(&self) -> Result<u8> {
        self.get_num(sys::IPROP_COUNT).map(|v| v as u8)
    }
    pub fn max_stack_size(&self) -> Result<u8> {
        self.get_num(sys::IPROP_MAX_STACK_SIZE).map(|v| v as u8)
    }
    pub fn aux_value(&self) -> Result<i32> {
        self.get_num(sys::IPROP_AUX_VALUE).map(|v| v as i32)
    }
    /// Numeric runtime item id (not stable across versions — prefer names).
    pub fn id(&self) -> Result<i32> {
        self.get_num(sys::IPROP_ID).map(|v| v as i32)
    }
    pub fn damage(&self) -> Result<i32> {
        self.get_num(sys::IPROP_DAMAGE).map(|v| v as i32)
    }
    pub fn is_null(&self) -> Result<bool> {
        self.get_num(sys::IPROP_IS_NULL).map(|v| v != 0.0)
    }
    pub fn is_block(&self) -> Result<bool> {
        self.get_num(sys::IPROP_IS_BLOCK).map(|v| v != 0.0)
    }
    pub fn is_enchanted(&self) -> Result<bool> {
        self.get_num(sys::IPROP_IS_ENCHANTED).map(|v| v != 0.0)
    }
    pub fn is_armor(&self) -> Result<bool> {
        self.get_num(sys::IPROP_IS_ARMOR).map(|v| v != 0.0)
    }
    pub fn is_damageable(&self) -> Result<bool> {
        self.get_num(sys::IPROP_IS_DAMAGEABLE).map(|v| v != 0.0)
    }
    pub fn is_damaged(&self) -> Result<bool> {
        self.get_num(sys::IPROP_IS_DAMAGED).map(|v| v != 0.0)
    }

    /// Full type name, e.g. `minecraft:diamond_sword`.
    pub fn type_name(&self) -> Result<String> {
        self.get_str(sys::ISTR_TYPE_NAME)
    }
    /// Display name (custom name if set, else the localized item name).
    pub fn name(&self) -> Result<String> {
        self.get_str(sys::ISTR_NAME)
    }
    pub fn custom_name(&self) -> Result<String> {
        self.get_str(sys::ISTR_CUSTOM_NAME)
    }
    pub fn raw_name_id(&self) -> Result<String> {
        self.get_str(sys::ISTR_RAW_NAME_ID)
    }

    // ── transforms (rebuild → mutate → reserialize) ──

    pub fn set_custom_name(&mut self, name: &str) -> Result<()> {
        self.transform(sys::IOP_SET_CUSTOM_NAME, name, 0.0)
    }
    pub fn set_damage(&mut self, damage: i32) -> Result<()> {
        self.transform(sys::IOP_SET_DAMAGE, "", damage as f64)
    }
    pub fn set_count(&mut self, count: u8) -> Result<()> {
        self.transform(sys::IOP_SET_COUNT, "", count as f64)
    }
    pub fn set_lore(&mut self, lines: &[&str]) -> Result<()> {
        let mut wrap = NbtValue::compound();
        wrap.insert(
            "lore",
            NbtValue::List(
                lines
                    .iter()
                    .map(|l| NbtValue::String((*l).to_owned()))
                    .collect(),
            ),
        );
        self.transform(sys::IOP_SET_LORE, &wrap.to_snbt(), 0.0)
    }
}
