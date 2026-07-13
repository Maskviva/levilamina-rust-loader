//! Scoreboard operations: objectives, scores, and display slots.

use super::*;
use crate::error::{Error, Result};
use crate::ffi::{call_out_str, s};
use crate::nbt::NbtValue;
use crate::rt;

impl Scoreboard {
    /// Create an objective (criteria: `dummy`). `display_name` may be empty
    /// to reuse `name`.
    pub fn add_objective(&self, name: &str, display_name: &str) -> Result<()> {
        self.op_bool(
            sys::SB_ADD_OBJECTIVE,
            name,
            display_name,
            0,
            "add_objective",
        )
    }

    pub fn remove_objective(&self, name: &str) -> Result<()> {
        self.op_bool(sys::SB_REMOVE_OBJECTIVE, name, "", 0, "remove_objective")
    }

    pub fn objectives(&self) -> Vec<Objective> {
        let Some(raw) = self.op(sys::SB_LIST_OBJECTIVES, "", "", 0) else {
            return Vec::new();
        };
        let Ok(v) = NbtValue::parse(&raw) else {
            return Vec::new();
        };
        v.as_list()
            .map(|items| {
                items
                    .iter()
                    .filter_map(|o| {
                        Some(Objective {
                            name: o.get("name")?.as_str()?.to_owned(),
                            display_name: o
                                .get("display")
                                .and_then(|d| d.as_str())
                                .unwrap_or("")
                                .to_owned(),
                        })
                    })
                    .collect()
            })
            .unwrap_or_default()
    }

    /// A score, or `None` when the target has no score on this objective.
    pub fn score(&self, objective: &str, who: &str) -> Option<i64> {
        self.op(sys::SB_GET_SCORE, objective, who, 0)?.parse().ok()
    }

    /// Set and return the new value.
    pub fn set_score(&self, objective: &str, who: &str, value: i64) -> Result<i64> {
        self.op(sys::SB_SET_SCORE, objective, who, value)
            .and_then(|v| v.parse().ok())
            .ok_or_else(|| {
                Error(format!(
                    "set_score failed (objective '{objective}' missing?)"
                ))
            })
    }

    pub fn add_score(&self, objective: &str, who: &str, delta: i64) -> Result<i64> {
        self.op(sys::SB_ADD_SCORE, objective, who, delta)
            .and_then(|v| v.parse().ok())
            .ok_or_else(|| {
                Error(format!(
                    "add_score failed (objective '{objective}' missing?)"
                ))
            })
    }

    pub fn reduce_score(&self, objective: &str, who: &str, delta: i64) -> Result<i64> {
        self.op(sys::SB_REDUCE_SCORE, objective, who, delta)
            .and_then(|v| v.parse().ok())
            .ok_or_else(|| {
                Error(format!(
                    "reduce_score failed (objective '{objective}' missing?)"
                ))
            })
    }

    pub fn reset_score(&self, objective: &str, who: &str) -> Result<()> {
        self.op_bool(sys::SB_RESET_SCORE, objective, who, 0, "reset_score")
    }

    pub fn set_display(&self, slot: DisplaySlot, objective: &str) -> Result<()> {
        // Bridge contract: a = display slot, b = objective
        // (mirrors `/scoreboard objectives setdisplay <slot> [objective]`).
        self.op_bool(
            sys::SB_SET_DISPLAY,
            slot.as_str(),
            objective,
            0,
            "set_display",
        )
    }

    pub fn clear_display(&self, slot: DisplaySlot) -> Result<()> {
        self.op_bool(sys::SB_CLEAR_DISPLAY, slot.as_str(), "", 0, "clear_display")
    }
}
