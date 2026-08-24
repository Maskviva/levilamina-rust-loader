use super::*;
use crate::error::{Error, Result};
use crate::sys;

impl Entity {
    pub fn kill(&self) -> Result<()> {
        self.action(sys::AACT_KILL, "", 0.0, 0.0, 0.0).map(|_| ())
    }

    pub fn despawn(&self) -> Result<()> {
        self.action(sys::AACT_DESPAWN, "", 0.0, 0.0, 0.0)
            .map(|_| ())
    }

    pub fn heal(&self, amount: i32) -> Result<()> {
        self.action(sys::AACT_HEAL, "", amount as f64, 0.0, 0.0)
            .map(|_| ())
    }

    pub fn set_on_fire(&self, seconds: i32) -> Result<()> {
        self.action(sys::AACT_SET_ON_FIRE, "", seconds as f64, 0.0, 0.0)
            .map(|_| ())
    }

    pub fn teleport(&self, x: f64, y: f64, z: f64) -> Result<()> {
        self.action(sys::AACT_TELEPORT, "", x, y, z).map(|_| ())
    }

    pub fn teleport_to_dimension(&self, dimension: i32, x: f64, y: f64, z: f64) -> Result<()> {
        self.action(sys::AACT_TELEPORT, &dimension.to_string(), x, y, z)
            .map(|_| ())
    }

    pub fn set_rotation(&self, pitch: f64, yaw: f64) -> Result<()> {
        self.action(sys::AACT_SET_ROTATION, "", pitch, yaw, 0.0)
            .map(|_| ())
    }

    pub fn execute_event(&self, event: &str) -> Result<()> {
        if event.is_empty() {
            return Err(Error("event name is empty".into()));
        }
        self.action(sys::AACT_EXECUTE_EVENT, event, 0.0, 0.0, 0.0)
            .map(|_| ())
    }

    pub fn set_name_tag(&self, name: &str) -> Result<()> {
        self.action(sys::AACT_SET_NAME_TAG, name, 0.0, 0.0, 0.0)
            .map(|_| ())
    }

    pub fn add_tag(&self, tag: &str) -> Result<bool> {
        Ok(self
            .action(sys::AACT_ADD_TAG, tag, 0.0, 0.0, 0.0)?
            .as_deref()
            == Some("1"))
    }

    pub fn remove_tag(&self, tag: &str) -> Result<bool> {
        Ok(self
            .action(sys::AACT_REMOVE_TAG, tag, 0.0, 0.0, 0.0)?
            .as_deref()
            == Some("1"))
    }

    pub fn has_tag(&self, tag: &str) -> Result<bool> {
        Ok(self
            .action(sys::AACT_HAS_TAG, tag, 0.0, 0.0, 0.0)?
            .as_deref()
            == Some("1"))
    }

    pub fn add_effect(
        &self,
        effect: &str,
        ticks: i32,
        amplifier: i32,
        visible: bool,
    ) -> Result<()> {
        self.action(
            sys::AACT_ADD_EFFECT,
            effect,
            ticks as f64,
            amplifier as f64,
            if visible { 1.0 } else { 0.0 },
        )
        .map(|_| ())
    }

    pub fn remove_effect(&self, effect: &str) -> Result<()> {
        self.action(sys::AACT_REMOVE_EFFECT, effect, 0.0, 0.0, 0.0)
            .map(|_| ())
    }

    pub fn clear_effects(&self) -> Result<()> {
        self.action(sys::AACT_CLEAR_EFFECTS, "", 0.0, 0.0, 0.0)
            .map(|_| ())
    }

    pub fn hurt(&self, amount: i32) -> Result<()> {
        self.action(sys::AACT_HURT, "", amount as f64, 0.0, 0.0)
            .map(|_| ())
    }
}
