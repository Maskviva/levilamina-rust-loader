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

pub mod custom;
pub mod modal;
pub mod simple;

pub use custom::CustomFormBuilder;
pub use modal::ModalFormBuilder;
pub use simple::SimpleFormBuilder;

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

pub(super) type FormCallback = Box<dyn FnOnce(FormResponse)>;

pub(super) unsafe extern "C" fn form_trampoline(user: *mut c_void, result_snbt: sys::LeviRsStr) {
    // Exactly-once contract: the bridge either calls this once or never.
    let cb: FormCallback = *Box::from_raw(user.cast::<FormCallback>());
    let response = parse_response(r(result_snbt));
    if catch_unwind(AssertUnwindSafe(move || cb(response))).is_err() {
        Logger::get().error("panic in form callback");
    }
}

pub(super) fn parse_response(snbt: &str) -> FormResponse {
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

pub(super) fn send(
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

pub(super) fn str_el(kind: &str, text: &str) -> NbtValue {
    let mut e = NbtValue::compound();
    e.insert("kind", NbtValue::String(kind.into()));
    e.insert("text", NbtValue::String(text.into()));
    e
}
