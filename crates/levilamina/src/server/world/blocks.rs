//! Block-level world access: region scan, single-block get/set.

use super::*;
use crate::error::{Error, Result};
use crate::ffi::{r, s};
use crate::types::PositionI32;
use crate::world::{BlockInfo, EntityInfo, Scan};
use crate::{rt, sys};

impl Server {
    /// Scan a cuboid region (corners inclusive, order-independent) into a
    /// [`Scan`]: one [`crate::ScanLayer`] per Y level, each a 2-D grid of
    /// [`crate::Cell`]s holding the block and any entities in that cell.
    /// Server thread only.
    pub fn scan_region(&self, dim: i32, a: PositionI32, b: PositionI32) -> Result<Scan> {
        let min = (a.0.min(b.0), a.1.min(b.1), a.2.min(b.2));
        let max = (a.0.max(b.0), a.1.max(b.1), a.2.max(b.2));
        let mut scan = Scan::new(min, max);

        // The sinks push into `scan` via a raw pointer valid only for this call.
        unsafe extern "C" fn block_sink(
            ctx: *mut c_void,
            x: i32,
            y: i32,
            z: i32,
            name: sys::LeviRsStr,
            snbt: sys::LeviRsStr,
        ) {
            let scan = &mut *ctx.cast::<Scan>();
            if let Some(cell) = scan.cell_mut(x, y, z) {
                cell.block = BlockInfo {
                    name: r(name).to_owned(),
                    snbt: r(snbt).to_owned(),
                };
            }
        }
        unsafe extern "C" fn entity_sink(
            ctx: *mut c_void,
            x: i32,
            y: i32,
            z: i32,
            kind: sys::LeviRsStr,
            snbt: sys::LeviRsStr,
        ) {
            let scan = &mut *ctx.cast::<Scan>();
            if let Some(cell) = scan.cell_mut(x, y, z) {
                cell.entities.push(EntityInfo {
                    kind: r(kind).to_owned(),
                    snbt: r(snbt).to_owned(),
                });
            }
        }

        let ok = unsafe {
            (rt().api.scan_region)(
                dim,
                min.0,
                min.1,
                min.2,
                max.0,
                max.1,
                max.2,
                (&mut scan as *mut Scan).cast(),
                block_sink,
                entity_sink,
            )
        };
        if ok {
            Ok(scan)
        } else {
            Err(Error("level/dimension not ready".into()))
        }
    }

    /// Read one block: `(type_name, serialization SNBT)`. Prefer
    /// [`crate::Block::at`] for the full property surface.
    pub fn get_block(&self, dim: i32, x: i32, y: i32, z: i32) -> Result<(String, String)> {
        unsafe extern "C" fn sink(
            ctx: *mut c_void,
            _x: i32,
            _y: i32,
            _z: i32,
            name: sys::LeviRsStr,
            snbt: sys::LeviRsStr,
        ) {
            *ctx.cast::<Option<(String, String)>>() =
                Some((r(name).to_owned(), r(snbt).to_owned()));
        }
        let mut out: Option<(String, String)> = None;
        let ok = unsafe {
            (rt().api.get_block)(
                dim,
                x,
                y,
                z,
                (&mut out as *mut Option<(String, String)>).cast(),
                sink,
            )
        };
        if !ok {
            return Err(Error("get_block: level/dimension not ready".into()));
        }
        out.ok_or_else(|| Error("get_block: no result".into()))
    }

    /// Replace one block. `spec` is anything `/setblock` accepts.
    pub fn set_block(&self, dim: i32, x: i32, y: i32, z: i32, spec: &str) -> Result<()> {
        let ok = unsafe { (rt().api.set_block)(dim, x, y, z, s(spec)) };
        if ok {
            Ok(())
        } else {
            Err(Error(format!("set_block failed for '{spec}'")))
        }
    }
}
