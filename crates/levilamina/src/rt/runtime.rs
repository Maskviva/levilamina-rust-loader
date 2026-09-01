use std::sync::OnceLock;

use crate::rt::handle::Handle;

#[cfg(feature = "client")]
use crate::client::Client;
use crate::logger::Logger;
use crate::packet::Packets;
#[cfg(feature = "server")]
use crate::server::Server;
use crate::sys;

pub(crate) struct Runtime {
    pub(crate) api: &'static sys::LeviRsApi,
    handle: Handle,
    pub(crate) loader_struct_size: usize,
}

static RUNTIME: OnceLock<Runtime> = OnceLock::new();

pub(crate) fn set_runtime(api: &'static sys::LeviRsApi, handle: sys::LeviRsModHandle) -> bool {
    let loader_struct_size = api.struct_size as usize;
    RUNTIME
        .set(Runtime {
            api,
            handle: Handle::new(handle),
            loader_struct_size,
        })
        .is_ok()
}

#[inline]
pub fn has_slot(offset: usize) -> bool {
    rt().loader_struct_size >= offset + core::mem::size_of::<usize>()
}

impl Runtime {
    pub(crate) fn handle(&self) -> sys::LeviRsModHandle {
        self.handle.get()
    }
}

pub(crate) fn rt() -> &'static Runtime {
    RUNTIME
        .get()
        .expect("levilamina runtime not initialized (register_mod! missing?)")
}

pub struct ModContext(());

impl ModContext {
    pub(crate) fn new() -> ModContext {
        ModContext(())
    }

    pub fn logger(&self) -> Logger {
        Logger::get()
    }

    pub fn packets(&self) -> Packets {
        Packets::get()
    }

    #[cfg(feature = "server")]
    pub fn server(&self) -> Server {
        Server::get()
    }

    #[cfg(feature = "client")]
    pub fn client(&self) -> Client {
        Client::get()
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct TaskId(pub(crate) u64);

impl TaskId {
    pub const NONE: TaskId = TaskId(0);

    pub fn is_valid(self) -> bool {
        self.0 != 0
    }

    pub fn raw(self) -> u64 {
        self.0
    }
}

#[macro_export]
macro_rules! require_slot {
    ($field:ident, $what:expr) => {
        if !$crate::__rt::has_slot(core::mem::offset_of!($crate::sys::LeviRsApi, $field)) {
            return Err($crate::Error(format!(
                "{} needs the loader function `{}`, which this loader does not \
                 provide. Update levilamina-rust-loader.",
                $what,
                stringify!($field)
            )));
        }
    };
}

#[macro_export]
macro_rules! has_slot {
    ($field:ident) => {
        $crate::__rt::has_slot(core::mem::offset_of!($crate::sys::LeviRsApi, $field))
    };
}
