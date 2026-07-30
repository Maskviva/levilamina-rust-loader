//! SNBT parser (internal): drives [`super::NbtValue::parse`].
//!
//! The recursive-descent `impl Parser` is split across sibling files by
//! grammar concern so each fits under the 200-line ceiling:
//!   - [`containers`] — compounds, lists, typed arrays
//!   - [`scalars`]    — quoted strings, bare tokens, numeric/bool scalars

use crate::error::{Error, Result};

mod containers;
mod scalars;

pub(super) struct Parser<'a> {
    bytes: &'a [u8],
    pos: usize,
}

impl<'a> Parser<'a> {
    /// Construct a parser over SNBT bytes (used by [`super::super::NbtValue::parse`]).
    pub(super) fn new(bytes: &'a [u8]) -> Parser<'a> {
        Parser { bytes, pos: 0 }
    }

    /// Parse the whole input, erroring on trailing bytes.
    pub(super) fn parse_all(text: &'a str) -> Result<super::super::NbtValue> {
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
}

/// Leading-byte → UTF-8 sequence length, used by [`scalars::quoted_string`].
pub(super) fn utf8_len(first: u8) -> usize {
    match first {
        b if b >= 0xF0 => 4,
        b if b >= 0xE0 => 3,
        b if b >= 0xC0 => 2,
        _ => 1,
    }
}
