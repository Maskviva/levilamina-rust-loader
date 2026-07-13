//! Server, console, and command event ids.
//!
//! The command events (`ExecutingCommandEvent` / `ExecutedCommandEvent`) are
//! dispatched to the bridge through a typed side-channel, but you subscribe to
//! them exactly like any other event — by (suffix of) the id below.
//!
//! Re-exported flat from [`crate::event::names`], so `names::SERVER_STARTED`
//! and `names::server::SERVER_STARTED` are the same string.

pub const EXECUTING_COMMAND: &str = "ExecutingCommandEvent";
pub const EXECUTED_COMMAND: &str = "ExecutedCommandEvent";
/// Pre-event (cancellable); the post-event is [`CONSOLE_OUTPUTTED`].
pub const CONSOLE_OUTPUTTING: &str = "ConsoleOutputtingEvent";
pub const CONSOLE_OUTPUTTED: &str = "ConsoleOutputtedEvent";
pub const SERVER_STARTED: &str = "ServerStartedEvent";
pub const SERVER_STOPPING: &str = "ServerStoppingEvent";
