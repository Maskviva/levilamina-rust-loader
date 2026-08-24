use std::sync::OnceLock;

#[cfg(feature = "client")]
use crate::client::Client;
use crate::logger::Logger;
use crate::packet::Packets;
#[cfg(feature = "server")]
use crate::server::Server;
use crate::sys;

pub(crate) struct Runtime {
    pub(crate) api: &'static sys::LeviRsApi,
    pub(crate) handle: sys::LeviRsModHandle,
}
unsafe impl Send for Runtime {}
unsafe impl Sync for Runtime {}

static RUNTIME: OnceLock<Runtime> = OnceLock::new();

pub(crate) fn set_runtime(api: &'static sys::LeviRsApi, handle: sys::LeviRsModHandle) -> bool {
    RUNTIME.set(Runtime { api, handle }).is_ok()
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
