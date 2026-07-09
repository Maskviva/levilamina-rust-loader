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

use crate::error::{Error, Result};

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
        let mut p = Parser {
            bytes: text.as_bytes(),
            pos: 0,
        };
        p.skip_ws();
        let v = p.value()?;
        p.skip_ws();
        if p.pos != p.bytes.len() {
            return Err(Error(format!("snbt: trailing input at byte {}", p.pos)));
        }
        Ok(v)
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

// ───────────────────────── parser ─────────────────────────

struct Parser<'a> {
    bytes: &'a [u8],
    pos: usize,
}

impl<'a> Parser<'a> {
    fn err(&self, msg: &str) -> Error {
        Error(format!("snbt: {msg} at byte {}", self.pos))
    }

    fn peek(&self) -> Option<u8> {
        self.bytes.get(self.pos).copied()
    }

    fn bump(&mut self) -> Option<u8> {
        let b = self.peek()?;
        self.pos += 1;
        Some(b)
    }

    fn skip_ws(&mut self) {
        while matches!(self.peek(), Some(b' ' | b'\t' | b'\r' | b'\n')) {
            self.pos += 1;
        }
    }

    fn expect(&mut self, b: u8) -> Result<()> {
        if self.peek() == Some(b) {
            self.pos += 1;
            Ok(())
        } else {
            Err(self.err(&format!("expected '{}'", b as char)))
        }
    }

    fn value(&mut self) -> Result<NbtValue> {
        self.skip_ws();
        match self.peek() {
            Some(b'{') => self.compound(),
            Some(b'[') => self.list_or_array(),
            Some(b'"') | Some(b'\'') => Ok(NbtValue::String(self.quoted_string()?)),
            Some(_) => self.scalar(),
            None => Err(self.err("unexpected end of input")),
        }
    }

    fn compound(&mut self) -> Result<NbtValue> {
        self.expect(b'{')?;
        let mut map = BTreeMap::new();
        self.skip_ws();
        if self.peek() == Some(b'}') {
            self.pos += 1;
            return Ok(NbtValue::Compound(map));
        }
        loop {
            self.skip_ws();
            let key = match self.peek() {
                Some(b'"') | Some(b'\'') => self.quoted_string()?,
                _ => self.bare_token()?.to_owned(),
            };
            self.skip_ws();
            self.expect(b':')?;
            let value = self.value()?;
            map.insert(key, value);
            self.skip_ws();
            match self.bump() {
                Some(b',') => continue,
                Some(b'}') => break,
                _ => return Err(self.err("expected ',' or '}' in compound")),
            }
        }
        Ok(NbtValue::Compound(map))
    }

    fn list_or_array(&mut self) -> Result<NbtValue> {
        self.expect(b'[')?;
        // Typed arrays: [B; …] / [I; …] / [L; …]
        if self.bytes.len() >= self.pos + 2 && self.bytes[self.pos + 1] == b';' {
            let kind = self.bytes[self.pos];
            self.pos += 2;
            return self.typed_array(kind);
        }
        let mut items = Vec::new();
        self.skip_ws();
        if self.peek() == Some(b']') {
            self.pos += 1;
            return Ok(NbtValue::List(items));
        }
        loop {
            items.push(self.value()?);
            self.skip_ws();
            match self.bump() {
                Some(b',') => continue,
                Some(b']') => break,
                _ => return Err(self.err("expected ',' or ']' in list")),
            }
        }
        Ok(NbtValue::List(items))
    }

    fn typed_array(&mut self, kind: u8) -> Result<NbtValue> {
        let mut bytes_ = Vec::new();
        let mut ints = Vec::new();
        let mut longs = Vec::new();
        self.skip_ws();
        if self.peek() == Some(b']') {
            self.pos += 1;
        } else {
            loop {
                let v = self.scalar()?;
                let n = v
                    .as_i64()
                    .ok_or_else(|| self.err("non-numeric element in typed array"))?;
                match kind {
                    b'B' => bytes_.push(n as i8),
                    b'I' => ints.push(n as i32),
                    b'L' => longs.push(n),
                    _ => return Err(self.err("unknown typed-array kind")),
                }
                self.skip_ws();
                match self.bump() {
                    Some(b',') => {
                        self.skip_ws();
                        continue;
                    }
                    Some(b']') => break,
                    _ => return Err(self.err("expected ',' or ']' in typed array")),
                }
            }
        }
        Ok(match kind {
            b'B' => NbtValue::ByteArray(bytes_),
            b'I' => NbtValue::IntArray(ints),
            _ => NbtValue::LongArray(longs),
        })
    }

    fn quoted_string(&mut self) -> Result<String> {
        let quote = self.bump().unwrap();
        let mut out = String::new();
        loop {
            match self.bump() {
                None => return Err(self.err("unterminated string")),
                Some(b) if b == quote => break,
                Some(b'\\') => match self.bump() {
                    Some(b'n') => out.push('\n'),
                    Some(b'r') => out.push('\r'),
                    Some(b't') => out.push('\t'),
                    Some(c @ (b'"' | b'\'' | b'\\')) => out.push(c as char),
                    Some(c) => {
                        out.push('\\');
                        out.push(c as char);
                    }
                    None => return Err(self.err("unterminated escape")),
                },
                Some(b) if b < 0x80 => out.push(b as char),
                Some(b) => {
                    // Re-decode a UTF-8 sequence starting at `b`.
                    let start = self.pos - 1;
                    let len = utf8_len(b);
                    let end = (start + len).min(self.bytes.len());
                    let chunk = std::str::from_utf8(&self.bytes[start..end])
                        .map_err(|_| self.err("invalid UTF-8 in string"))?;
                    out.push_str(chunk);
                    self.pos = end;
                }
            }
        }
        Ok(out)
    }

    fn bare_token(&mut self) -> Result<&'a str> {
        let start = self.pos;
        while let Some(b) = self.peek() {
            if b.is_ascii_alphanumeric() || matches!(b, b'_' | b'-' | b'.' | b'+') {
                self.pos += 1;
            } else {
                break;
            }
        }
        if self.pos == start {
            return Err(self.err("expected a token"));
        }
        std::str::from_utf8(&self.bytes[start..self.pos]).map_err(|_| self.err("invalid UTF-8"))
    }

    fn scalar(&mut self) -> Result<NbtValue> {
        let token = self.bare_token()?;
        // Booleans.
        if token.eq_ignore_ascii_case("true") {
            return Ok(NbtValue::Byte(1));
        }
        if token.eq_ignore_ascii_case("false") {
            return Ok(NbtValue::Byte(0));
        }
        // Suffixed numbers.
        let (body, suffix) = match token.as_bytes().last() {
            Some(c @ (b'b' | b'B' | b's' | b'S' | b'l' | b'L' | b'f' | b'F' | b'd' | b'D')) => {
                (&token[..token.len() - 1], Some(c.to_ascii_lowercase()))
            }
            _ => (token, None),
        };
        let is_numeric_body = !body.is_empty()
            && body
                .bytes()
                .all(|b| b.is_ascii_digit() || matches!(b, b'-' | b'+' | b'.' | b'e' | b'E'))
            && body.bytes().any(|b| b.is_ascii_digit());
        if is_numeric_body {
            match suffix {
                Some(b'b') => {
                    if let Ok(v) = body.parse::<i8>() {
                        return Ok(NbtValue::Byte(v));
                    }
                }
                Some(b's') => {
                    if let Ok(v) = body.parse::<i16>() {
                        return Ok(NbtValue::Short(v));
                    }
                }
                Some(b'l') => {
                    if let Ok(v) = body.parse::<i64>() {
                        return Ok(NbtValue::Long(v));
                    }
                }
                Some(b'f') => {
                    if let Ok(v) = body.parse::<f32>() {
                        return Ok(NbtValue::Float(v));
                    }
                }
                Some(b'd') => {
                    if let Ok(v) = body.parse::<f64>() {
                        return Ok(NbtValue::Double(v));
                    }
                }
                None => {
                    if body.contains(['.', 'e', 'E']) {
                        if let Ok(v) = body.parse::<f64>() {
                            return Ok(NbtValue::Double(v));
                        }
                    } else if let Ok(v) = body.parse::<i32>() {
                        return Ok(NbtValue::Int(v));
                    } else if let Ok(v) = body.parse::<i64>() {
                        // Engine sometimes prints int64 without the L suffix.
                        return Ok(NbtValue::Long(v));
                    }
                }
                _ => {}
            }
        }
        // Anything else is an unquoted string.
        Ok(NbtValue::String(token.to_owned()))
    }
}

fn utf8_len(first: u8) -> usize {
    match first {
        b if b >= 0xF0 => 4,
        b if b >= 0xE0 => 3,
        b if b >= 0xC0 => 2,
        _ => 1,
    }
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
