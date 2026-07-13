//! A simple button-list form.

use super::*;
use crate::error::Result;
use crate::nbt::NbtValue;
use crate::player::Player;

/// A button-list form.
///
/// ```no_run
/// # use levilamina::prelude::*;
/// # let player = Player::by_name("steve");
/// levilamina::SimpleFormBuilder::new("Menu")
///     .content("choose one")
///     .button("Teleport home")
///     .button("Open shop")
///     .send(&player, |resp| {
///         if let FormResponse::Button(i) = resp { /* … */ }
///     })
///     .unwrap();
/// ```
pub struct SimpleFormBuilder {
    title: String,
    content: String,
    elements: Vec<NbtValue>,
}

impl SimpleFormBuilder {
    pub fn new(title: &str) -> Self {
        SimpleFormBuilder {
            title: title.into(),
            content: String::new(),
            elements: Vec::new(),
        }
    }

    pub fn content(mut self, content: &str) -> Self {
        self.content = content.into();
        self
    }

    /// A plain button. Button indices in [`FormResponse::Button`] follow
    /// declaration order (headers/labels/dividers don't count).
    pub fn button(mut self, text: &str) -> Self {
        self.elements.push(str_el("button", text));
        self
    }

    /// A button with an image: `image_type` is `"path"` (texture pack path)
    /// or `"url"`.
    pub fn button_with_image(mut self, text: &str, image: &str, image_type: &str) -> Self {
        let mut e = str_el("button", text);
        e.insert("image", NbtValue::String(image.into()));
        e.insert("image_type", NbtValue::String(image_type.into()));
        self.elements.push(e);
        self
    }

    pub fn header(mut self, text: &str) -> Self {
        self.elements.push(str_el("header", text));
        self
    }
    pub fn label(mut self, text: &str) -> Self {
        self.elements.push(str_el("label", text));
        self
    }
    pub fn divider(mut self) -> Self {
        self.elements.push(str_el("divider", ""));
        self
    }

    pub fn send(self, player: &Player, cb: impl FnOnce(FormResponse) + 'static) -> Result<()> {
        let mut spec = NbtValue::compound();
        spec.insert("title", NbtValue::String(self.title));
        spec.insert("content", NbtValue::String(self.content));
        spec.insert("elements", NbtValue::List(self.elements));
        send(player, 0, &spec, cb)
    }
}
