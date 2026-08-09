//! Read-only player queries.

use super::*;
use crate::entity::{Actor, Entity};
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

    /// 拿到这名玩家的 [`Actor`] 对象 —— [`Player::as_entity`] 的同义方法。
    ///
    /// 原生的继承链是 `Player : Mob : Actor`，也就是说位置、朝向、生命值、
    /// 药水效果、Tag、骑乘、AABB、射线检测这些**都在 `Actor` 那一层**，
    /// [`Player`] 上并没有重复一遍。要用它们，先转过去：
    ///
    /// ```no_run
    /// # use levilamina::prelude::*;
    /// # fn demo(player: &Player) -> Result<()> {
    /// let actor = player.get_actor()?;
    ///
    /// let (x, y, z) = actor.pos()?;
    /// actor.add_tag("in_arena")?;
    /// actor.add_effect("speed", 200, 1, false, true)?;
    /// # Ok(())
    /// # }
    /// ```
    ///
    /// 名字对齐 LSE / 原生 C++ 的叫法。`as_entity()` 保留，两者完全等价，
    /// 挑顺手的用。
    ///
    /// # 失败条件
    ///
    /// 玩家不在线时返回 `Err` —— [`Player`] 是选择器不是指针，每次调用都会
    /// 重新解析一遍。也因此**不要把结果缓存过一个 tick**：玩家退出重进后
    /// `ActorUniqueID` 会变，旧的 [`Actor`] 从此指向一个不存在的实体。
    /// 需要长期保存的是 [`Player`]（选择器），不是 [`Actor`]（id）。
    pub fn get_actor(&self) -> Result<Actor> {
        self.as_entity()
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

    /// The dimension the player is currently in.
    ///
    /// Vanilla dimensions are 0 (overworld), 1 (nether) and 2 (the end);
    /// dimensions registered through `more_dimensions` are >= 3, so do not
    /// assume the value is one of the three vanilla ids.
    pub fn dimension(&self) -> Result<i32> {
        self.get_num(sys::PPROP_DIMENSION).map(|v| v as i32)
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
