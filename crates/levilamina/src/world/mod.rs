pub mod scan;
pub mod structures;

use crate::types::PositionI32;
pub use scan::*;
pub use structures::*;

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Bounds {
    pub min: PositionI32,
    pub max: PositionI32,
}
