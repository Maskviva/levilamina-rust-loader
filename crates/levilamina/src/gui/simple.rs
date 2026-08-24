use super::*;
use crate::error::Result;
use crate::nbt::NbtValue;
use crate::player::Player;

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

    pub fn button(mut self, text: &str) -> Self {
        self.elements.push(str_el("button", text));
        self
    }

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
