//! Player handles: selectors (name / xuid / uuid) resolved against the live
//! player list on every call — never cached pointers.

use crate::container::Container;
use crate::entity::Entity;
use crate::error::{Error, Result};
use crate::ffi::{call_out_str, collect_strs, s};
use crate::item::ItemStack;
use crate::nbt::NbtValue;
use crate::{rt, sys};

/// A summary line from [`Player::list`]: identity + position.
#[derive(Debug, Clone, Default)]
pub struct PlayerInfo {
    pub name: String,
    pub xuid: String,
    pub uuid: String,
    pub dimension: i32,
    pub pos: (f64, f64, f64),
}

/// Which ability slots [`Player::set_ability`] speaks. Raw values mirror
/// `AbilitiesIndex` in the engine.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Ability {
    Build = 0,
    Invulnerable = 8,
    Flying = 9,
    MayFly = 10,
    WorldBuilder = 16,
}

/// Game mode raw values as `/gamemode` understands them.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum GameMode {
    Survival = 0,
    Creative = 1,
    Adventure = 2,
    Spectator = 6,
}

#[derive(Debug, Clone)]
enum Selector {
    Name(String),
    Xuid(String),
    Uuid(String),
}

/// A player handle. Cheap to clone; resolved on every call, so it can never
/// dangle — calls after the player leaves simply return `Err`.
#[derive(Debug, Clone)]
pub struct Player {
    sel: Selector,
}

impl Player {
    // ── constructors ──

    /// By account name (exact `getRealName()` match, falling back to the
    /// display name). Prefer [`Player::by_xuid`] for long-lived storage.
    pub fn by_name(name: impl Into<String>) -> Player {
        Player {
            sel: Selector::Name(name.into()),
        }
    }

    /// Alias for [`Player::by_name`], matching the docs' `Player::get`.
    pub fn get(name: impl Into<String>) -> Player {
        Player::by_name(name)
    }

    pub fn by_xuid(xuid: impl Into<String>) -> Player {
        Player {
            sel: Selector::Xuid(xuid.into()),
        }
    }

    pub fn by_uuid(uuid: impl Into<String>) -> Player {
        Player {
            sel: Selector::Uuid(uuid.into()),
        }
    }

    /// Every online player: identity + position.
    pub fn list() -> Vec<PlayerInfo> {
        let lines = collect_strs(|ctx, sink| unsafe { (rt().api.list_players)(ctx, sink) });
        lines
            .iter()
            .filter_map(|line| {
                let v = NbtValue::parse(line).ok()?;
                Some(PlayerInfo {
                    name: v.get("name")?.as_str()?.to_owned(),
                    xuid: v
                        .get("xuid")
                        .and_then(|x| x.as_str())
                        .unwrap_or("")
                        .to_owned(),
                    uuid: v
                        .get("uuid")
                        .and_then(|x| x.as_str())
                        .unwrap_or("")
                        .to_owned(),
                    dimension: v.get("dim").and_then(|x| x.as_i64()).unwrap_or(0) as i32,
                    pos: (
                        v.get("x").and_then(|x| x.as_f64()).unwrap_or(0.0),
                        v.get("y").and_then(|x| x.as_f64()).unwrap_or(0.0),
                        v.get("z").and_then(|x| x.as_f64()).unwrap_or(0.0),
                    ),
                })
            })
            .collect()
    }

    /// `sendMessage` to every online player.
    pub fn broadcast(msg: &str) {
        unsafe { (rt().api.broadcast_message)(s(msg)) }
    }

    // ── plumbing ──

    pub(crate) fn ffi_sel(&self) -> sys::LeviRsPlayerSel {
        let (kind, value) = match &self.sel {
            Selector::Name(v) => (0, v.as_str()),
            Selector::Xuid(v) => (1, v.as_str()),
            Selector::Uuid(v) => (2, v.as_str()),
        };
        sys::LeviRsPlayerSel {
            kind,
            value: s(value),
        }
    }

    fn gone(&self) -> Error {
        Error(format!("player not online: {:?}", self.sel))
    }

    fn get_num(&self, prop: i32) -> Result<f64> {
        let mut out = 0.0f64;
        let ok = unsafe { (rt().api.player_get_num)(self.ffi_sel(), prop, &mut out) };
        if ok {
            Ok(out)
        } else {
            Err(self.gone())
        }
    }

    fn get_str(&self, prop: i32) -> Result<String> {
        call_out_str(|ctx, sink| unsafe {
            (rt().api.player_get_str)(self.ffi_sel(), prop, ctx, sink)
        })
        .ok_or_else(|| self.gone())
    }

    fn set_num(&self, prop: i32, v: f64) -> Result<()> {
        let ok = unsafe { (rt().api.player_set_num)(self.ffi_sel(), prop, v) };
        if ok {
            Ok(())
        } else {
            Err(Error("player offline or property not settable".into()))
        }
    }

    fn action(&self, action: i32, sarg: &str, a: f64, b: f64, c: f64) -> Result<Option<String>> {
        let mut out: Option<String> = None;
        let ok = unsafe {
            (rt().api.player_action)(
                self.ffi_sel(),
                action,
                s(sarg),
                a,
                b,
                c,
                (&mut out as *mut Option<String>).cast(),
                crate::ffi::set_string,
            )
        };
        if ok {
            Ok(out)
        } else {
            Err(Error("player offline or action unsupported".into()))
        }
    }

    // ── identity & presence ──

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

    // ── numeric properties ──

    /// Raw `GameType` value (0=survival 1=creative 2=adventure 6=spectator).
    pub fn game_type(&self) -> Result<i32> {
        self.get_num(sys::PPROP_GAME_TYPE).map(|v| v as i32)
    }
    pub fn level(&self) -> Result<i32> {
        self.get_num(sys::PPROP_LEVEL).map(|v| v as i32)
    }
    pub fn set_level(&self, level: i32) -> Result<()> {
        self.set_num(sys::PPROP_LEVEL, level as f64)
    }
    /// Progress toward the next level, `0.0..=1.0`.
    pub fn experience(&self) -> Result<f64> {
        self.get_num(sys::PPROP_EXPERIENCE)
    }
    pub fn set_experience(&self, progress: f64) -> Result<()> {
        self.set_num(sys::PPROP_EXPERIENCE, progress)
    }
    pub fn hunger(&self) -> Result<f64> {
        self.get_num(sys::PPROP_HUNGER)
    }
    pub fn set_hunger(&self, v: f64) -> Result<()> {
        self.set_num(sys::PPROP_HUNGER, v)
    }
    pub fn saturation(&self) -> Result<f64> {
        self.get_num(sys::PPROP_SATURATION)
    }
    pub fn set_saturation(&self, v: f64) -> Result<()> {
        self.set_num(sys::PPROP_SATURATION, v)
    }
    pub fn exhaustion(&self) -> Result<f64> {
        self.get_num(sys::PPROP_EXHAUSTION)
    }
    pub fn set_exhaustion(&self, v: f64) -> Result<()> {
        self.set_num(sys::PPROP_EXHAUSTION, v)
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

    // ── actions ──

    pub fn send_message(&self, msg: &str) -> Result<()> {
        let ok = unsafe { (rt().api.player_send_message)(self.ffi_sel(), s(msg)) };
        if ok {
            Ok(())
        } else {
            Err(self.gone())
        }
    }

    pub fn disconnect(&self, reason: &str) -> Result<()> {
        let ok = unsafe { (rt().api.player_disconnect)(self.ffi_sel(), s(reason)) };
        if ok {
            Ok(())
        } else {
            Err(self.gone())
        }
    }

    pub fn set_gamemode(&self, mode: GameMode) -> Result<()> {
        let ok = unsafe { (rt().api.player_set_gamemode)(self.ffi_sel(), mode as i32) };
        if ok {
            Ok(())
        } else {
            Err(self.gone())
        }
    }

    pub fn teleport(&self, dimension: i32, x: f64, y: f64, z: f64) -> Result<()> {
        let ok = unsafe { (rt().api.player_teleport)(self.ffi_sel(), dimension, x, y, z) };
        if ok {
            Ok(())
        } else {
            Err(Error(
                "teleport failed (player offline / bad dimension?)".into(),
            ))
        }
    }

    pub fn set_ability(&self, ability: Ability, value: bool) -> Result<()> {
        self.action(
            sys::PACT_SET_ABILITY,
            "",
            ability as i32 as f64,
            if value { 1.0 } else { 0.0 },
            0.0,
        )
        .map(|_| ())
    }

    /// Set an ability by raw `AbilitiesIndex` value (for slots not covered
    /// by the [`Ability`] enum).
    pub fn set_ability_raw(&self, index: i32, value: bool) -> Result<()> {
        self.action(
            sys::PACT_SET_ABILITY,
            "",
            index as f64,
            if value { 1.0 } else { 0.0 },
            0.0,
        )
        .map(|_| ())
    }

    pub fn can_use_ability(&self, ability: Ability) -> Result<bool> {
        let out = self.action(
            sys::PACT_CAN_USE_ABILITY,
            "",
            ability as i32 as f64,
            0.0,
            0.0,
        )?;
        Ok(out.as_deref() == Some("1"))
    }

    pub fn set_selected_slot(&self, slot: i32) -> Result<()> {
        self.action(sys::PACT_SET_SELECTED_SLOT, "", slot as f64, 0.0, 0.0)
            .map(|_| ())
    }

    /// Give an item (added to the inventory, refreshed to the client).
    pub fn give_item(&self, item: &ItemStack) -> Result<()> {
        self.action(sys::PACT_GIVE_ITEM, item.snbt(), 0.0, 0.0, 0.0)
            .map(|_| ())
    }

    pub fn set_spawn_point(&self, dimension: i32, x: i32, y: i32, z: i32) -> Result<()> {
        self.action(
            sys::PACT_SET_SPAWN_POINT,
            &dimension.to_string(),
            x as f64,
            y as f64,
            z as f64,
        )
        .map(|_| ())
    }

    pub fn clear_title(&self) -> Result<()> {
        self.action(sys::PACT_CLEAR_TITLE, "", 0.0, 0.0, 0.0)
            .map(|_| ())
    }
    pub fn set_title(&self, text: &str) -> Result<()> {
        self.action(sys::PACT_SET_TITLE, text, 0.0, 0.0, 0.0)
            .map(|_| ())
    }
    pub fn set_subtitle(&self, text: &str) -> Result<()> {
        self.action(sys::PACT_SET_TITLE, text, 1.0, 0.0, 0.0)
            .map(|_| ())
    }
    pub fn set_actionbar(&self, text: &str) -> Result<()> {
        self.action(sys::PACT_SET_TITLE, text, 2.0, 0.0, 0.0)
            .map(|_| ())
    }

    // ── containers ──

    /// The player's main inventory.
    pub fn inventory(&self) -> Container {
        Container::player_inventory(self.clone())
    }

    /// The player's ender chest.
    pub fn ender_chest(&self) -> Container {
        Container::player_ender_chest(self.clone())
    }
}
