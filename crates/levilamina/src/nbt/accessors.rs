//! `NbtValue` accessors: get/insert/path/index + typed `as_*` projections.
//!
//! Split out of [`super::NbtValue`] so the object-model surface is browseable
//! independently from the SNBT serializer.

use std::collections::BTreeMap;

use super::NbtValue;

impl NbtValue {
    pub fn get(&self, key: &str) -> Option<&NbtValue> {
        match self {
            NbtValue::Compound(map) => map.get(key),
            _ => None,
        }
    }

    pub fn get_mut(&mut self, key: &str) -> Option<&mut NbtValue> {
        match self {
            NbtValue::Compound(map) => map.get_mut(key),
            _ => None,
        }
    }

    /// Insert into a compound (no-op returning `false` on other variants).
    pub fn insert(&mut self, key: impl Into<String>, value: NbtValue) -> bool {
        match self {
            NbtValue::Compound(map) => {
                map.insert(key.into(), value);
                true
            }
            _ => false,
        }
    }

    /// Walk a dotted path through nested compounds: `v.path("player.pos")`.
    pub fn path(&self, dotted: &str) -> Option<&NbtValue> {
        let mut cur = self;
        for part in dotted.split('.') {
            cur = cur.get(part)?;
        }
        Some(cur)
    }

    pub fn index(&self, i: usize) -> Option<&NbtValue> {
        match self {
            NbtValue::List(items) => items.get(i),
            _ => None,
        }
    }

    pub fn as_i64(&self) -> Option<i64> {
        match *self {
            NbtValue::Byte(v) => Some(v as i64),
            NbtValue::Short(v) => Some(v as i64),
            NbtValue::Int(v) => Some(v as i64),
            NbtValue::Long(v) => Some(v),
            NbtValue::Float(v) => Some(v as i64),
            NbtValue::Double(v) => Some(v as i64),
            _ => None,
        }
    }

    pub fn as_f64(&self) -> Option<f64> {
        match *self {
            NbtValue::Byte(v) => Some(v as f64),
            NbtValue::Short(v) => Some(v as f64),
            NbtValue::Int(v) => Some(v as f64),
            NbtValue::Long(v) => Some(v as f64),
            NbtValue::Float(v) => Some(v as f64),
            NbtValue::Double(v) => Some(v),
            _ => None,
        }
    }

    pub fn as_bool(&self) -> Option<bool> {
        self.as_i64().map(|v| v != 0)
    }

    pub fn as_str(&self) -> Option<&str> {
        match self {
            NbtValue::String(s) => Some(s),
            _ => None,
        }
    }

    pub fn as_list(&self) -> Option<&[NbtValue]> {
        match self {
            NbtValue::List(v) => Some(v),
            _ => None,
        }
    }

    pub fn as_compound(&self) -> Option<&BTreeMap<String, NbtValue>> {
        match self {
            NbtValue::Compound(m) => Some(m),
            _ => None,
        }
    }

    pub fn is_compound(&self) -> bool {
        matches!(self, NbtValue::Compound(_))
    }
}
