//! Player actions (mutations, messaging, movement).

use super::*;
use crate::error::{Error, Result};
use crate::ffi::s;
use crate::item::ItemStack;
use crate::{rt, sys};

impl Player {
    pub fn set_level(&self, level: i32) -> Result<()> {
        self.set_num(sys::PPROP_LEVEL, level as f64)
    }

    pub fn set_experience(&self, progress: f64) -> Result<()> {
        self.set_num(sys::PPROP_EXPERIENCE, progress)
    }

    pub fn set_hunger(&self, v: f64) -> Result<()> {
        self.set_num(sys::PPROP_HUNGER, v)
    }

    pub fn set_saturation(&self, v: f64) -> Result<()> {
        self.set_num(sys::PPROP_SATURATION, v)
    }

    pub fn set_exhaustion(&self, v: f64) -> Result<()> {
        self.set_num(sys::PPROP_EXHAUSTION, v)
    }

    pub fn send_message(&self, msg: &str) -> Result<()> {
        let ok = unsafe { (rt().api.player_send_message)(self.ffi_sel(), s(msg)) };
        if ok {
            Ok(())
        } else {
            Err(self.gone())
        }
    }

    /// Send a message of a specific [`MessageType`] — the equivalent of LSE's
    /// `player.tell(msg, type)`. Use it for tips (above the hotbar), popups
    /// (screen centre), system messages, and so on; [`MessageType::Raw`] (or
    /// [`send_message`](Self::send_message)) is an ordinary chat line.
    ///
    /// ```no_run
    /// # use levilamina::player::{Player, MessageType};
    /// # let p = Player::by_name("Steve");
    /// p.tell("Saved!", MessageType::Tip)?;
    /// # Ok::<(), levilamina::Error>(())
    /// ```
    pub fn tell(&self, msg: &str, kind: MessageType) -> Result<()> {
        let ok =
            unsafe { (rt().api.player_send_message_typed)(self.ffi_sel(), s(msg), kind as i32) };
        if ok {
            Ok(())
        } else {
            Err(self.gone())
        }
    }

    /// Send a **raw network packet** to this player's connection only.
    ///
    /// This is the generic escape hatch that per-player features like
    /// [`Server::spawn_particle_for`](crate::server::Server::spawn_particle_for)
    /// derive from: `packet_id` is a `MinecraftPacketIds` value, `body` is the
    /// packet's **wire-format body for the current game version**, which the
    /// bridge deserialises into a real packet object and delivers to this one
    /// connection — no other client receives it.
    ///
    /// Errors if the player is offline, the id can't be constructed, the body
    /// fails to parse, or bytes are left over after parsing (a wrong shape for
    /// this game version is refused instead of being sent half-parsed).
    ///
    /// # Caveats
    /// The wire format is **version-specific and unchecked beyond parsing** —
    /// a body that parses but carries nonsense is still delivered, and a
    /// malformed-but-parseable packet can desync or disconnect the client.
    /// Prefer a typed API whenever one exists; reach for this only when the
    /// bridge doesn't have the packet you need yet. Server thread only.
    pub fn send_packet(&self, packet_id: i32, body: &[u8]) -> Result<()> {
        let ok =
            unsafe { (rt().api.send_packet)(self.ffi_sel(), packet_id, body.as_ptr(), body.len()) };
        if ok {
            Ok(())
        } else {
            Err(Error(format!(
                "send_packet(id {packet_id}): player offline or body rejected"
            )))
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
}
