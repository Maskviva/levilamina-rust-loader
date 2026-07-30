//! Raw packet interception: read and rewrite Bedrock wire bytes in flight.
//!
//! This is the layer below everything else in the crate. Where [`crate::event`]
//! hands you a deserialized `CompoundTag` and [`crate::server::Server`] hands
//! you gameplay objects, an interceptor sees a packet exactly as it will hit
//! the socket: an id and a body of bytes.
//!
//! ```no_run
//! use levilamina::prelude::*;
//! use levilamina::packet::{Direction, Verdict};
//!
//! # fn demo(ctx: &ModContext) -> Result<()> {
//! ctx.packets()
//!     .intercept(Direction::Both, |p| {
//!         if p.direction() == Direction::Inbound && p.packet_id() == 1 {
//!             // Login: stamp the server's protocol over the client's.
//!             let mut body = p.body().to_vec();
//!             body[..4].copy_from_slice(&800i32.to_be_bytes());
//!             p.set_body(&body);
//!         }
//!         Verdict::Forward
//!     })?
//!     .forget();
//! # Ok(())
//! # }
//! ```
//!
//! # Delivery unit
//!
//! One callback, one packet. The loader strips the leading varint header
//! before the call and rebuilds it afterwards, so [`PacketCtx::body`] is the
//! body alone and [`PacketCtx::set_packet_id`] remaps a packet without any
//! varint arithmetic. Batching and compression happen below the hook; you will
//! never see a batch, and you must never write a length prefix.
//!
//! # Threading — the part that differs from the rest of this crate
//!
//! Every other callback in `levilamina` runs on the game thread. These do not,
//! necessarily: an inbound callback runs wherever the connection is pumped and
//! an outbound one wherever the send originates. In practice that is the
//! server thread, but async flush means it is not guaranteed, and the ABI
//! declines to promise what it cannot.
//!
//! Two consequences, both enforced by the signature rather than by
//! documentation you have to remember:
//!
//! * the handler is `Fn + Send + Sync`, so shared state goes behind a `Mutex`
//!   (or an atomic) rather than being captured mutably;
//! * nothing here may touch the world. Use [`crate::server::Server::schedule`]
//!   to bounce work onto the game thread.
//!
//! Interceptors sit on the hottest loop in the server. Parse lazily, return
//! [`Verdict::Forward`] without copying whenever a packet isn't interesting,
//! and do not log per packet.

use std::ffi::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};

use crate::error::{Error, Result};
use crate::ffi::r;
use crate::logger::Logger;
use crate::{rt, sys};

/// Which way a packet is travelling.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Direction {
    /// Client to server.
    Inbound,
    /// Server to client.
    Outbound,
    /// Both — only meaningful when registering, never reported by an event.
    Both,
}

impl Direction {
    fn mask(self) -> i32 {
        match self {
            Direction::Inbound => sys::LEVI_RS_PKT_MASK_INBOUND,
            Direction::Outbound => sys::LEVI_RS_PKT_MASK_OUTBOUND,
            Direction::Both => sys::LEVI_RS_PKT_MASK_INBOUND | sys::LEVI_RS_PKT_MASK_OUTBOUND,
        }
    }

    fn from_raw(raw: i32) -> Direction {
        if raw == sys::LEVI_RS_PKT_OUTBOUND {
            Direction::Outbound
        } else {
            Direction::Inbound
        }
    }
}

/// What should happen to the packet once the handler returns.
///
/// Edits made through [`PacketCtx`] apply to [`Verdict::Forward`]; they are
/// irrelevant to [`Verdict::Drop`], which discards the packet outright.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Verdict {
    /// Send it on, carrying any edits made through the context.
    Forward,
    /// Swallow it. The peer never sees it, and on the inbound side the pump
    /// simply moves to the next packet.
    Drop,
}

/// One intercepted packet, plus the staging area for edits.
///
/// Borrowed for the duration of the callback: copy anything you keep.
pub struct PacketCtx<'a> {
    ev: &'a sys::LeviRsPacketEvent,
    edit: &'a mut sys::LeviRsPacketEdit,
    /// `None` until the handler calls [`PacketCtx::set_body`].
    new_body: Option<Vec<u8>>,
    /// Set by any mutation; drives whether the loader rebuilds the packet.
    dirty: bool,
}

impl PacketCtx<'_> {
    /// Which way this packet is going. Never [`Direction::Both`].
    pub fn direction(&self) -> Direction {
        Direction::from_raw(self.ev.direction)
    }

    /// Stable id for this connection, usable before a `Player` exists — which
    /// is exactly when login-phase rewriting happens. Pair it with
    /// [`Packets::on_connection`] to know when to drop per-connection state.
    pub fn conn_id(&self) -> u64 {
        self.ev.conn_id
    }

    /// `"host:port"`.
    pub fn address(&self) -> &str {
        // SAFETY: bridge contract — valid UTF-8 for the duration of the call.
        unsafe { r(self.ev.address) }
    }

    /// `MinecraftPacketIds` value, reflecting any [`PacketCtx::set_packet_id`].
    pub fn packet_id(&self) -> i32 {
        self.edit.packet_id
    }

    pub fn sender_sub_id(&self) -> u8 {
        self.edit.sender_sub_id
    }

    pub fn target_sub_id(&self) -> u8 {
        self.edit.target_sub_id
    }

    /// The packet body, header excluded — the staged replacement if
    /// [`PacketCtx::set_body`] was called, otherwise the original bytes.
    pub fn body(&self) -> &[u8] {
        if let Some(ref b) = self.new_body {
            return b;
        }
        if self.ev.body.is_null() || self.ev.body_len == 0 {
            return &[];
        }
        // SAFETY: bridge contract — `body_len` bytes live for the call.
        unsafe { std::slice::from_raw_parts(self.ev.body, self.ev.body_len) }
    }

    /// Replace the body. The header is rebuilt by the loader, so pass the body
    /// alone and never a length prefix.
    pub fn set_body(&mut self, bytes: &[u8]) {
        self.new_body = Some(bytes.to_vec());
        self.dirty = true;
    }

    /// Remap the packet to a different id, keeping the body.
    pub fn set_packet_id(&mut self, id: i32) {
        self.edit.packet_id = id;
        self.dirty = true;
    }

    pub fn set_sender_sub_id(&mut self, v: u8) {
        self.edit.sender_sub_id = v;
        self.dirty = true;
    }

    pub fn set_target_sub_id(&mut self, v: u8) {
        self.edit.target_sub_id = v;
        self.dirty = true;
    }
}

/// A connection opening or closing.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ConnectionState {
    Opened,
    Closed,
}

type PacketHandler = Box<dyn Fn(&mut PacketCtx) -> Verdict + Send + Sync>;
type ConnHandler = Box<dyn Fn(u64, &str, ConnectionState) + Send + Sync>;

/// A live interceptor. Dropping it detaches the handler (RAII); call
/// [`PacketHook::forget`] to keep it for the lifetime of the mod.
pub struct PacketHook {
    raw: sys::LeviRsPacketHookHandle,
    /// Type-erased owner of the boxed handler, freed on drop.
    drop_cb: unsafe fn(*mut c_void),
    cb: *mut c_void,
    /// Which unregister entry point owns `raw`.
    kind: HookKind,
}

#[derive(Clone, Copy)]
enum HookKind {
    Packet,
    Connection,
}

impl PacketHook {
    /// Keep this interceptor attached forever (leaks the handler — the right
    /// call for one installed at `on_enable` and meant to live as long as the
    /// mod).
    pub fn forget(mut self) {
        self.raw = std::ptr::null_mut();
        self.cb = std::ptr::null_mut();
        std::mem::forget(self);
    }
}

impl Drop for PacketHook {
    fn drop(&mut self) {
        if self.raw.is_null() {
            return;
        }
        let rt = rt();
        unsafe {
            let detached = match self.kind {
                HookKind::Packet => (rt.api.packet_hook_unregister)(rt.handle, self.raw),
                HookKind::Connection => (rt.api.packet_conn_hook_unregister)(rt.handle, self.raw),
            };
            // Only reclaim the handler once the loader promises it will not be
            // called again. A failed unregister means something is still
            // holding it, and freeing here would be a use-after-free on the
            // next packet.
            if detached {
                (self.drop_cb)(self.cb);
            }
        }
    }
}

// SAFETY: `raw`/`cb` are plain identifiers handed back to the loader on drop;
// the handler behind `cb` is `Send + Sync` by construction. The hook itself
// carries no thread affinity.
unsafe impl Send for PacketHook {}
unsafe impl Sync for PacketHook {}

unsafe fn drop_packet_handler(p: *mut c_void) {
    drop(Box::from_raw(p.cast::<PacketHandler>()));
}

unsafe fn drop_conn_handler(p: *mut c_void) {
    drop(Box::from_raw(p.cast::<ConnHandler>()));
}

unsafe extern "C" fn packet_trampoline(
    user: *mut c_void,
    ev: *const sys::LeviRsPacketEvent,
    edit: *mut sys::LeviRsPacketEdit,
    replace_ctx: *mut c_void,
    replace: sys::LeviRsBytesSink,
) -> i32 {
    if user.is_null() || ev.is_null() || edit.is_null() {
        return sys::LEVI_RS_PKT_PASS;
    }
    let handler = &*user.cast::<PacketHandler>();
    let mut ctx = PacketCtx {
        ev: &*ev,
        edit: &mut *edit,
        new_body: None,
        dirty: false,
    };

    // A panicking handler must not unwind into C++. Pass the packet through
    // untouched: a corrupted stream is far worse than a missed translation.
    let verdict = match catch_unwind(AssertUnwindSafe(|| handler(&mut ctx))) {
        Ok(v) => v,
        Err(_) => {
            Logger::get().error("panic in packet interceptor; packet forwarded unchanged");
            return sys::LEVI_RS_PKT_PASS;
        }
    };

    if verdict == Verdict::Drop {
        return sys::LEVI_RS_PKT_DROP;
    }
    if !ctx.dirty {
        return sys::LEVI_RS_PKT_PASS;
    }

    // Dirty but no new body (an id-only remap): echo the original bytes back,
    // otherwise the loader would rebuild the packet with an empty body.
    let body = ctx.body();
    replace(replace_ctx, body.as_ptr(), body.len());
    sys::LEVI_RS_PKT_REPLACE
}

unsafe extern "C" fn conn_trampoline(
    user: *mut c_void,
    conn_id: u64,
    address: sys::LeviRsStr,
    opened: bool,
) {
    if user.is_null() {
        return;
    }
    let handler = &*user.cast::<ConnHandler>();
    let state = if opened {
        ConnectionState::Opened
    } else {
        ConnectionState::Closed
    };
    let addr = r(address);
    if catch_unwind(AssertUnwindSafe(|| handler(conn_id, addr, state))).is_err() {
        Logger::get().error("panic in connection handler");
    }
}

/// Entry point for packet interception. Obtain one via [`crate::ModContext::packets`].
#[derive(Clone, Copy)]
pub struct Packets(());

impl Packets {
    pub(crate) fn get() -> Packets {
        Packets(())
    }

    /// Attach an interceptor for one or both directions.
    ///
    /// With several interceptors registered, each sees the previous one's
    /// output, in registration order; the first [`Verdict::Drop`] wins and the
    /// rest are skipped. Registering or unregistering from inside a handler is
    /// safe.
    pub fn intercept(
        &self,
        direction: Direction,
        handler: impl Fn(&mut PacketCtx) -> Verdict + Send + Sync + 'static,
    ) -> Result<PacketHook> {
        let boxed: *mut PacketHandler = Box::into_raw(Box::new(Box::new(handler)));
        let raw = unsafe {
            (rt().api.packet_hook_register)(
                rt().handle,
                direction.mask(),
                packet_trampoline,
                boxed.cast(),
            )
        };
        if raw.is_null() {
            unsafe { drop_packet_handler(boxed.cast()) };
            return Err(Error::new("failed to register packet interceptor"));
        }
        Ok(PacketHook {
            raw,
            drop_cb: drop_packet_handler,
            cb: boxed.cast(),
            kind: HookKind::Packet,
        })
    }

    /// Observe connections opening and closing.
    ///
    /// This is the only reliable place to drop per-connection state: a
    /// connection that never finishes the login handshake never becomes a
    /// `Player`, so no player event ever fires for it.
    pub fn on_connection(
        &self,
        handler: impl Fn(u64, &str, ConnectionState) + Send + Sync + 'static,
    ) -> Result<PacketHook> {
        let boxed: *mut ConnHandler = Box::into_raw(Box::new(Box::new(handler)));
        let raw = unsafe {
            (rt().api.packet_conn_hook_register)(rt().handle, conn_trampoline, boxed.cast())
        };
        if raw.is_null() {
            unsafe { drop_conn_handler(boxed.cast()) };
            return Err(Error::new("failed to register connection observer"));
        }
        Ok(PacketHook {
            raw,
            drop_cb: drop_conn_handler,
            cb: boxed.cast(),
            kind: HookKind::Connection,
        })
    }
}
