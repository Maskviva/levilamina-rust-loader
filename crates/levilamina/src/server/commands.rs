//! `Server` command surface: run, register (raw + parameterised), and enums.

use super::*;
use crate::command::{CommandBuilder, CommandInvocation, CommandPermission, CommandResult};
use crate::error::{Error, Result};
use crate::ffi::{r, s};
use crate::logger::Logger;
use crate::nbt::NbtValue;
use crate::{rt, sys};

impl Server {
    /// Execute a command as the server console and collect its output.
    /// Server thread only.
    pub fn execute_command(&self, cmd: &str) -> Result<CommandResult> {
        let mut result = CommandResult {
            success: false,
            output: String::new(),
        };
        unsafe extern "C" fn sink(ctx: *mut c_void, success: bool, output: sys::LeviRsStr) {
            let res = &mut *ctx.cast::<CommandResult>();
            res.success = success;
            res.output = r(output).to_owned();
        }
        let ok = unsafe {
            (rt().api.execute_command)(s(cmd), (&mut result as *mut CommandResult).cast(), sink)
        };
        if ok {
            Ok(result)
        } else {
            Err(Error("level not ready (server still starting?)".into()))
        }
    }

    /// Register `/name [args…]` taking one raw-text argument. The handler
    /// lives for the whole server lifetime (Bedrock cannot unregister
    /// commands). Call from `on_enable`. For typed parameters use
    /// [`Server::command`] instead.
    pub fn register_command(
        &self,
        name: &str,
        description: &str,
        permission: CommandPermission,
        handler: impl FnMut(&CommandInvocation) + 'static,
    ) -> Result<()> {
        type CommandCallback = Box<dyn FnMut(&CommandInvocation)>;
        let cb: *mut CommandCallback = Box::into_raw(Box::new(Box::new(handler)));

        unsafe extern "C" fn trampoline(
            user: *mut c_void,
            args: sys::LeviRsStr,
            origin: sys::LeviRsStr,
            out_ctx: *mut c_void,
            out_success: sys::LeviRsStrSink,
            out_error: sys::LeviRsStrSink,
        ) {
            type CommandCallback = Box<dyn FnMut(&CommandInvocation)>;
            let cb = &mut *user.cast::<CommandCallback>();
            let inv = CommandInvocation {
                args: r(args),
                origin: r(origin),
                out_ctx,
                out_success,
                out_error,
            };
            if catch_unwind(AssertUnwindSafe(|| cb(&inv))).is_err() {
                Logger::get().error("panic in command handler");
            }
        }

        let ok = unsafe {
            (rt().api.register_command)(
                rt().handle,
                s(name),
                s(description),
                permission as i32,
                trampoline,
                cb.cast(),
            )
        };
        if ok {
            Ok(()) // callback intentionally leaked: commands live forever
        } else {
            unsafe { drop(Box::from_raw(cb)) };
            Err(Error(format!("failed to register command '{name}'")))
        }
    }

    /// Start building a parameterized command with typed overloads.
    /// See [`CommandBuilder`] for a full example. Call from `on_enable`.
    pub fn command(
        &self,
        name: &str,
        description: &str,
        permission: CommandPermission,
    ) -> CommandBuilder {
        CommandBuilder::new(name, description, permission)
    }

    /// Register a hard enum for `ParamType::Enum` parameters:
    /// `server.register_command_enum("warp_action", &[("add", 0), ("remove", 1)])`.
    pub fn register_command_enum(&self, name: &str, values: &[(&str, u64)]) -> Result<()> {
        let list = NbtValue::List(
            values
                .iter()
                .map(|(v, idx)| {
                    NbtValue::List(vec![
                        NbtValue::String((*v).to_owned()),
                        NbtValue::Long(*idx as i64),
                    ])
                })
                .collect(),
        );
        let mut spec = NbtValue::compound();
        spec.insert("values", list);
        let ok = unsafe { (rt().api.register_command_enum)(s(name), s(&spec.to_snbt())) };
        if ok {
            Ok(())
        } else {
            Err(Error(format!("register_command_enum('{name}') failed")))
        }
    }

    /// Register a soft enum (suggestions only; free text still accepted).
    pub fn register_command_soft_enum(&self, name: &str, values: &[&str]) -> Result<()> {
        let mut spec = NbtValue::compound();
        spec.insert(
            "values",
            NbtValue::List(
                values
                    .iter()
                    .map(|v| NbtValue::String((*v).to_owned()))
                    .collect(),
            ),
        );
        let ok = unsafe { (rt().api.register_command_soft_enum)(s(name), s(&spec.to_snbt())) };
        if ok {
            Ok(())
        } else {
            Err(Error(format!(
                "register_command_soft_enum('{name}') failed"
            )))
        }
    }

    /// Update a soft enum's values: `op` semantics — replace all / add / remove.
    pub fn update_command_soft_enum(
        &self,
        name: &str,
        op: SoftEnumOp,
        values: &[&str],
    ) -> Result<()> {
        let mut spec = NbtValue::compound();
        spec.insert(
            "values",
            NbtValue::List(
                values
                    .iter()
                    .map(|v| NbtValue::String((*v).to_owned()))
                    .collect(),
            ),
        );
        let ok =
            unsafe { (rt().api.update_command_soft_enum)(s(name), op as i32, s(&spec.to_snbt())) };
        if ok {
            Ok(())
        } else {
            Err(Error(format!("update_command_soft_enum('{name}') failed")))
        }
    }
}
