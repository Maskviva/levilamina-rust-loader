//! Player-related value types: [`PlayerInfo`], [`Ability`], [`GameMode`], [`MessageType`].

/// A summary line from [`Player::list`]: identity + position.
#[derive(Debug, Clone, Default)]
pub struct PlayerInfo {
    pub name: String,
    pub xuid: String,
    pub uuid: String,
    pub dimension: i32,
    pub pos: (f64, f64, f64),
}

/// Which ability slots [`Player::set_ability`] speaks. Raw values mirror
/// `AbilitiesIndex` in the engine.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Ability {
    Build = 0,
    Invulnerable = 8,
    Flying = 9,
    MayFly = 10,
    WorldBuilder = 16,
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
