//! Item queries and property setters.

use super::*;
use crate::error::Result;
use crate::nbt::NbtValue;
use crate::sys;

impl ItemStack {
    /// The stack's serialized form (vanilla `ItemStack::save` layout).
    pub fn snbt(&self) -> &str {
        &self.snbt
    }

    /// Parse into a structured value for field-level inspection.
    pub fn to_nbt(&self) -> Result<NbtValue> {
        NbtValue::parse(&self.snbt)
    }

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
