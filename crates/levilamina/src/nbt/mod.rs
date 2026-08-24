use std::collections::BTreeMap;

use crate::error::Result;

mod accessors;
mod binary;
mod parser;
mod serde;

pub use binary::NbtBinaryFormat;

#[derive(Debug, Clone, PartialEq)]
pub enum NbtValue {
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
    pub fn compound() -> NbtValue {
        NbtValue::Compound(BTreeMap::new())
    }

    pub fn parse(text: &str) -> Result<NbtValue> {
        parser::Parser::parse_all(text)
    }

    pub fn to_snbt(&self) -> String {
        let mut out = String::new();
        self.write(&mut out);
        out
    }
}
