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

#[derive(Debug, Clone, PartialEq)]
pub enum FormValue {
    Int(i64),

    Float(f64),

    Text(String),

    Choice { index: i64, text: String },
}

impl FormValue {
    pub fn as_i64(&self) -> Option<i64> {
        match self {
            FormValue::Int(v) => Some(*v),
            FormValue::Float(v) => Some(*v as i64),
            FormValue::Choice { index, .. } => Some(*index),
            FormValue::Text(_) => None,
        }
    }
    pub fn as_f64(&self) -> Option<f64> {
        match self {
            FormValue::Int(v) => Some(*v as f64),
            FormValue::Float(v) => Some(*v),
            FormValue::Choice { index, .. } => Some(*index as f64),
            FormValue::Text(_) => None,
        }
    }
    pub fn as_bool(&self) -> Option<bool> {
        self.as_i64().map(|v| v != 0)
    }
    pub fn as_str(&self) -> Option<&str> {
        match self {
            FormValue::Text(v) => Some(v),
            FormValue::Choice { text, .. } => Some(text),
            _ => None,
        }
    }

    pub fn as_index(&self) -> Option<usize> {
        match self {
            FormValue::Choice { index, .. } if *index >= 0 => Some(*index as usize),
            _ => None,
        }
    }
}

#[derive(Debug, Clone)]
pub enum FormResponse {
    Cancelled { reason: i32 },

    Button(usize),

    Custom(HashMap<String, FormValue>),

    Modal { upper: bool },
}

pub(super) type FormCallback = Box<dyn FnOnce(FormResponse)>;

pub(super) unsafe extern "C" fn form_trampoline(user: *mut c_void, result_snbt: sys::LeviRsStr) {
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
        let texts = v.get("texts").and_then(|x| x.as_compound());
        let text_of = |key: &String| -> Option<String> {
            texts.and_then(|t| t.get(key)).and_then(|x| match x {
                NbtValue::String(s) => Some(s.clone()),
                _ => None,
            })
        };

        let mut out = HashMap::new();
        for (key, value) in values {
            let fv = match value {
                NbtValue::Float(f) => FormValue::Float(*f as f64),
                NbtValue::Double(f) => FormValue::Float(*f),
                NbtValue::String(t) => FormValue::Text(t.clone()),
                other => match other.as_i64() {
                    Some(i) => match text_of(key) {
                        Some(text) => FormValue::Choice { index: i, text },
                        None => FormValue::Int(i),
                    },
                    None => continue,
                },
            };
            out.insert(key.clone(), fv);
        }

        if let Some(texts) = texts {
            for (key, value) in texts {
                if out.contains_key(key) {
                    continue;
                }
                if let NbtValue::String(s) = value {
                    out.insert(key.clone(), FormValue::Text(s.clone()));
                }
            }
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
            rt().handle(),
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
