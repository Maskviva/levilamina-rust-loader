//! Player gap-fill API: equipment, cooldowns, network status (ABI v5 additive).

use super::*;
use crate::error::Result;
use crate::ffi::{call_out_str, s};
use crate::rt;

impl Player {
    /// The item currently being carried (main hand) as SNBT.
    pub fn carried_item(&self) -> Result<String> {
        call_out_str(|ctx, sink| unsafe {
            (rt().api.player_get_carried_item)(self.ffi_sel(), ctx, sink)
        })
        .ok_or_else(|| self.gone())
    }

    /// Read the item at `slot` (0-based inventory index) as SNBT.
    pub fn get_item(&self, slot: i32) -> Result<String> {
        call_out_str(|ctx, sink| unsafe {
            (rt().api.player_get_item)(self.ffi_sel(), slot, ctx, sink)
        })
        .ok_or_else(|| self.gone())
    }

    /// Replace the item at `slot` with an SNBT stack.
    pub fn set_item(&self, slot: i32, item_snbt: &str) -> Result<()> {
        let ok = unsafe { (rt().api.player_set_item)(self.ffi_sel(), slot, s(item_snbt)) };
        if ok {
            Ok(())
        } else {
            Err(self.gone())
        }
    }

    /// All equipment as SNBT: `[{slot, item_snbt}, …]`
    /// slot: 0=mainhand 1=offhand 2-5=armor
    pub fn equipment(&self) -> Result<String> {
        call_out_str(|ctx, sink| unsafe {
            (rt().api.player_get_equipment)(self.ffi_sel(), ctx, sink)
        })
        .ok_or_else(|| self.gone())
    }

    /// Ticks remaining for an item cooldown (-1 if not on cooldown / offline).
    pub fn item_cooldown(&self, item_name: &str) -> i32 {
        unsafe { (rt().api.player_get_cooldown)(self.ffi_sel(), s(item_name)) }
    }

    /// Start a cooldown for `item_name` lasting `ticks` ticks.
    pub fn start_item_cooldown(&self, item_name: &str, ticks: i32) -> Result<()> {
        let ok = unsafe { (rt().api.player_start_cooldown)(self.ffi_sel(), s(item_name), ticks) };
        if ok {
            Ok(())
        } else {
            Err(self.gone())
        }
    }

    /// Network status as SNBT: `{ping, avg_ping, packet_loss, avg_packet_loss, max_bps}`
    pub fn network_status(&self) -> Result<String> {
        call_out_str(|ctx, sink| unsafe {
            (rt().api.player_get_network_status)(self.ffi_sel(), ctx, sink)
        })
        .ok_or_else(|| self.gone())
    }
}
