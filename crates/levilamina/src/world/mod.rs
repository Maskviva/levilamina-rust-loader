//! World-scan value types (ABI v3, unchanged from v0.x): [`Scan`] /
//! [`ScanLayer`] / [`Cell`] / [`BlockInfo`] / [`EntityInfo`] / [`PlayerPos`].

pub mod scan;
pub mod structures;

pub use scan::*;
pub use structures::*;

/// An axis-aligned integer box `[min, max]` (inclusive), as used by village
/// and structure bounds.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Bounds {
    pub min: (i32, i32, i32),
    pub max: (i32, i32, i32),
}
