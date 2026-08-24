use std::fmt::Write as _;

use super::NbtValue;

impl NbtValue {
    pub(super) fn write(&self, out: &mut String) {
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
