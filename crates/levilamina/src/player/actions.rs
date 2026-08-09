//! Player actions (mutations, messaging, movement).

use super::*;
use crate::error::{Error, Result};
use crate::ffi::s;
use crate::item::ItemStack;
use crate::Logger;
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

    /// Teleport the player, across dimensions if needed.
    ///
    /// `dimension` accepts custom dimensions (MoreDimensions ids >= 3) as well
    /// as the vanilla 0/1/2. The loader routes this through the engine's own
    /// teleport path, so the dimension-change hooks that make the client accept
    /// a custom dimension fire correctly.
    ///
    /// Fails when the player is offline, or when `dimension` isn't a
    /// *registered* dimension id. Note the loader deliberately refuses unknown
    /// ids instead of silently falling back to the overworld — dumping a player
    /// into the main world because of a stale id is worse than an error.
    pub fn teleport(&self, dimension: i32, x: f64, y: f64, z: f64) -> Result<()> {
        let ok = unsafe { (rt().api.player_teleport)(self.ffi_sel(), dimension, x, y, z) };
        if ok {
            Ok(())
        } else {
            Err(Error(format!(
                "teleport of '{}' to dimension {dimension} at ({x:.1}, {y:.1}, {z:.1}) failed. \
                 Causes: the player is offline, or dimension {dimension} is not registered \
                 (custom dimensions must be re-registered on every startup — \
                 more_dimensions::add_simple_dimension / add_plot_dimension are idempotent, \
                 call them unconditionally from on_enable).",
                self.selector_hint()
            )))
        }
    }

    /// Set a player ability.
    ///
    /// `value` may be a `bool` (boolean slots such as `MayFly`) or a number
    /// (float slots: `FlySpeed`, `WalkSpeed`, `VerticalFlySpeed`). The
    /// engine's `AbilitiesIndex` stores those three as floats and everything
    /// else as bools; [`Ability::is_float`] says which is which.
    ///
    /// Mixing them up used to be silent — this now warns instead, because the
    /// FFI boundary is a bare `f64` and nothing downstream can tell a
    /// mis-typed value from an intentional one.
    pub fn set_ability<V: AbilityValue>(&self, ability: Ability, value: V) -> Result<()> {
        if ability.is_float() && V::IS_BOOL {
            Logger::get().warn(&format!(
                "set_ability({ability:?}, <bool>): 这是一个 float 槽位，\
                 传 bool 会把速度设成 0 或 1，八成不是你想要的"
            ));
        } else if !ability.is_float() && !V::IS_BOOL {
            let v = value.as_f64();
            if v != 0.0 && v != 1.0 {
                Logger::get().warn(&format!(
                    "set_ability({ability:?}, {v}): 这是一个 bool 槽位，\
                     非 0 的值一律当作 true"
                ));
            }
        }
        self.set_ability_raw(ability as i32, value)
    }

    /// Set an ability by raw `AbilitiesIndex` value (for slots not covered
    /// by the [`Ability`] enum). `value` may be a `bool` (boolean slots) or a
    /// `f64`/`f32`/`i32` (float slots such as `FlySpeed`).
    ///
    /// No slot-kind checking here — the whole point of the raw form is that
    /// the caller knows something this crate's enum does not.
    pub fn set_ability_raw<V: AbilityValue>(&self, index: i32, value: V) -> Result<()> {
        self.action(sys::PACT_SET_ABILITY, "", index as f64, value.as_f64(), 0.0)
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

    /// Send one `SetTitlePacket`. The general form behind [`set_title`],
    /// [`set_subtitle`], [`set_actionbar`] and [`clear_title`].
    ///
    /// `times` of `None` leaves the client's stored timing alone; `Some(t)`
    /// sends a `Times` packet first so the result does not depend on what some
    /// other plugin last set for this player.
    ///
    /// # Why this is a packet and not `/title`
    ///
    /// Until this method existed, every title went out as a console command:
    /// `title "<name>" title <text>`. The text was pasted in unquoted, so any
    /// `"` in it truncated the command; and `/title`'s text parameter is a
    /// `message`, which expands selectors — a plot named `@e` was command
    /// injection rather than a name. Neither failure was visible from the
    /// calling side, because `runConsoleCommand` reported success.
    ///
    /// [`set_title`]: Self::set_title
    /// [`set_subtitle`]: Self::set_subtitle
    /// [`set_actionbar`]: Self::set_actionbar
    /// [`clear_title`]: Self::clear_title
    ///
    /// ```no_run
    /// # use levilamina::player::{Player, TitleKind, TitleTimes};
    /// # let p = Player::by_name("Steve");
    /// p.send_title(TitleKind::Title, "§b海边小屋", Some(TitleTimes::new(5, 40, 10)))?;
    /// p.send_title(TitleKind::Subtitle, "§7欢迎光临", None)?;
    /// # Ok::<(), levilamina::Error>(())
    /// ```
    pub fn send_title(&self, kind: TitleKind, text: &str, times: Option<TitleTimes>) -> Result<()> {
        let t = times.unwrap_or(TitleTimes::new(-1, -1, -1));
        let ok = unsafe {
            (rt().api.player_send_title)(
                self.ffi_sel(),
                kind as i32,
                s(text),
                t.fade_in,
                t.stay,
                t.fade_out,
            )
        };
        if ok {
            Ok(())
        } else {
            Err(self.gone())
        }
    }

    /// Set fade-in / stay / fade-out for this player's *subsequent* titles.
    ///
    /// Rarely needed on its own: [`send_title`](Self::send_title) with
    /// `Some(times)` already sends this ahead of the content packet.
    pub fn set_title_times(&self, times: TitleTimes) -> Result<()> {
        self.send_title(TitleKind::Times, "", Some(times))
    }

    /// Hide the title/subtitle currently on screen. Timings are kept; use
    /// [`TitleKind::Reset`] via [`send_title`](Self::send_title) to restore the
    /// client's defaults as well.
    pub fn clear_title(&self) -> Result<()> {
        self.send_title(TitleKind::Clear, "", None)
    }

    /// Big text at screen centre, with the client's current timings.
    ///
    /// Use [`send_title`](Self::send_title) when the timing matters — "current"
    /// here means whatever the last `/title … times` on this player set, which
    /// is not something a caller can rely on.
    pub fn set_title(&self, text: &str) -> Result<()> {
        self.send_title(TitleKind::Title, text, None)
    }

    /// Second line under the title. Only visible while a title is showing, so
    /// send this *before* the title (or accept that it will wait for the next
    /// one).
    pub fn set_subtitle(&self, text: &str) -> Result<()> {
        self.send_title(TitleKind::Subtitle, text, None)
    }

    /// Text above the hotbar. Independent of the title/subtitle pair — this is
    /// the cheap one for status lines that update often.
    pub fn set_actionbar(&self, text: &str) -> Result<()> {
        self.send_title(TitleKind::Actionbar, text, None)
    }
}
