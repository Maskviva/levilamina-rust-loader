//! SNBT parser (internal): drives [`NbtValue::parse`].

use super::*;
use crate::error::{Error, Result};

pub(super) struct Parser<'a> {
    bytes: &'a [u8],
    pos: usize,
}

impl<'a> Parser<'a> {
    /// Construct a parser over SNBT bytes (used by [`super::NbtValue::parse`]).
    pub(super) fn new(bytes: &'a [u8]) -> Parser<'a> {
        Parser { bytes, pos: 0 }
    }

    /// Parse the whole input, erroring on trailing bytes.
    pub(super) fn parse_all(text: &'a str) -> Result<super::NbtValue> {
        let mut p = Parser::new(text.as_bytes());
        p.skip_ws();
        let v = p.value()?;
        p.skip_ws();
        if p.pos != p.bytes.len() {
            return Err(Error(format!("snbt: trailing input at byte {}", p.pos)));
        }
        Ok(v)
    }

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
