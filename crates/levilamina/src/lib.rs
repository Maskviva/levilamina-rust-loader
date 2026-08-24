mod misc;
#[cfg(feature = "server")]
pub use misc::edit;
pub use misc::{system, types};
mod rt;
pub(crate) use rt::{error, ffi, logger, registration, runtime};
mod comms;
#[cfg(all(feature = "server", feature = "more_dimensions"))]
pub use comms::more_dimensions;
pub use comms::{bus, kvdb, packet, service};
#[cfg(all(feature = "server", feature = "client"))]
compile_error!(
    "The `server` and `client` features are mutually exclusive. \
     Enable exactly one of them."
);
#[cfg(not(any(feature = "server", feature = "client")))]
compile_error!(
    "You must enable exactly one of the `server` or `client` features \
     (default is `server`)."
);

pub use levilamina_sys as sys;

pub mod block;

pub mod container;
pub mod entity;
pub mod event;
pub mod item;

pub mod lane;
pub mod nbt;
pub mod player;

pub mod world;

#[cfg(feature = "server")]
pub mod command;

#[cfg(feature = "server")]
pub mod gui;
#[cfg(feature = "server")]
pub mod money;
#[cfg(feature = "server")]
pub mod scoreboard;
#[cfg(feature = "server")]
pub mod server;
#[cfg(feature = "server")]
pub mod sim;

#[cfg(feature = "client")]
pub mod client;

pub use block::Block;
pub use bus::{Subscription, Vetoable};
pub use container::Container;
pub use entity::{Actor, Entity, EntityId};
pub use error::{Error, Result};
pub use event::{EventPriority, EventRef, Listener, PlayerIdentity};
pub use item::ItemStack;
pub use kvdb::KvDb;
pub use lane::{Lane, LaneContract, LaneData, LaneError, LaneSlice, LaneStr};
pub use logger::{LogLevel, Logger};
pub use nbt::NbtValue;

pub use packet::{ConnectionState, PacketCtx, PacketHook, Packets};
pub use player::{
    Ability, AbilityValue, GameMode, MessageType, Player, PlayerInfo, TitleKind, TitleTimes,
};
pub use registration::{__init_runtime, __lifecycle, __load, LeviMod, ModSlot};
pub use runtime::ModContext;
pub use world::{
    BlockInfo, Bounds, Cell, EntityInfo, PlayerPos, Scan, ScanLayer, StructureInfo, VillageInfo,
};

#[cfg(feature = "server")]
pub use command::{
    CommandBuilder, CommandInvocation, CommandInvocationEx, CommandOrigin, CommandPermission,
    CommandResult, OverloadBuilder, ParamType,
};
#[cfg(feature = "server")]
pub use edit::{BlockUpdate, RayHit, RayHitKind};
#[cfg(feature = "server")]
pub use gui::{CustomFormBuilder, FormResponse, FormValue, ModalFormBuilder, SimpleFormBuilder};
#[cfg(feature = "server")]
pub use money::{MoneyEvent, MoneyEventKind};
#[cfg(feature = "server")]
pub use scoreboard::{DisplaySlot, Objective, Scoreboard};
#[cfg(feature = "server")]
pub use server::{GamingStatus, Server, SoftEnumOp, TaskId, Weather};
#[cfg(feature = "server")]
pub use sim::SimPlayer;

#[cfg(feature = "client")]
pub use client::{Client, ClientInstance, GamingStatus, TaskId};

pub(crate) use runtime::rt;

pub mod prelude {
    pub use crate::{
        register_mod, Ability, Actor, Block, BlockInfo, Cell, Entity, EntityInfo, EventPriority,
        EventRef, GameMode, ItemStack, KvDb, LeviMod, Listener, LogLevel, Logger, MessageType,
        ModContext, NbtValue, Player, PlayerInfo, PlayerPos, Result, Scan, ScanLayer, TitleKind,
        TitleTimes,
    };

    #[cfg(feature = "server")]
    pub use crate::{
        BlockUpdate, CommandBuilder, CommandInvocation, CommandInvocationEx, CommandPermission,
        Container, DisplaySlot, FormResponse, FormValue, GamingStatus, Objective, ParamType,
        Scoreboard, Server, SimPlayer, SoftEnumOp, TaskId, Weather,
    };

    #[cfg(feature = "more_dimensions")]
    pub use crate::more_dimensions::GeneratorType;

    #[cfg(feature = "client")]
    pub use crate::{client::KeyAction, Client, ClientInstance, Container, TaskId};
}
