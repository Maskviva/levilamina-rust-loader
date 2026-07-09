//! Persistent key-value storage (LevelDB) confined to the mod's own data
//! directory. Thread-safe by bridge contract (a single mutex guards every
//! operation), so a [`KvDb`] may be shared with background threads.

use crate::error::{Error, Result};
use crate::ffi::{call_out_str, s};
use crate::{rt, sys};

/// An open key-value database. Closes on [`Drop`]; databases left open at
/// mod unload are force-closed by the loader (with a warning).
pub struct KvDb {
    handle: sys::LeviRsKvDbHandle,
}

// SAFETY: every bridge kvdb operation takes a global mutex; the handle is an
// opaque registry id, not a pointer into mod memory.
unsafe impl Send for KvDb {}
unsafe impl Sync for KvDb {}

impl KvDb {
    /// Open (or create) a database at `path`, which must be **relative** and
    /// stays inside the mod's data directory:
    /// `bds/data_mods/<your-mod>/<path>`.
    pub fn open(path: &str) -> Result<KvDb> {
        Self::open_with(path, true)
    }

    /// Open an existing database; fails if it doesn't exist.
    pub fn open_existing(path: &str) -> Result<KvDb> {
        Self::open_with(path, false)
    }

    fn open_with(path: &str, create_if_missing: bool) -> Result<KvDb> {
        let handle = unsafe { (rt().api.kvdb_open)(rt().handle, s(path), create_if_missing) };
        if handle.is_null() {
            Err(Error(format!(
                "kvdb open failed for '{path}' (path must be relative, no '..')"
            )))
        } else {
            Ok(KvDb { handle })
        }
    }

    /// `None` when the key doesn't exist.
    pub fn get(&self, key: &str) -> Option<String> {
        call_out_str(|ctx, sink| unsafe { (rt().api.kvdb_get)(self.handle, s(key), ctx, sink) })
    }

    pub fn set(&self, key: &str, value: &str) -> Result<()> {
        let ok = unsafe { (rt().api.kvdb_set)(self.handle, s(key), s(value)) };
        if ok {
            Ok(())
        } else {
            Err(Error(format!("kvdb set('{key}') failed")))
        }
    }

    pub fn del(&self, key: &str) -> Result<()> {
        let ok = unsafe { (rt().api.kvdb_del)(self.handle, s(key)) };
        if ok {
            Ok(())
        } else {
            Err(Error(format!("kvdb del('{key}') failed")))
        }
    }

    pub fn has(&self, key: &str) -> bool {
        unsafe { (rt().api.kvdb_has)(self.handle, s(key)) }
    }

    pub fn is_empty(&self) -> bool {
        unsafe { (rt().api.kvdb_is_empty)(self.handle) }
    }

    /// Every `(key, value)` pair. For large databases prefer keeping keys
    /// structured so you can [`KvDb::get`] directly instead.
    pub fn iter(&self) -> Vec<(String, String)> {
        use std::ffi::c_void;
        unsafe extern "C" fn sink(ctx: *mut c_void, key: sys::LeviRsStr, value: sys::LeviRsStr) {
            (*ctx.cast::<Vec<(String, String)>>()).push((
                crate::ffi::r(key).to_owned(),
                crate::ffi::r(value).to_owned(),
            ));
        }
        let mut out: Vec<(String, String)> = Vec::new();
        unsafe {
            (rt().api.kvdb_iter)(
                self.handle,
                (&mut out as *mut Vec<(String, String)>).cast(),
                sink,
            )
        };
        out
    }
}

impl Drop for KvDb {
    fn drop(&mut self) {
        unsafe { (rt().api.kvdb_close)(self.handle) }
    }
}
