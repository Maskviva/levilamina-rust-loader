//! Scalar productions: quoted strings (with escapes + UTF-8), bare tokens,
//! and the numeric/boolean dispatch that turns a bare token into a typed
//! [`NbtValue`].

use crate::error::Result;

use super::{super::super::NbtValue, utf8_len, Parser};

impl<'a> Parser<'a> {
    pub(super) fn quoted_string(&mut self) -> Result<String> {
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

    pub(super) fn bare_token(&mut self) -> Result<&'a str> {
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

    pub(super) fn scalar(&mut self) -> Result<NbtValue> {
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
