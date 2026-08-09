//! Server, console, and command event ids.
//!
//! The command events (`ExecutingCommandEvent` / `ExecutedCommandEvent`) are
//! dispatched to the bridge through a typed side-channel, but you subscribe to
//! them exactly like any other event — by (suffix of) the id below.
//!
//! Re-exported flat from [`crate::event::names`], so `names::SERVER_STARTED`
//! and `names::server::SERVER_STARTED` are the same string.

/// A player is about to run a command. **Cancellable** — call
/// [`crate::event::EventRef::cancel`] to refuse it.
///
/// # Player origins only
///
/// The console, command blocks, and every other non-player origin are not
/// reported at all. That is deliberate and load-bearing for command gating:
/// refusing the console would lock a server owner out of their own server with
/// no way back in.
///
/// Payload: `{name, command, _player}`. `command` is the raw line as typed,
/// leading slash included.
pub const EXECUTING_COMMAND: &str = "ExecutingCommandEvent";
/// A player's command has finished. **Not cancellable** — it already ran; a
/// `cancel()` here is discarded.
pub const EXECUTED_COMMAND: &str = "ExecutedCommandEvent";
/// Pre-event (cancellable); the post-event is [`CONSOLE_OUTPUTTED`].
pub const CONSOLE_OUTPUTTING: &str = "ConsoleOutputtingEvent";
pub const CONSOLE_OUTPUTTED: &str = "ConsoleOutputtedEvent";
pub const SERVER_STARTED: &str = "ServerStartedEvent";
pub const SERVER_STOPPING: &str = "ServerStoppingEvent";

/// **桥接 hook 事件，不是 LL 总线事件。不可取消。**
///
/// 漏斗的任意一格被写入时触发（`HopperBlockActor::setItem`），也就是物品
/// 进出漏斗的每一次。载荷同时带前后两份堆叠，用差值判断方向：
/// `count > old_count` 是流入，反之是流出。
///
/// 载荷：`{x, y, z, slot, item, count, old_item, old_count}`。
///
/// **没有 `dim` 字段** —— `setItem` 这一层拿不到 `BlockSource`。需要区分维度
/// 的话，按自己注册时记下的坐标去认，别指望事件告诉你。
///
/// 高频事件，回调里别做重活。C++ 侧一直在发，Rust 这边此前没有常量。
pub const HOPPER_TRANSFER: &str = "HopperTransferEvent";
