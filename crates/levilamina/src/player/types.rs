use crate::types::PositionF64;

#[derive(Debug, Clone, Default)]
pub struct PlayerInfo {
    pub name: String,
    pub xuid: String,
    pub uuid: String,
    pub dimension: i32,
    pub pos: PositionF64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Ability {
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

    FlySpeed = 13,
    WalkSpeed = 14,
    VerticalFlySpeed = 19,
}

impl Ability {
    pub fn is_float(self) -> bool {
        matches!(
            self,
            Ability::FlySpeed | Ability::WalkSpeed | Ability::VerticalFlySpeed
        )
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum GameMode {
    Survival = 0,
    Creative = 1,
    Adventure = 2,
    Spectator = 6,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PlayerPermission {
    Visitor = 0,
    Member = 1,
    Operator = 2,

    Custom = 3,
}

impl PlayerPermission {
    pub fn from_i32(v: i32) -> Option<PlayerPermission> {
        Some(match v {
            0 => PlayerPermission::Visitor,
            1 => PlayerPermission::Member,
            2 => PlayerPermission::Operator,
            3 => PlayerPermission::Custom,
            _ => return None,
        })
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MessageType {
    Raw = 0,
    Chat = 1,
    Translate = 2,

    Popup = 3,
    JukeboxPopup = 4,

    Tip = 5,

    SystemMessage = 6,
    Whisper = 7,
    Announcement = 8,
    TextObjectWhisper = 9,
    TextObject = 10,
    TextObjectAnnouncement = 11,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TitleKind {
    Clear = 0,
    Reset = 1,

    Title = 2,

    Subtitle = 3,

    Actionbar = 4,

    Times = 5,
}

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

pub trait AbilityValue: Copy {
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
