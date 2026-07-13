//! Read-only player queries.

use super::*;
use crate::entity::Entity;
use crate::error::Result;
use crate::{rt, sys};

impl Player {
    /// True if the selector currently resolves to an online player.
    pub fn is_online(&self) -> bool {
        let mut id: sys::LeviRsActorId = 0;
        unsafe { (rt().api.player_resolve)(self.ffi_sel(), &mut id) }
    }

    /// Resolve to the underlying [`Entity`] handle (ActorUniqueID) — the
    /// gateway to positions, effects, tags and the rest of the actor API.
    pub fn as_entity(&self) -> Result<Entity> {
        let mut id: sys::LeviRsActorId = 0;
        let ok = unsafe { (rt().api.player_resolve)(self.ffi_sel(), &mut id) };
        if ok {
            Ok(Entity::from_id(id))
        } else {
            Err(self.gone())
        }
    }

    pub fn real_name(&self) -> Result<String> {
        self.get_str(sys::PSTR_REAL_NAME)
    }

    pub fn uuid(&self) -> Result<String> {
        self.get_str(sys::PSTR_UUID)
    }

    pub fn xuid(&self) -> Result<String> {
        self.get_str(sys::PSTR_XUID)
    }

    pub fn ip_and_port(&self) -> Result<String> {
        self.get_str(sys::PSTR_IP_AND_PORT)
    }

    pub fn locale_code(&self) -> Result<String> {
        self.get_str(sys::PSTR_LOCALE_CODE)
    }

    /// Display name (`Actor::getNameTag`) — nameplate plugins change this.
    pub fn name_tag(&self) -> Result<String> {
        self.get_str(sys::PSTR_NAME_TAG)
    }

    /// Raw `GameType` value (0=survival 1=creative 2=adventure 6=spectator).
    pub fn game_type(&self) -> Result<i32> {
        self.get_num(sys::PPROP_GAME_TYPE).map(|v| v as i32)
    }

    pub fn level(&self) -> Result<i32> {
        self.get_num(sys::PPROP_LEVEL).map(|v| v as i32)
    }

    /// Progress toward the next level, `0.0..=1.0`.
    pub fn experience(&self) -> Result<f64> {
        self.get_num(sys::PPROP_EXPERIENCE)
    }

    pub fn hunger(&self) -> Result<f64> {
        self.get_num(sys::PPROP_HUNGER)
    }

    pub fn saturation(&self) -> Result<f64> {
        self.get_num(sys::PPROP_SATURATION)
    }

    pub fn exhaustion(&self) -> Result<f64> {
        self.get_num(sys::PPROP_EXHAUSTION)
    }

    pub fn xp_needed_for_next_level(&self) -> Result<i32> {
        self.get_num(sys::PPROP_XP_NEEDED_NEXT_LEVEL)
            .map(|v| v as i32)
    }

    pub fn luck(&self) -> Result<f64> {
        self.get_num(sys::PPROP_LUCK)
    }

    pub fn selected_slot(&self) -> Result<i32> {
        self.get_num(sys::PPROP_SELECTED_SLOT).map(|v| v as i32)
    }

    pub fn is_operator(&self) -> Result<bool> {
        self.get_num(sys::PPROP_IS_OPERATOR).map(|v| v != 0.0)
    }

    pub fn can_use_operator_blocks(&self) -> Result<bool> {
        self.get_num(sys::PPROP_CAN_USE_OPERATOR_BLOCKS)
            .map(|v| v != 0.0)
    }

    pub fn is_flying(&self) -> Result<bool> {
        self.get_num(sys::PPROP_IS_FLYING).map(|v| v != 0.0)
    }

    pub fn can_jump(&self) -> Result<bool> {
        self.get_num(sys::PPROP_CAN_JUMP).map(|v| v != 0.0)
    }

    pub fn is_emoting(&self) -> Result<bool> {
        self.get_num(sys::PPROP_IS_EMOTING).map(|v| v != 0.0)
    }

    pub fn is_in_raid(&self) -> Result<bool> {
        self.get_num(sys::PPROP_IS_IN_RAID).map(|v| v != 0.0)
    }

    pub fn is_hurt(&self) -> Result<bool> {
        self.get_num(sys::PPROP_IS_HURT).map(|v| v != 0.0)
    }

    pub fn is_scoping(&self) -> Result<bool> {
        self.get_num(sys::PPROP_IS_SCOPING).map(|v| v != 0.0)
    }

    pub fn can_sleep(&self) -> Result<bool> {
        self.get_num(sys::PPROP_CAN_SLEEP).map(|v| v != 0.0)
    }

    pub fn has_respawn_position(&self) -> Result<bool> {
        self.get_num(sys::PPROP_HAS_RESPAWN_POSITION)
            .map(|v| v != 0.0)
    }

    pub fn client_sub_id(&self) -> Result<i32> {
        self.get_num(sys::PPROP_CLIENT_SUB_ID).map(|v| v as i32)
    }
}
