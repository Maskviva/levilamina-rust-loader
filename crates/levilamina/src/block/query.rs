//! Read-only block queries.

use super::*;
use crate::error::{Error, Result};
use crate::ffi::{call_out_str, s};
use crate::nbt::NbtValue;
use crate::{rt, sys};

impl Block {
    /// Full type name, e.g. `minecraft:grass_block`.
    pub fn type_name(&self) -> Result<String> {
        self.get_str(sys::BSTR_TYPE_NAME)
    }

    /// Full serialization `{name, states, version}` as a structured value.
    pub fn to_nbt(&self) -> Result<NbtValue> {
        NbtValue::parse(&self.get_str(sys::BSTR_SNBT)?)
    }

    /// Raw serialization SNBT.
    pub fn snbt(&self) -> Result<String> {
        self.get_str(sys::BSTR_SNBT)
    }

    pub fn description_id(&self) -> Result<String> {
        self.get_str(sys::BSTR_DESCRIPTION_ID)
    }

    pub fn debug_string(&self) -> Result<String> {
        self.get_str(sys::BSTR_DEBUG_STRING)
    }

    /// Engine block tags (e.g. `minecraft:is_axe_item_destructible`).
    pub fn tags(&self) -> Result<Vec<String>> {
        let raw = self.get_str(sys::BSTR_TAGS)?;
        let v = NbtValue::parse(&raw)?;
        Ok(v.as_list()
            .map(|items| {
                items
                    .iter()
                    .filter_map(|t| t.as_str().map(str::to_owned))
                    .collect()
            })
            .unwrap_or_default())
    }

    pub fn has_tag(&self, tag: &str) -> Result<bool> {
        let mut out: Option<String> = None;
        let ok = unsafe {
            (rt().api.block_action)(
                self.dim,
                self.x,
                self.y,
                self.z,
                sys::BACT_HAS_TAG,
                crate::ffi::s(tag),
                (&mut out as *mut Option<String>).cast(),
                crate::ffi::set_string,
            )
        };
        if ok {
            Ok(out.as_deref() == Some("1"))
        } else {
            Err(self.unreachable())
        }
    }

    pub fn is_air(&self) -> Result<bool> {
        self.get_num(sys::BPROP_IS_AIR).map(|v| v != 0.0)
    }

    /// Legacy data value (block variant).
    pub fn data(&self) -> Result<i32> {
        self.get_num(sys::BPROP_DATA).map(|v| v as i32)
    }

    pub fn block_item_id(&self) -> Result<i32> {
        self.get_num(sys::BPROP_BLOCK_ITEM_ID).map(|v| v as i32)
    }

    pub fn is_crafting_block(&self) -> Result<bool> {
        self.get_num(sys::BPROP_IS_CRAFTING_BLOCK).map(|v| v != 0.0)
    }

    pub fn is_interactive_block(&self) -> Result<bool> {
        self.get_num(sys::BPROP_IS_INTERACTIVE_BLOCK)
            .map(|v| v != 0.0)
    }

    pub fn has_block_entity(&self) -> Result<bool> {
        self.get_num(sys::BPROP_HAS_BLOCK_ENTITY).map(|v| v != 0.0)
    }

    /// The block entity's saved NBT (chest contents, sign text, …), if any.
    pub fn block_entity(&self) -> Result<Option<NbtValue>> {
        let snbt = call_out_str(|ctx, sink| unsafe {
            (rt().api.block_entity_snbt)(self.dim, self.x, self.y, self.z, ctx, sink)
        });
        match snbt {
            Some(text) => Ok(Some(NbtValue::parse(&text)?)),
            None => Ok(None), // no block entity here (or unreachable)
        }
    }
}
