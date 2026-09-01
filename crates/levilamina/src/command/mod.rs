use std::ffi::c_void;

use crate::ffi::s;
use crate::nbt::NbtValue;
use crate::sys;

pub mod builder;

use crate::types::PositionF64;
pub use builder::{CommandBuilder, OverloadBuilder};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CommandPermission {
    Any = 0,
    GameDirectors = 1,
    Admin = 2,
    Host = 3,
    Owner = 4,
}

#[derive(Debug, Clone)]
pub struct CommandResult {
    pub success: bool,
    pub output: String,
}

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

#[derive(Debug, Clone, Default)]
pub struct CommandOrigin {
    pub name: String,

    pub origin_type: i32,

    pub dimension: Option<i32>,
    pub position: Option<PositionF64>,
}

impl CommandOrigin {
    pub const PLAYER: i32 = 0;
    pub const DEDICATED_SERVER: i32 = 7;

    /// S2：只有专用服务器控制台算控制台。命令块 / 矿车命令块 / 脚本的 `origin_type`
    /// 都不是 7，但 `name` 在没设 CustomName 时是空串 —— 以前三个 mod 把「空名」也
    /// 当成控制台，能放命令块的人就能给自己加管理员组。
    pub fn is_console(&self) -> bool {
        self.origin_type == Self::DEDICATED_SERVER
    }

    pub fn player_name(&self) -> Option<&str> {
        (self.origin_type == Self::PLAYER)
            .then(|| self.name.trim())
            .filter(|n| !n.is_empty())
    }
}

pub struct CommandInvocationEx<'a> {
    pub overload: usize,

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

    pub fn arg(&self, name: &str) -> Option<&NbtValue> {
        self.args.get(name)
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ParamType {
    Int,
    Bool,
    Float,
    Dimension,
    String,

    Enum,

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
