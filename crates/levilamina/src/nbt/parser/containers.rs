//! Container productions: the dispatch `value`, plus compounds, lists, and
//! the `[B; …]` / `[I; …]` / `[L; …]` typed arrays.

use crate::error::Result;

use super::{super::super::NbtValue, Parser};
use std::collections::BTreeMap;

impl<'a> Parser<'a> {
    pub(super) fn value(&mut self) -> Result<NbtValue> {
        self.skip_ws();
        match self.peek() {
            Some(b'{') => self.compound(),
            Some(b'[') => self.list_or_array(),
            Some(b'"') | Some(b'\'') => Ok(NbtValue::String(self.quoted_string()?)),
            Some(_) => self.scalar(),
            None => Err(self.err("unexpected end of input")),
        }
    }

    pub(super) fn compound(&mut self) -> Result<NbtValue> {
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

    pub(super) fn list_or_array(&mut self) -> Result<NbtValue> {
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

    pub(super) fn typed_array(&mut self, kind: u8) -> Result<NbtValue> {
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
}
