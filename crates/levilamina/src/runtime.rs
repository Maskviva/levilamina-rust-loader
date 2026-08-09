//! Runtime plumbing: the FFI API table handle and the [`ModContext`] facade.
//!
//! Everything in this module is `pub(crate)` except [`ModContext`], which is
//! part of the public lifecycle API. The single global [`Runtime`] is set
//! exactly once by [`crate::__init_runtime`] during `levi_rs_main`.

use std::sync::OnceLock;

#[cfg(feature = "client")]
use crate::client::Client;
use crate::logger::Logger;
use crate::packet::Packets;
#[cfg(feature = "server")]
use crate::server::Server;
use crate::sys;

/// Process-wide handle to the bridge-provided API table + mod handle.
///
/// The `api` reference is `&'static` because the bridge guarantees the table
/// outlives the mod dylib. The `handle` is only ever dereferenced by the
/// bridge on the game thread, so [`Send`] + [`Sync`] are sound here even
/// though the handle itself is a raw pointer in the FFI layer.
pub(crate) struct Runtime {
    pub(crate) api: &'static sys::LeviRsApi,
    pub(crate) handle: sys::LeviRsModHandle,
}
unsafe impl Send for Runtime {}
unsafe impl Sync for Runtime {}

static RUNTIME: OnceLock<Runtime> = OnceLock::new();

/// Install the runtime. Called exactly once from `__init_runtime`; returns
/// `false` if a runtime was already installed (double registration).
pub(crate) fn set_runtime(api: &'static sys::LeviRsApi, handle: sys::LeviRsModHandle) -> bool {
    RUNTIME.set(Runtime { api, handle }).is_ok()
}

/// Borrow the live runtime. Panics if called before `register_mod!` has run
/// (i.e. outside a mod lifecycle / callback context).
pub(crate) fn rt() -> &'static Runtime {
    RUNTIME
        .get()
        .expect("levilamina runtime not initialized (register_mod! missing?)")
}

/// Everything a mod needs, passed to lifecycle hooks.
///
/// A thin facade over the runtime singletons ([`Logger`], [`Server`] or
/// [`Client`]); it carries no state of its own and is cheap to pass by value.
pub struct ModContext(());

impl ModContext {
    /// Construct a `ModContext` for a lifecycle hook. Only callable from
    /// within this crate (the bridge entry points in [`crate::registration`]).
    pub(crate) fn new() -> ModContext {
        ModContext(())
    }

    pub fn logger(&self) -> Logger {
        Logger::get()
    }

    /// Raw packet interception. Available on both targets: the ABI slots sit
    /// before the client-only block, so a server and a client loader both
    /// provide them.
    ///
    /// Read [`crate::packet`] before using this — the callbacks are the one
    /// place in this crate that does not promise the game thread.
    pub fn packets(&self) -> Packets {
        Packets::get()
    }

    /// Access the server-side API surface. Only available with the `server`
    /// feature (server loader build).
    #[cfg(feature = "server")]
    pub fn server(&self) -> Server {
        Server::get()
    }

    /// Access the client-side API surface. Only available with the `client`
    /// feature (client loader build).
    #[cfg(feature = "client")]
    pub fn client(&self) -> Client {
        Client::get()
    }
}

/// Handle to a scheduled task, returned by `schedule` / `schedule_after`.
///
/// Lives here rather than in `server` / `client` because those two modules are
/// mutually exclusive cargo features and both need this type.
///
/// Loader-side ids are per-process and monotonically increasing, so an id is
/// never reused: a stale `TaskId` cancels nothing rather than cancelling some
/// unrelated task that happened to reuse the number.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct TaskId(pub(crate) u64);

impl TaskId {
    /// The id the loader returns when a task could not be registered.
    pub const NONE: TaskId = TaskId(0);

    /// False when scheduling failed (null callback, or the mod is going away).
    pub fn is_valid(self) -> bool {
        self.0 != 0
    }

    pub fn raw(self) -> u64 {
        self.0
    }
}
