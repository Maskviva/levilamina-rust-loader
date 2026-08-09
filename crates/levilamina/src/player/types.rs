//! Player-related value types: [`PlayerInfo`], [`Ability`], [`GameMode`], [`MessageType`].

use crate::types::PositionF64;

/// A summary line from [`Player::list`]: identity + position.
#[derive(Debug, Clone, Default)]
pub struct PlayerInfo {
    pub name: String,
    pub xuid: String,
    pub uuid: String,
    pub dimension: i32,
    pub pos: PositionF64,
}

/// Which ability slots [`Player::set_ability`] speaks. Raw values mirror
/// `AbilitiesIndex` in the engine.
///
/// Boolean slots take a `bool` (`true`/`false`); the three `*Speed` float
/// slots take an `f64`/`f32` (e.g. `set_ability(Ability::FlySpeed, 0.2)`).
///
/// The values below are verified against `AbilitiesIndex.h` for BDS 1.26.20:
/// the enum runs 0..=19 with `AbilityCount = 20`, and exactly three slots are
/// floats — `FlySpeed = 13`, `WalkSpeed = 14`, `VerticalFlySpeed = 19`.
/// Use [`Ability::is_float`] rather than an index range; an earlier version of
/// this crate assumed float slots lived at "index >= 32", which is not a range
/// this enum ever occupied, so every float ability silently took the bool path.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Ability {
    // ── boolean slots ──
    Build = 0,
    Mine = 1,
    DoorsAndSwitches = 2,
    OpenContainers = 3,
    AttackPlayers = 4,
    AttackMobs = 5,
    Operator = 6,
    Teleport = 7,
    Invulnerable = 8,
    Flying = 9,
    MayFly = 10,
    Instabuild = 11,
    Lightning = 12,
    Muted = 15,
    WorldBuilder = 16,
    NoClip = 17,
    PrivilegedBuilder = 18,
    // ── float slots (pass an f64/f32) ──
    FlySpeed = 13,
    WalkSpeed = 14,
    VerticalFlySpeed = 19,
}

impl Ability {
    /// True for the three slots the engine stores as a float rather than a
    /// bool: FlySpeed, WalkSpeed, VerticalFlySpeed.
    ///
    /// Everything else takes 0.0 / non-zero as false / true. Getting this
    /// wrong is silent — the engine has no type check at the FFI boundary —
    /// so prefer this over hardcoding indices.
    pub fn is_float(self) -> bool {
        matches!(
            self,
            Ability::FlySpeed | Ability::WalkSpeed | Ability::VerticalFlySpeed
        )
    }
}

/// Game mode raw values as `/gamemode` understands them.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum GameMode {
    Survival = 0,
    Creative = 1,
    Adventure = 2,
    Spectator = 6,
}

/// Kind of on-screen message for [`Player::tell`]. Raw values mirror the
/// engine's `TextPacketType`. The single-string kinds (`Raw`, `Tip`, `Popup`,
/// `JukeboxPopup`, `SystemMessage`, `Announcement`) are the useful ones for a
/// server tool; the author/param kinds (`Chat`, `Whisper`, `Translate`, and
/// the `TextObject*` trio) still send as a plain line — the same
/// simplification LSE's `tell(msg, type)` makes.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MessageType {
    /// Plain client-side chat line (default; same as [`Player::send_message`]).
    Raw = 0,
    Chat = 1,
    Translate = 2,
    /// Larger text near the centre of the screen.
    Popup = 3,
    JukeboxPopup = 4,
    /// Small text above the hotbar.
    Tip = 5,
    /// A system message line.
    SystemMessage = 6,
    Whisper = 7,
    Announcement = 8,
    TextObjectWhisper = 9,
    TextObject = 10,
    TextObjectAnnouncement = 11,
}

/// Which slot of the title UI a [`Player::send_title`](crate::Player::send_title)
/// call addresses. Raw values mirror `SetTitlePacketPayload::TitleType`.
///
/// [`Clear`](Self::Clear) hides whatever is currently showing but keeps the
/// timings; [`Reset`](Self::Reset) also restores the client's default timings.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TitleKind {
    Clear = 0,
    Reset = 1,
    /// Big text, screen centre.
    Title = 2,
    /// Smaller line under the title. Only shown while a title is on screen —
    /// send the title too, or the subtitle never appears.
    Subtitle = 3,
    /// Text above the hotbar. Independent of the title/subtitle pair.
    Actionbar = 4,
    /// Timing only; no text. Applies to titles sent *after* it.
    Times = 5,
}

/// Fade-in / stay / fade-out, in ticks (20 ticks = 1 second).
///
/// Passing `None` to a send call keeps whatever timing the client last stored,
/// which is a coin flip in practice — a `/title … times` from any command
/// block, plugin, or datapack changes it globally per player. Prefer being
/// explicit; [`TitleTimes::default`] is vanilla's 0.5 s / 3 s / 0.5 s.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TitleTimes {
    pub fade_in: i32,
    pub stay: i32,
    pub fade_out: i32,
}

impl TitleTimes {
    pub const fn new(fade_in: i32, stay: i32, fade_out: i32) -> Self {
        TitleTimes {
            fade_in,
            stay,
            fade_out,
        }
    }
}

impl Default for TitleTimes {
    fn default() -> Self {
        TitleTimes::new(10, 60, 10)
    }
}

/// Anything acceptable as a `set_ability` value.
///
/// Exists because `bool` deliberately does not implement `Into<f64>` in std,
/// so the old `V: Into<f64>` bound could not accept the boolean abilities that
/// make up 17 of the 20 slots. `IS_BOOL` lets `set_ability` warn when a value
/// is passed to the wrong kind of slot — the FFI boundary is a bare `f64`, so
/// nothing downstream could otherwise tell.
pub trait AbilityValue: Copy {
    /// True for `bool`, false for the numeric impls.
    const IS_BOOL: bool;
    fn as_f64(self) -> f64;
}

impl AbilityValue for bool {
    const IS_BOOL: bool = true;
    fn as_f64(self) -> f64 {
        if self {
            1.0
        } else {
            0.0
        }
    }
}

macro_rules! impl_numeric_ability_value {
    ($($t:ty),* $(,)?) => {$(
        impl AbilityValue for $t {
            const IS_BOOL: bool = false;
            fn as_f64(self) -> f64 {
                self as f64
            }
        }
    )*};
}

impl_numeric_ability_value!(f32, f64, i8, i16, i32, i64, u8, u16, u32, u64);
