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

    pub fn tell(&self, msg: &str, kind: MessageType) -> Result<()> {
        let ok =
            unsafe { (rt().api.player_send_message_typed)(self.ffi_sel(), s(msg), kind as i32) };
        if ok {
            Ok(())
        } else {
            Err(self.gone())
        }
    }

    pub fn set_sidebar(&self, objective: &str, title: &str, lines: &[String]) -> Result<()> {
        if objective.is_empty() || objective.contains('\n') || title.contains('\n') {
            return Err(Error("sidebar: objective/title must be one line".into()));
        }
        if lines.iter().any(|l| l.contains('\n')) {
            return Err(Error("sidebar: a line may not contain a newline".into()));
        }
        let mut payload = String::with_capacity(objective.len() + title.len() + 64);
        payload.push_str(objective);
        payload.push('\n');
        payload.push_str(title);
        for line in lines {
            payload.push('\n');
            payload.push_str(line);
        }
        self.action(sys::PACT_SIDEBAR_SET, &payload, 0.0, 0.0, 0.0)
            .map(|_| ())
    }

    pub fn clear_sidebar(&self, objective: &str) -> Result<()> {
        if objective.is_empty() {
            return Err(Error("sidebar: objective is empty".into()));
        }
        self.action(sys::PACT_SIDEBAR_CLEAR, objective, 0.0, 0.0, 0.0)
            .map(|_| ())
    }

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

    pub fn set_title_times(&self, times: TitleTimes) -> Result<()> {
        self.send_title(TitleKind::Times, "", Some(times))
    }

    pub fn clear_title(&self) -> Result<()> {
        self.send_title(TitleKind::Clear, "", None)
    }

    pub fn set_title(&self, text: &str) -> Result<()> {
        self.send_title(TitleKind::Title, text, None)
    }

    pub fn set_subtitle(&self, text: &str) -> Result<()> {
        self.send_title(TitleKind::Subtitle, text, None)
    }

    pub fn set_actionbar(&self, text: &str) -> Result<()> {
        self.send_title(TitleKind::Actionbar, text, None)
    }
}
