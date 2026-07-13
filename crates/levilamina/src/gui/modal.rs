//! A two-button modal form.

use super::*;
use crate::error::Result;
use crate::nbt::NbtValue;
use crate::player::Player;

/// A two-button confirm dialog.
pub struct ModalFormBuilder {
    title: String,
    content: String,
    upper: String,
    lower: String,
}

impl ModalFormBuilder {
    pub fn new(title: &str, content: &str) -> Self {
        ModalFormBuilder {
            title: title.into(),
            content: content.into(),
            upper: "OK".into(),
            lower: "Cancel".into(),
        }
    }

    pub fn upper(mut self, text: &str) -> Self {
        self.upper = text.into();
        self
    }
    pub fn lower(mut self, text: &str) -> Self {
        self.lower = text.into();
        self
    }

    pub fn send(self, player: &Player, cb: impl FnOnce(FormResponse) + 'static) -> Result<()> {
        let mut spec = NbtValue::compound();
        spec.insert("title", NbtValue::String(self.title));
        spec.insert("content", NbtValue::String(self.content));
        spec.insert("upper", NbtValue::String(self.upper));
        spec.insert("lower", NbtValue::String(self.lower));
        send(player, 2, &spec, cb)
    }
}
