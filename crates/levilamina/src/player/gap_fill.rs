use super::*;
use crate::error::Result;
use crate::ffi::{call_out_str, s};
use crate::rt;

impl Player {
    pub fn carried_item(&self) -> Result<String> {
        call_out_str(|ctx, sink| unsafe {
            (rt().api.player_get_carried_item)(self.ffi_sel(), ctx, sink)
        })
        .ok_or_else(|| self.gone())
    }

    pub fn get_item(&self, slot: i32) -> Result<String> {
        call_out_str(|ctx, sink| unsafe {
            (rt().api.player_get_item)(self.ffi_sel(), slot, ctx, sink)
        })
        .ok_or_else(|| self.gone())
    }

    pub fn set_item(&self, slot: i32, item_snbt: &str) -> Result<()> {
        let ok = unsafe { (rt().api.player_set_item)(self.ffi_sel(), slot, s(item_snbt)) };
        if ok {
            Ok(())
        } else {
            Err(self.gone())
        }
    }

    pub fn equipment(&self) -> Result<String> {
        call_out_str(|ctx, sink| unsafe {
            (rt().api.player_get_equipment)(self.ffi_sel(), ctx, sink)
        })
        .ok_or_else(|| self.gone())
    }

    pub fn item_cooldown(&self, item_name: &str) -> i32 {
        unsafe { (rt().api.player_get_cooldown)(self.ffi_sel(), s(item_name)) }
    }

    pub fn start_item_cooldown(&self, item_name: &str, ticks: i32) -> Result<()> {
        let ok = unsafe { (rt().api.player_start_cooldown)(self.ffi_sel(), s(item_name), ticks) };
        if ok {
            Ok(())
        } else {
            Err(self.gone())
        }
    }

    pub fn network_status(&self) -> Result<String> {
        call_out_str(|ctx, sink| unsafe {
            (rt().api.player_get_network_status)(self.ffi_sel(), ctx, sink)
        })
        .ok_or_else(|| self.gone())
    }
}
