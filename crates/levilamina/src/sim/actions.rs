//! Simulated-player actions (the simulate* verb surface).

use super::*;
use crate::error::Result;
use crate::nbt::NbtValue;

impl SimPlayer {
    /// Disconnect and remove the bot.
    pub fn despawn(self) -> Result<()> {
        self.act("despawn", "")
    }

    /// Stop everything: movement, item use, building, interacting, mining.
    pub fn stop(&self) -> Result<()> {
        self.act("stop", "")
    }

    pub fn jump(&self) -> Result<()> {
        self.act("jump", "")
    }

    /// Attack what the bot is currently facing.
    pub fn attack(&self) -> Result<()> {
        self.act("attack", "")
    }

    /// Interact with what the bot is currently facing.
    pub fn interact(&self) -> Result<()> {
        self.act("interact", "")
    }

    /// Use the currently selected item.
    pub fn use_item(&self) -> Result<()> {
        self.act("use_item", "")
    }

    /// Drop the currently selected item.
    pub fn drop_selected(&self) -> Result<()> {
        self.act("drop", "")
    }

    pub fn respawn(&self) -> Result<()> {
        self.act("respawn", "")
    }

    /// Walk straight towards a point (no pathfinding).
    /// `speed` is a multiplier (1.0 = walking), `face_target` turns the head.
    pub fn move_to(&self, x: f64, y: f64, z: f64, speed: f64, face_target: bool) -> Result<()> {
        self.act(
            "move_to",
            &Self::pos_args(
                x,
                y,
                z,
                &[
                    ("speed", speed),
                    ("face_target", f64::from(face_target as u8)),
                ],
            ),
        )
    }

    /// Pathfind towards a point (navigation mesh; handles obstacles).
    pub fn navigate_to(&self, x: f64, y: f64, z: f64, speed: f64) -> Result<()> {
        self.act("navigate_to", &Self::pos_args(x, y, z, &[("speed", speed)]))
    }

    /// Turn to look at a world position.
    pub fn look_at(&self, x: f64, y: f64, z: f64) -> Result<()> {
        self.act("look_at", &Self::pos_args(x, y, z, &[]))
    }

    /// Start mining the block at a position. `face` is 0..=5
    /// (Down/Up/North/South/West/East).
    pub fn destroy_block(&self, x: i32, y: i32, z: i32, face: i32) -> Result<()> {
        self.act(
            "destroy_block",
            &format!("{{\"x\":{x},\"y\":{y},\"z\":{z},\"face\":{face}}}"),
        )
    }

    /// Start mining whatever the bot is looking at, within `hand` blocks.
    pub fn destroy_look_at(&self, hand: f64) -> Result<()> {
        self.act("destroy_look", &format!("{{\"hand\":{hand}}}"))
    }

    pub fn stop_destroying(&self) -> Result<()> {
        self.act("stop_destroy", "")
    }

    /// Interact with (right-click) the block at a position, `face` 0..=5.
    pub fn interact_block(&self, x: i32, y: i32, z: i32, face: i32) -> Result<()> {
        self.act(
            "interact_block",
            &format!("{{\"x\":{x},\"y\":{y},\"z\":{z},\"face\":{face}}}"),
        )
    }

    pub fn set_sneaking(&self, on: bool) -> Result<()> {
        self.act("sneak", &format!("{{\"on\":{}}}", u8::from(on)))
    }

    pub fn set_flying(&self, on: bool) -> Result<()> {
        self.act("fly", &format!("{{\"on\":{}}}", u8::from(on)))
    }

    /// Say something in chat as the bot.
    pub fn chat(&self, msg: &str) -> Result<()> {
        let mut args = NbtValue::compound();
        args.insert("msg", NbtValue::String(msg.to_string()));
        self.act("chat", &args.to_snbt())
    }
}
