use core::ffi::c_void;

use crate::api::LeviRsApi;
use crate::types::LeviRsModHandle;

#[repr(C)]
pub struct LeviRsModVTable {
    pub abi_version: u32,
    pub instance: *mut c_void,
    pub on_enable: Option<unsafe extern "C" fn(instance: *mut c_void) -> bool>,
    pub on_disable: Option<unsafe extern "C" fn(instance: *mut c_void) -> bool>,
    pub on_unload: Option<unsafe extern "C" fn(instance: *mut c_void) -> bool>,
}

pub type LeviRsMainFn = unsafe extern "C" fn(
    api: *const LeviRsApi,
    self_: LeviRsModHandle,
    out_vtable: *mut LeviRsModVTable,
) -> bool;
