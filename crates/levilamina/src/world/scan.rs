use crate::types::PositionI32;

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct PlayerPos {
    pub x: f64,
    pub y: f64,
    pub z: f64,
    pub dim: i32,
}

impl PlayerPos {
    pub fn block(&self) -> PositionI32 {
        (
            self.x.floor() as i32,
            self.y.floor() as i32,
            self.z.floor() as i32,
        )
    }
}

#[derive(Debug, Clone, Default)]
pub struct BlockInfo {
    pub name: String,
    pub snbt: String,
}

impl BlockInfo {
    pub fn is_air(&self) -> bool {
        self.name.is_empty() || self.name.ends_with("air")
    }
}

#[derive(Debug, Clone)]
pub struct EntityInfo {
    pub kind: String,
    pub snbt: String,
}

#[derive(Debug, Clone, Default)]
pub struct Cell {
    pub block: BlockInfo,
    pub entities: Vec<EntityInfo>,
}

impl Cell {
    pub fn is_empty(&self) -> bool {
        self.block.is_air() && self.entities.is_empty()
    }
}

#[derive(Debug, Clone)]
pub struct ScanLayer {
    pub y: i32,

    pub cells: Vec<Vec<Cell>>,
}

#[derive(Debug, Clone)]
pub struct Scan {
    pub min: PositionI32,
    pub max: PositionI32,

    pub layers: Vec<ScanLayer>,
}

impl Scan {
    pub(crate) fn new(min: PositionI32, max: PositionI32) -> Self {
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

    pub fn size(&self) -> (usize, usize, usize) {
        (
            (self.max.0 - self.min.0 + 1) as usize,
            (self.max.1 - self.min.1 + 1) as usize,
            (self.max.2 - self.min.2 + 1) as usize,
        )
    }

    pub fn non_empty_count(&self) -> usize {
        self.layers
            .iter()
            .flat_map(|l| l.cells.iter().flatten())
            .filter(|c| !c.is_empty())
            .count()
    }

    pub fn entity_count(&self) -> usize {
        self.layers
            .iter()
            .flat_map(|l| l.cells.iter().flatten())
            .map(|c| c.entities.len())
            .sum()
    }
}
