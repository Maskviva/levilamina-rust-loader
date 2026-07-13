//! Commands: the v0.x raw-text handler plus the v1.0.0 parameterized
//! builder ([`CommandBuilder`]) with typed overloads, enums and soft enums.

use std::ffi::c_void;

use crate::ffi::s;
use crate::nbt::NbtValue;
use crate::sys;

pub mod builder;

use crate::types::PositionF64;
pub use builder::{CommandBuilder, OverloadBuilder};

/// Mirrors `CommandPermissionLevel`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CommandPermission {
    Any = 0,
    GameDirectors = 1,
    Admin = 2,
    Host = 3,
    Owner = 4,
}

/// Output of [`crate::Server::execute_command`].
#[derive(Debug, Clone)]
pub struct CommandResult {
    pub success: bool,
    pub output: String,
}

/// Command invocation context passed to raw-text command handlers.
pub struct CommandInvocation<'a> {
    pub args: &'a str,
    pub origin: &'a str,
    pub(crate) out_ctx: *mut c_void,
    pub(crate) out_success: sys::LeviRsStrSink,
    pub(crate) out_error: sys::LeviRsStrSink,
}

impl<'a> CommandInvocation<'a> {
    pub fn success(&self, msg: &str) {
        unsafe { (self.out_success)(self.out_ctx, s(msg)) }
    }
    pub fn error(&self, msg: &str) {
        unsafe { (self.out_error)(self.out_ctx, s(msg)) }
    }
}

/// Who ran a parameterized command, decoded from the bridge's origin SNBT.
#[derive(Debug, Clone, Default)]
pub struct CommandOrigin {
    pub name: String,
    /// `CommandOriginType` raw value (0 = player, 7 = dedicated server, …).
    pub origin_type: i32,
    /// Present when the origin has an entity (players, mobs, /execute as).
    pub dimension: Option<i32>,
    pub position: Option<PositionF64>,
}

/// Invocation context for parameterized commands ([`CommandBuilder`]).
pub struct CommandInvocationEx<'a> {
    /// Index of the overload that matched (declaration order).
    pub overload: usize,
    /// Parsed arguments: `{name: value}` — selector params become lists.
    pub args: NbtValue,
    pub origin: CommandOrigin,
    inner: CommandInvocation<'a>,
}

impl<'a> CommandInvocationEx<'a> {
    pub fn success(&self, msg: &str) {
        self.inner.success(msg)
    }
    pub fn error(&self, msg: &str) {
        self.inner.error(msg)
    }

    /// Convenience: the parsed argument by name.
    pub fn arg(&self, name: &str) -> Option<&NbtValue> {
        self.args.get(name)
    }
}

/// Parameter kinds accepted by [`OverloadBuilder`]. String values match the
/// bridge contract in `LeviRsAbi.h` §H.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ParamType {
    Int,
    Bool,
    Float,
    Dimension,
    String,
    /// Needs [`OverloadBuilder::required_enum`] / `optional_enum`.
    Enum,
    /// Needs [`OverloadBuilder::required_enum`] / `optional_enum`.
    SoftEnum,
    Actor,
    Player,
    BlockPos,
    Vec3,
    RawText,
    Message,
    Json,
    Item,
    BlockName,
    Effect,
    ActorType,
    Command,
    RelativeFloat,
    FilePath,
}

impl ParamType {
    fn as_str(self) -> &'static str {
        match self {
            ParamType::Int => "int",
            ParamType::Bool => "bool",
            ParamType::Float => "float",
            ParamType::Dimension => "dimension",
            ParamType::String => "string",
            ParamType::Enum => "enum",
            ParamType::SoftEnum => "soft_enum",
            ParamType::Actor => "actor",
            ParamType::Player => "player",
            ParamType::BlockPos => "block_pos",
            ParamType::Vec3 => "vec3",
            ParamType::RawText => "raw_text",
            ParamType::Message => "message",
            ParamType::Json => "json",
            ParamType::Item => "item",
            ParamType::BlockName => "block_name",
            ParamType::Effect => "effect",
            ParamType::ActorType => "actor_type",
            ParamType::Command => "command",
            ParamType::RelativeFloat => "relative_float",
            ParamType::FilePath => "file_path",
        }
    }
}
