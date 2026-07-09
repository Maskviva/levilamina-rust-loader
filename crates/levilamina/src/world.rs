//! World-scan value types (ABI v3, unchanged from v0.x): [`Scan`] /
//! [`ScanLayer`] / [`Cell`] / [`BlockInfo`] / [`EntityInfo`] / [`PlayerPos`].

/// A connected player's feet position and dimension.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct PlayerPos {
    pub x: f64,
    pub y: f64,
    pub z: f64,
    pub dim: i32,
}

impl PlayerPos {
    /// The integer block cell the player is standing in.
    pub fn block(&self) -> (i32, i32, i32) {
        (
            self.x.floor() as i32,
            self.y.floor() as i32,
            self.z.floor() as i32,
        )
    }
}

/// The block occupying a cell: its type name and full serialization SNBT.
/// An empty cell has `name == "minecraft:air"`.
#[derive(Debug, Clone, Default)]
pub struct BlockInfo {
    pub name: String,
    pub snbt: String,
}

impl BlockInfo {
    /// True if this cell holds no real block (air).
    pub fn is_air(&self) -> bool {
        self.name.is_empty() || self.name.ends_with("air")
    }
}

/// An entity found inside a cell: its type name and serialized NBT (SNBT).
#[derive(Debug, Clone)]
pub struct EntityInfo {
    pub kind: String,
    pub snbt: String,
}

/// Everything at one grid cell: the block plus any entities standing in it.
#[derive(Debug, Clone, Default)]
pub struct Cell {
    pub block: BlockInfo,
    pub entities: Vec<EntityInfo>,
}

impl Cell {
    /// True if the cell is air with no entities.
    pub fn is_empty(&self) -> bool {
        self.block.is_air() && self.entities.is_empty()
    }
}

/// One horizontal layer (a single Y level) of a [`Scan`]: a 2-D grid of cells
/// indexed `cells[x_index][z_index]`, where the indices are offsets from the
/// region's minimum corner.
#[derive(Debug, Clone)]
pub struct ScanLayer {
    pub y: i32,
    /// `cells[dx][dz]`, dx over X (west→east), dz over Z (north→south).
    pub cells: Vec<Vec<Cell>>,
}

/// The result of [`crate::Server::scan_region`]: a stack of [`ScanLayer`]s,
/// one per Y level from the bottom of the region up. A six-block-tall
/// selection yields six layers, each a 2-D array whose every element is a
/// [`Cell`] describing the block and entities at that grid position.
#[derive(Debug, Clone)]
pub struct Scan {
    pub min: (i32, i32, i32),
    pub max: (i32, i32, i32),
    /// One entry per Y level, from `min.1` (index 0) up to `max.1`.
    pub layers: Vec<ScanLayer>,
}

impl Scan {
    pub(crate) fn new(min: (i32, i32, i32), max: (i32, i32, i32)) -> Self {
        let size_x = (max.0 - min.0 + 1).max(0) as usize;
        let size_z = (max.2 - min.2 + 1).max(0) as usize;
        let layers = (min.1..=max.1)
            .map(|y| ScanLayer {
                y,
                cells: vec![vec![Cell::default(); size_z]; size_x],
            })
            .collect();
        Scan { min, max, layers }
    }

    pub(crate) fn cell_mut(&mut self, x: i32, y: i32, z: i32) -> Option<&mut Cell> {
        let dy = (y - self.min.1) as usize;
        let dx = (x - self.min.0) as usize;
        let dz = (z - self.min.2) as usize;
        self.layers.get_mut(dy)?.cells.get_mut(dx)?.get_mut(dz)
    }

    /// Dimensions of the scanned box as `(size_x, size_y, size_z)`.
    pub fn size(&self) -> (usize, usize, usize) {
        (
            (self.max.0 - self.min.0 + 1) as usize,
            (self.max.1 - self.min.1 + 1) as usize,
            (self.max.2 - self.min.2 + 1) as usize,
        )
    }

    /// Total non-empty cells (a non-air block or at least one entity).
    pub fn non_empty_count(&self) -> usize {
        self.layers
            .iter()
            .flat_map(|l| l.cells.iter().flatten())
            .filter(|c| !c.is_empty())
            .count()
    }

    /// Total entities across the whole region.
    pub fn entity_count(&self) -> usize {
        self.layers
            .iter()
            .flat_map(|l| l.cells.iter().flatten())
            .map(|c| c.entities.len())
            .sum()
    }
}
