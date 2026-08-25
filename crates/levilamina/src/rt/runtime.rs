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
    /// `LeviRsApi::struct_size` as reported by the loader we are actually
    /// running under — which may be SMALLER than `size_of::<LeviRsApi>()`
    /// on this crate. See [`has_slot`].
    pub(crate) loader_struct_size: usize,
}
unsafe impl Send for Runtime {}
unsafe impl Sync for Runtime {}

static RUNTIME: OnceLock<Runtime> = OnceLock::new();

pub(crate) fn set_runtime(api: &'static sys::LeviRsApi, handle: sys::LeviRsModHandle) -> bool {
    let loader_struct_size = api.struct_size as usize;
    RUNTIME
        .set(Runtime {
            api,
            handle,
            loader_struct_size,
        })
        .is_ok()
}

/// Is the slot at byte `offset` actually present in the loader's table?
///
/// The function table is append-only, so a loader older than this crate has a
/// table that is a *prefix* of ours: every slot it does have is at the same
/// offset and is byte-identical, and only the tail is missing. That makes
/// per-slot availability the precise question, and `struct_size` the exact
/// answer.
///
/// The alternative — refusing the whole loader when
/// `struct_size < size_of::<LeviRsApi>()` — is what this crate used to do,
/// and it is far too blunt. It compares the size of the struct *definition*,
/// not of the slots the mod calls, so simply rebuilding a mod against a newer
/// crate makes it reject every older loader even when the mod touches nothing
/// new. Mods stayed pinned to old crate versions for no reason.
///
/// Use [`crate::require_slot`] rather than calling this directly.
#[inline]
pub fn has_slot(offset: usize) -> bool {
    // A slot is present iff the loader's table extends past its last byte.
    // Every slot is a function pointer.
    rt().loader_struct_size >= offset + core::mem::size_of::<usize>()
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

/// Guard a call to a slot that was appended to `LeviRsApi` after ABI v1.
///
/// Expands to an early `return Err(...)` when the loader's table does not
/// reach that slot, so a mod built against a newer crate keeps working on an
/// older loader for everything it does *not* call.
///
/// ```ignore
/// pub fn send_title(...) -> Result<()> {
///     require_slot!(player_send_title, "Server::send_title");
///     unsafe { (rt().api.player_send_title)(...) };
///     Ok(())
/// }
/// ```
///
/// Without this the crate refused the whole loader up front, which meant a
/// mod that never touched `player_send_title` still could not run on a
/// loader that lacked it.
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

/// Non-`Result` form: yields `false` instead of returning an error.
#[macro_export]
macro_rules! has_slot {
    ($field:ident) => {
        $crate::__rt::has_slot(core::mem::offset_of!($crate::sys::LeviRsApi, $field))
    };
}
