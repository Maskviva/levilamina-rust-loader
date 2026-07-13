//! Pure-Rust SNBT object model (decision #9: only *binary* NBT crosses the
//! FFI boundary; the textual object model lives entirely on this side).
//!
//! The serializer's compatibility baseline is the bridge's own output —
//! `CompoundTag::toSnbt(SnbtFormat::Minimize)` — and everything this module
//! emits round-trips through `CompoundTag::fromSnbt` (used by event
//! write-back, item rebuild, form specs and command overload declarations).
//!
//! ```
//! use levilamina::nbt::NbtValue;
//! let v = NbtValue::parse(r#"{name:"steve",hp:20.0f,tags:["a","b"]}"#).unwrap();
//! assert_eq!(v.get("name").and_then(|n| n.as_str()), Some("steve"));
//! ```

use std::collections::BTreeMap;
use std::fmt::Write as _;

use crate::error::Result;

mod parser;

/// One NBT value. Compounds use a `BTreeMap` so serialization is
/// deterministic (stable diffs, stable tests).
#[derive(Debug, Clone, PartialEq)]
pub enum NbtValue {
    /// TAG_Byte — also SNBT's boolean (`true` parses to 1, `false` to 0).
    Byte(i8),
    Short(i16),
    Int(i32),
    Long(i64),
    Float(f32),
    Double(f64),
    String(String),
    List(Vec<NbtValue>),
    Compound(BTreeMap<String, NbtValue>),
    ByteArray(Vec<i8>),
    IntArray(Vec<i32>),
    LongArray(Vec<i64>),
}

impl NbtValue {
    /// An empty compound — the natural starting point for building tags.
    pub fn compound() -> NbtValue {
        NbtValue::Compound(BTreeMap::new())
    }

    // ── accessors ──

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

    // ── parse / serialize ──

    /// Parse SNBT text into a value. Accepts the full grammar the engine
    /// emits (Minimize or pretty), including typed arrays and both quote
    /// styles.
    pub fn parse(text: &str) -> Result<NbtValue> {
        parser::Parser::parse_all(text)
    }

    /// Serialize to minimized SNBT (the same dialect the bridge emits and
    /// consumes). Keys are quoted only when needed.
    pub fn to_snbt(&self) -> String {
        let mut out = String::new();
        self.write(&mut out);
        out
    }

    fn write(&self, out: &mut String) {
        match self {
            NbtValue::Byte(v) => {
                let _ = write!(out, "{v}b");
            }
            NbtValue::Short(v) => {
                let _ = write!(out, "{v}s");
            }
            NbtValue::Int(v) => {
                let _ = write!(out, "{v}");
            }
            NbtValue::Long(v) => {
                let _ = write!(out, "{v}L");
            }
            NbtValue::Float(v) => {
                write_float(out, *v as f64);
                out.push('f');
            }
            NbtValue::Double(v) => {
                write_float(out, *v);
                out.push('d');
            }
            NbtValue::String(v) => write_quoted(out, v),
            NbtValue::List(items) => {
                out.push('[');
                for (i, item) in items.iter().enumerate() {
                    if i > 0 {
                        out.push(',');
                    }
                    item.write(out);
                }
                out.push(']');
            }
            NbtValue::Compound(map) => {
                out.push('{');
                for (i, (key, value)) in map.iter().enumerate() {
                    if i > 0 {
                        out.push(',');
                    }
                    if is_bare_key(key) {
                        out.push_str(key);
                    } else {
                        write_quoted(out, key);
                    }
                    out.push(':');
                    value.write(out);
                }
                out.push('}');
            }
            NbtValue::ByteArray(items) => {
                out.push_str("[B;");
                for (i, v) in items.iter().enumerate() {
                    if i > 0 {
                        out.push(',');
                    }
                    let _ = write!(out, "{v}b");
                }
                out.push(']');
            }
            NbtValue::IntArray(items) => {
                out.push_str("[I;");
                for (i, v) in items.iter().enumerate() {
                    if i > 0 {
                        out.push(',');
                    }
                    let _ = write!(out, "{v}");
                }
                out.push(']');
            }
            NbtValue::LongArray(items) => {
                out.push_str("[L;");
                for (i, v) in items.iter().enumerate() {
                    if i > 0 {
                        out.push(',');
                    }
                    let _ = write!(out, "{v}L");
                }
                out.push(']');
            }
        }
    }
}

fn write_float(out: &mut String, v: f64) {
    if v == v.trunc() && v.is_finite() && v.abs() < 1e15 {
        // Keep a decimal point so the suffix-less double form stays a double.
        let _ = write!(out, "{v:.1}");
    } else {
        let _ = write!(out, "{v}");
    }
}

fn write_quoted(out: &mut String, s: &str) {
    out.push('"');
    for c in s.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            _ => out.push(c),
        }
    }
    out.push('"');
}

fn is_bare_key(key: &str) -> bool {
    !key.is_empty()
        && key
            .bytes()
            .all(|b| b.is_ascii_alphanumeric() || b == b'_' || b == b'-' || b == b'.' || b == b'+')
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_trip() {
        let src =
            r#"{"a name":"steve",hp:20.5f,lvl:3b,pos:[1.0d,64.0d,-3.5d],xs:[I;1,2,3],id:42L}"#;
        let v = NbtValue::parse(src).unwrap();
        let again = NbtValue::parse(&v.to_snbt()).unwrap();
        assert_eq!(v, again);
        assert_eq!(v.get("hp").unwrap(), &NbtValue::Float(20.5));
        assert_eq!(
            v.path("pos").unwrap().index(1).unwrap().as_f64(),
            Some(64.0)
        );
    }

    #[test]
    fn booleans_and_bare_strings() {
        let v = NbtValue::parse("{ok:true,name:steve}").unwrap();
        assert_eq!(v.get("ok").unwrap().as_bool(), Some(true));
        assert_eq!(v.get("name").unwrap().as_str(), Some("steve"));
    }
}
