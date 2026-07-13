//! A custom form: inputs, toggles, dropdowns, sliders.

use super::*;
use crate::error::Result;
use crate::nbt::NbtValue;
use crate::player::Player;

/// A settings-style form with named inputs; results come back keyed by the
/// element names you pass in.
pub struct CustomFormBuilder {
    title: String,
    submit: Option<String>,
    elements: Vec<NbtValue>,
}

impl CustomFormBuilder {
    pub fn new(title: &str) -> Self {
        CustomFormBuilder {
            title: title.into(),
            submit: None,
            elements: Vec::new(),
        }
    }

    /// Text of the submit button.
    pub fn submit(mut self, text: &str) -> Self {
        self.submit = Some(text.into());
        self
    }

    fn named(kind: &str, name: &str, text: &str) -> NbtValue {
        let mut e = str_el(kind, text);
        e.insert("name", NbtValue::String(name.into()));
        e
    }

    pub fn input(mut self, name: &str, text: &str, placeholder: &str, default: &str) -> Self {
        let mut e = Self::named("input", name, text);
        e.insert("placeholder", NbtValue::String(placeholder.into()));
        e.insert("default", NbtValue::String(default.into()));
        self.elements.push(e);
        self
    }

    pub fn toggle(mut self, name: &str, text: &str, default: bool) -> Self {
        let mut e = Self::named("toggle", name, text);
        e.insert("default", NbtValue::Byte(if default { 1 } else { 0 }));
        self.elements.push(e);
        self
    }

    pub fn dropdown(mut self, name: &str, text: &str, options: &[&str], default: usize) -> Self {
        let mut e = Self::named("dropdown", name, text);
        e.insert(
            "options",
            NbtValue::List(
                options
                    .iter()
                    .map(|o| NbtValue::String((*o).into()))
                    .collect(),
            ),
        );
        e.insert("default", NbtValue::Int(default as i32));
        self.elements.push(e);
        self
    }

    pub fn slider(
        mut self,
        name: &str,
        text: &str,
        min: f64,
        max: f64,
        step: f64,
        default: f64,
    ) -> Self {
        let mut e = Self::named("slider", name, text);
        e.insert("min", NbtValue::Double(min));
        e.insert("max", NbtValue::Double(max));
        e.insert("step", NbtValue::Double(step));
        e.insert("default", NbtValue::Double(default));
        self.elements.push(e);
        self
    }

    pub fn step_slider(mut self, name: &str, text: &str, steps: &[&str], default: usize) -> Self {
        let mut e = Self::named("step_slider", name, text);
        e.insert(
            "options",
            NbtValue::List(
                steps
                    .iter()
                    .map(|o| NbtValue::String((*o).into()))
                    .collect(),
            ),
        );
        e.insert("default", NbtValue::Int(default as i32));
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
        if let Some(submit) = self.submit {
            spec.insert("submit", NbtValue::String(submit));
        }
        spec.insert("elements", NbtValue::List(self.elements));
        send(player, 1, &spec, cb)
    }
}
