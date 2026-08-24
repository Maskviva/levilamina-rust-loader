use std::ffi::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};

use crate::error::{Error, Result};
use crate::ffi::r;
use crate::logger::Logger;
use crate::{rt, sys};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Direction {
    Inbound,

    Outbound,

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

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Verdict {
    Forward,

    Drop,
}

pub struct PacketCtx<'a> {
    ev: &'a sys::LeviRsPacketEvent,
    edit: &'a mut sys::LeviRsPacketEdit,

    new_body: Option<Vec<u8>>,

    dirty: bool,
}

impl PacketCtx<'_> {
    pub fn direction(&self) -> Direction {
        Direction::from_raw(self.ev.direction)
    }

    pub fn conn_id(&self) -> u64 {
        self.ev.conn_id
    }

    pub fn address(&self) -> &str {
        unsafe { r(self.ev.address) }
    }

    pub fn packet_id(&self) -> i32 {
        self.edit.packet_id
    }

    pub fn sender_sub_id(&self) -> u8 {
        self.edit.sender_sub_id
    }

    pub fn target_sub_id(&self) -> u8 {
        self.edit.target_sub_id
    }

    pub fn body(&self) -> &[u8] {
        if let Some(ref b) = self.new_body {
            return b;
        }
        if self.ev.body.is_null() || self.ev.body_len == 0 {
            return &[];
        }

        unsafe { std::slice::from_raw_parts(self.ev.body, self.ev.body_len) }
    }

    pub fn set_body(&mut self, bytes: &[u8]) {
        self.new_body = Some(bytes.to_vec());
        self.dirty = true;
    }

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

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ConnectionState {
    Opened,
    Closed,
}

type PacketHandler = Box<dyn Fn(&mut PacketCtx) -> Verdict + Send + Sync>;
type ConnHandler = Box<dyn Fn(u64, &str, ConnectionState) + Send + Sync>;

pub struct PacketHook {
    raw: sys::LeviRsPacketHookHandle,

    drop_cb: unsafe fn(*mut c_void),
    cb: *mut c_void,

    kind: HookKind,
}

#[derive(Clone, Copy)]
enum HookKind {
    Packet,
    Connection,
}

impl PacketHook {
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

            if detached {
                (self.drop_cb)(self.cb);
            }
        }
    }
}

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

#[derive(Clone, Copy)]
pub struct Packets(());

impl Packets {
    pub(crate) fn get() -> Packets {
        Packets(())
    }

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
