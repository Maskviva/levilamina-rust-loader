use crate::error::{Error, Result};
use crate::ffi::{call_out_str, s};
use crate::rt::handle::Handle;
use crate::{rt, sys};

pub struct KvDb {
    handle: Handle,
}

impl KvDb {
    pub fn open(path: &str) -> Result<KvDb> {
        Self::open_with(path, true)
    }

    pub fn open_existing(path: &str) -> Result<KvDb> {
        Self::open_with(path, false)
    }

    fn open_with(path: &str, create_if_missing: bool) -> Result<KvDb> {
        let handle = unsafe { (rt().api.kvdb_open)(rt().handle(), s(path), create_if_missing) };
        if handle.is_null() {
            Err(Error(format!(
                "kvdb open failed for '{path}' (path must be relative, no '..')"
            )))
        } else {
            Ok(KvDb {
                handle: Handle::new(handle),
            })
        }
    }

    pub fn get(&self, key: &str) -> Option<String> {
        call_out_str(|ctx, sink| unsafe {
            (rt().api.kvdb_get)(self.handle.get(), s(key), ctx, sink)
        })
    }

    pub fn set(&self, key: &str, value: &str) -> Result<()> {
        let ok = unsafe { (rt().api.kvdb_set)(self.handle.get(), s(key), s(value)) };
        if ok {
            Ok(())
        } else {
            Err(Error(format!("kvdb set('{key}') failed")))
        }
    }

    pub fn del(&self, key: &str) -> Result<()> {
        let ok = unsafe { (rt().api.kvdb_del)(self.handle.get(), s(key)) };
        if ok {
            Ok(())
        } else {
            Err(Error(format!("kvdb del('{key}') failed")))
        }
    }

    pub fn has(&self, key: &str) -> bool {
        unsafe { (rt().api.kvdb_has)(self.handle.get(), s(key)) }
    }

    pub fn is_empty(&self) -> bool {
        unsafe { (rt().api.kvdb_is_empty)(self.handle.get()) }
    }

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
                self.handle.get(),
                (&mut out as *mut Vec<(String, String)>).cast(),
                sink,
            )
        };
        out
    }
}

impl Drop for KvDb {
    fn drop(&mut self) {
        unsafe { (rt().api.kvdb_close)(self.handle.get()) }
    }
}
