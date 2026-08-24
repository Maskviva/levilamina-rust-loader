use super::*;
use crate::error::{Error, Result};
use crate::ffi::{collect_bytes, r, s};
use crate::types::PositionI32;
use crate::world::{BlockInfo, EntityInfo, Scan};
use crate::{rt, sys};

impl Server {
    pub fn scan_region(&self, dim: i32, a: PositionI32, b: PositionI32) -> Result<Scan> {
        let min = (a.0.min(b.0), a.1.min(b.1), a.2.min(b.2));
        let max = (a.0.max(b.0), a.1.max(b.1), a.2.max(b.2));
        let mut scan = Scan::new(min, max);

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

    pub fn set_block(&self, dim: i32, x: i32, y: i32, z: i32, spec: &str) -> Result<()> {
        let ok = unsafe { (rt().api.set_block)(dim, x, y, z, s(spec)) };
        if ok {
            Ok(())
        } else {
            Err(Error(format!("set_block failed for '{spec}'")))
        }
    }

    pub fn delete_chunk_keys(&self, dim: i32, chunk_x: i32, chunk_z: i32) -> Result<u32> {
        let mut keys: Vec<Vec<u8>> = Vec::new();
        let listed = unsafe {
            (rt().api.level_chunk_keys)(
                dim,
                chunk_x,
                chunk_z,
                (&mut keys as *mut Vec<Vec<u8>>).cast(),
                collect_bytes,
            )
        };
        if listed < 0 {
            return Err(Error(
                "delete_chunk_keys: level storage not available".into(),
            ));
        }
        let mut done = 0u32;
        for k in &keys {
            let sv = sys::LeviRsStr {
                ptr: k.as_ptr().cast(),
                len: k.len(),
            };
            if unsafe { (rt().api.level_delete_key)(sv) } {
                done += 1;
            }
        }
        Ok(done)
    }

    pub fn chunks_loaded(
        &self,
        dim: i32,
        min_x: i32,
        min_z: i32,
        max_x: i32,
        max_z: i32,
    ) -> Result<bool> {
        let r = unsafe { (rt().api.level_chunks_loaded)(dim, min_x, min_z, max_x, max_z) };
        match r {
            1 => Ok(true),
            0 => Ok(false),
            _ => Err(Error("chunks_loaded: dimension not available".into())),
        }
    }
}

impl Server {
    pub fn set_biome(&self, dim: i32, from: (i32, i32), to: (i32, i32), biome: &str) -> i32 {
        unsafe { (rt().api.level_set_biome)(dim, from.0, from.1, to.0, to.1, s(biome)) }
    }
}
