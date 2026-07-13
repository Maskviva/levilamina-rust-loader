//! Command construction: [`CommandBuilder`] and per-overload [`OverloadBuilder`].

use super::*;
use crate::error::{Error, Result};
use crate::ffi::{r, s};
use crate::logger::Logger;
use crate::nbt::NbtValue;
use crate::{rt, sys};
use std::ffi::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};

#[derive(Debug, Clone)]
struct ParamDecl {
    name: String,
    kind: ParamType,
    enum_name: Option<String>,
    optional: bool,
}

/// One overload's parameter list, in declaration order.
#[derive(Debug, Clone, Default)]
pub struct OverloadBuilder {
    params: Vec<ParamDecl>,
}

impl OverloadBuilder {
    pub fn required(mut self, name: &str, kind: ParamType) -> Self {
        self.params.push(ParamDecl {
            name: name.into(),
            kind,
            enum_name: None,
            optional: false,
        });
        self
    }

    pub fn optional(mut self, name: &str, kind: ParamType) -> Self {
        self.params.push(ParamDecl {
            name: name.into(),
            kind,
            enum_name: None,
            optional: true,
        });
        self
    }

    /// A required enum / soft-enum parameter bound to a registered enum name
    /// (see [`crate::Server::register_command_enum`]).
    pub fn required_enum(mut self, name: &str, kind: ParamType, enum_name: &str) -> Self {
        self.params.push(ParamDecl {
            name: name.into(),
            kind,
            enum_name: Some(enum_name.into()),
            optional: false,
        });
        self
    }

    pub fn optional_enum(mut self, name: &str, kind: ParamType, enum_name: &str) -> Self {
        self.params.push(ParamDecl {
            name: name.into(),
            kind,
            enum_name: Some(enum_name.into()),
            optional: true,
        });
        self
    }
}

/// Builder for a parameterized command. Obtain via [`crate::Server::command`].
///
/// ```no_run
/// # use levilamina::prelude::*;
/// Server::get()
///     .command("warp", "teleport to a named warp", CommandPermission::Any)
///     .overload(|o| o.required("name", ParamType::String))
///     .overload(|o| o.required("name", ParamType::String).optional("who", ParamType::Player))
///     .register(|inv| {
///         let warp = inv.arg("name").and_then(|v| v.as_str()).unwrap_or_default();
///         inv.success(&format!("warping to {warp} (overload {})", inv.overload));
///     })
///     .unwrap();
/// ```
pub struct CommandBuilder {
    name: String,
    description: String,
    permission: CommandPermission,
    overloads: Vec<OverloadBuilder>,
}

impl CommandBuilder {
    pub(crate) fn new(name: &str, description: &str, permission: CommandPermission) -> Self {
        CommandBuilder {
            name: name.into(),
            description: description.into(),
            permission,
            overloads: Vec::new(),
        }
    }

    pub fn overload(mut self, build: impl FnOnce(OverloadBuilder) -> OverloadBuilder) -> Self {
        self.overloads.push(build(OverloadBuilder::default()));
        self
    }

    /// Register with the server. The handler lives for the whole server
    /// lifetime (Bedrock cannot unregister commands). Call from `on_enable`.
    pub fn register(self, handler: impl FnMut(&CommandInvocationEx) + 'static) -> Result<()> {
        if self.overloads.is_empty() {
            return Err(Error(
                "command builder: declare at least one overload".into(),
            ));
        }
        // Encode the declaration: {overloads:[[{name,kind,enum?,optional}, …], …]}
        let mut overloads_list = Vec::new();
        for o in &self.overloads {
            let mut params = Vec::new();
            for p in &o.params {
                let mut decl = NbtValue::compound();
                decl.insert("name", NbtValue::String(p.name.clone()));
                decl.insert("kind", NbtValue::String(p.kind.as_str().into()));
                if let Some(e) = &p.enum_name {
                    decl.insert("enum", NbtValue::String(e.clone()));
                }
                decl.insert("optional", NbtValue::Byte(if p.optional { 1 } else { 0 }));
                params.push(decl);
            }
            overloads_list.push(NbtValue::List(params));
        }
        let mut spec = NbtValue::compound();
        spec.insert("overloads", NbtValue::List(overloads_list));
        let spec_snbt = spec.to_snbt();

        type ExCallback = Box<dyn FnMut(&CommandInvocationEx)>;
        let cb: *mut ExCallback = Box::into_raw(Box::new(Box::new(handler)));

        unsafe extern "C" fn trampoline(
            user: *mut c_void,
            args: sys::LeviRsStr,
            origin: sys::LeviRsStr,
            out_ctx: *mut c_void,
            out_success: sys::LeviRsStrSink,
            out_error: sys::LeviRsStrSink,
        ) {
            type ExCallback = Box<dyn FnMut(&CommandInvocationEx)>;
            let cb = &mut *user.cast::<ExCallback>();
            let args_str = r(args);
            let origin_str = r(origin);

            let parsed = NbtValue::parse(args_str).unwrap_or_else(|_| NbtValue::compound());
            let overload = parsed.get("overload").and_then(|v| v.as_i64()).unwrap_or(0) as usize;
            let arg_values = parsed
                .get("args")
                .cloned()
                .unwrap_or_else(NbtValue::compound);

            let origin_v = NbtValue::parse(origin_str).unwrap_or_else(|_| NbtValue::compound());
            let origin = CommandOrigin {
                name: origin_v
                    .get("name")
                    .and_then(|v| v.as_str())
                    .unwrap_or_default()
                    .to_owned(),
                origin_type: origin_v.get("type").and_then(|v| v.as_i64()).unwrap_or(-1) as i32,
                dimension: origin_v
                    .get("dim")
                    .and_then(|v| v.as_i64())
                    .map(|v| v as i32),
                position: match (
                    origin_v.get("x").and_then(|v| v.as_f64()),
                    origin_v.get("y").and_then(|v| v.as_f64()),
                    origin_v.get("z").and_then(|v| v.as_f64()),
                ) {
                    (Some(x), Some(y), Some(z)) => Some((x, y, z)),
                    _ => None,
                },
            };

            let inv = CommandInvocationEx {
                overload,
                args: arg_values,
                origin,
                inner: CommandInvocation {
                    args: args_str,
                    origin: origin_str,
                    out_ctx,
                    out_success,
                    out_error,
                },
            };
            if catch_unwind(AssertUnwindSafe(|| cb(&inv))).is_err() {
                Logger::get().error("panic in command handler");
            }
        }

        let ok = unsafe {
            (rt().api.register_command_ex)(
                rt().handle,
                s(&self.name),
                s(&self.description),
                self.permission as i32,
                s(&spec_snbt),
                trampoline,
                cb.cast(),
            )
        };
        if ok {
            Ok(()) // callback intentionally leaked: commands live forever
        } else {
            unsafe { drop(Box::from_raw(cb)) };
            Err(Error(format!(
                "failed to register command '{}' (name taken / bad overloads?)",
                self.name
            )))
        }
    }
}
