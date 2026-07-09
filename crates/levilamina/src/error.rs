//! Crate-wide error type. One string-carrying error keeps the FFI surface
//! honest: the bridge reports failures as booleans, so the safe layer's job
//! is to attach *why* from context, not to taxonomize.

#[derive(Debug)]
pub struct Error(pub String);

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.0)
    }
}
impl std::error::Error for Error {}

impl Error {
    pub fn new(msg: impl std::fmt::Display) -> Self {
        Error(msg.to_string())
    }
}

impl From<String> for Error {
    fn from(s: String) -> Self {
        Error(s)
    }
}
impl From<&str> for Error {
    fn from(s: &str) -> Self {
        Error(s.to_owned())
    }
}

pub type Result<T> = std::result::Result<T, Error>;
