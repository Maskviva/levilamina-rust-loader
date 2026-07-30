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
//!
//! The implementation is split across sibling files so each concern fits in
//! one screen:
//!   - [`accessors`] — get/insert/path/index + `as_*` projections
//!   - [`serde`]      — the recursive SNBT writer + helpers + round-trip tests
//!   - [`parser`]     — the SNBT reader driving [`NbtValue::parse`]

use std::collections::BTreeMap;

use crate::error::Result;

mod accessors;
mod binary;
mod parser;
mod serde;

pub use binary::NbtBinaryFormat;

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
}
