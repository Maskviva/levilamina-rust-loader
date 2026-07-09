//! Forms: SimpleForm / CustomForm / ModalForm builders with async result
//! callbacks.
//!
//! Callbacks are `FnOnce` and fire **exactly once, on the server thread** —
//! or never, if the owning mod is disabled/unloaded before the player
//! responds (in which case the boxed callback is intentionally leaked; a
//! few dozen bytes per muted form beats a use-after-free every time).

use std::collections::HashMap;
use std::ffi::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};

use crate::error::{Error, Result};
use crate::ffi::{r, s};
use crate::logger::Logger;
use crate::nbt::NbtValue;
use crate::player::Player;
use crate::{rt, sys};

/// One value from a CustomForm submission.
#[derive(Debug, Clone, PartialEq)]
pub enum FormValue {
    /// Toggle (0/1), dropdown index, or step-slider index.
    Int(i64),
    /// Slider value.
    Float(f64),
    /// Input text.
    Text(String),
}

impl FormValue {
    pub fn as_i64(&self) -> Option<i64> {
        match self {
            FormValue::Int(v) => Some(*v),
            FormValue::Float(v) => Some(*v as i64),
            _ => None,
        }
    }
    pub fn as_f64(&self) -> Option<f64> {
        match self {
            FormValue::Int(v) => Some(*v as f64),
            FormValue::Float(v) => Some(*v),
            _ => None,
        }
    }
    pub fn as_bool(&self) -> Option<bool> {
        self.as_i64().map(|v| v != 0)
    }
    pub fn as_str(&self) -> Option<&str> {
        match self {
            FormValue::Text(v) => Some(v),
            _ => None,
        }
    }
}

/// What came back from a form.
#[derive(Debug, Clone)]
pub enum FormResponse {
    /// Player closed the form. `reason` is the raw `ModalFormCancelReason`
    /// (-1 when the client didn't say).
    Cancelled { reason: i32 },
    /// SimpleForm: index of the pressed button (declaration order).
    Button(usize),
    /// CustomForm: values keyed by the element names you declared.
    Custom(HashMap<String, FormValue>),
    /// ModalForm: `upper == true` for the primary button.
    Modal { upper: bool },
}

// ── shared send plumbing ──

type FormCallback = Box<dyn FnOnce(FormResponse)>;

unsafe extern "C" fn form_trampoline(user: *mut c_void, result_snbt: sys::LeviRsStr) {
    // Exactly-once contract: the bridge either calls this once or never.
    let cb: FormCallback = *Box::from_raw(user.cast::<FormCallback>());
    let response = parse_response(r(result_snbt));
    if catch_unwind(AssertUnwindSafe(move || cb(response))).is_err() {
        Logger::get().error("panic in form callback");
    }
}

fn parse_response(snbt: &str) -> FormResponse {
    let Ok(v) = NbtValue::parse(snbt) else {
        return FormResponse::Cancelled { reason: -1 };
    };
    if v.get("cancelled")
        .and_then(|c| c.as_bool())
        .unwrap_or(false)
    {
        return FormResponse::Cancelled {
            reason: v.get("reason").and_then(|x| x.as_i64()).unwrap_or(-1) as i32,
        };
    }
    if let Some(values) = v.get("values").and_then(|x| x.as_compound()) {
        let mut out = HashMap::new();
        for (key, value) in values {
            let fv = match value {
                NbtValue::Float(f) => FormValue::Float(*f as f64),
                NbtValue::Double(f) => FormValue::Float(*f),
                NbtValue::String(t) => FormValue::Text(t.clone()),
                other => match other.as_i64() {
                    Some(i) => FormValue::Int(i),
                    None => continue,
                },
            };
            out.insert(key.clone(), fv);
        }
        return FormResponse::Custom(out);
    }
    match v.get("button") {
        Some(NbtValue::String(which)) => FormResponse::Modal {
            upper: which == "upper",
        },
        Some(other) => FormResponse::Button(other.as_i64().unwrap_or(0).max(0) as usize),
        None => FormResponse::Cancelled { reason: -1 },
    }
}

fn send(
    player: &Player,
    kind: i32,
    spec: &NbtValue,
    cb: impl FnOnce(FormResponse) + 'static,
) -> Result<()> {
    let boxed: Box<FormCallback> = Box::new(Box::new(cb));
    let user = Box::into_raw(boxed);
    let ok = unsafe {
        (rt().api.form_send)(
            rt().handle,
            player.ffi_sel(),
            kind,
            s(&spec.to_snbt()),
            form_trampoline,
            user.cast(),
        )
    };
    if ok {
        Ok(())
    } else {
        // Send failed synchronously → the bridge will never call back;
        // reclaim the callback.
        unsafe { drop(Box::from_raw(user)) };
        Err(Error(
            "form_send failed (player offline / bad form?)".into(),
        ))
    }
}

fn str_el(kind: &str, text: &str) -> NbtValue {
    let mut e = NbtValue::compound();
    e.insert("kind", NbtValue::String(kind.into()));
    e.insert("text", NbtValue::String(text.into()));
    e
}

// ── SimpleForm ──

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

// ── CustomForm ──

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

// ── ModalForm ──

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
