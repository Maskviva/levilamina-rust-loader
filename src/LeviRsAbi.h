/**
 * levilamina-rs C++ ABI — v5
 *
 * This header is the single source of truth for the FFI contract between the
 * C++ loader mod (`levilamina-rust-loader`) and Rust mods (`levilamina-sys`).
 * The Rust side mirrors these declarations field-for-field in
 * `crates/levilamina-sys/src/lib.rs`. Any change here requires:
 *   1. bumping LEVI_RS_ABI_VERSION,
 *   2. appending fields ONLY at the end of structs (never reorder/remove),
 *   3. updating the Rust mirror.
 *
 * C++-only: `LeviRsStr` is `std::string_view`, so this header no longer
 * parses as C (nothing in-tree used it as C, but it could have). Deliberate
 * — see the layout note below.
 *
 * Conventions:
 *   - All strings are UTF-8 (ptr, len) views. NOT guaranteed NUL-terminated.
 *   - Strings passed INTO callbacks are owned by the caller and only valid
 *     for the duration of the call. Copy if you need to keep them.
 *   - Strings passed OUT of Rust use "sink" callbacks invoked within the
 *     call frame, so no cross-boundary ownership ever changes hands.
 *   - Threading: unless documented otherwise, functions must be called on
 *     the SERVER THREAD. `log`, `gaming_status`, `schedule`, `schedule_after`
 *     are thread-safe. All callbacks (events, commands, scheduled tasks) are
 *     invoked on the server thread.
 */
#pragma once

#include <cstdint>
#include <string_view>

extern "C" {
#define LEVI_RS_ABI_VERSION 5u

/**
 * Oldest mod ABI this loader still accepts.
 *
 * Every ABI bump so far has been *additive*: new fields appended to the end
 * of LeviRsApi, existing fields never reordered or removed (see
 * docs/DESIGN.md §8 and the CHANGELOG — v2, v3, … all "additive"). A mod
 * built against ABI vN therefore only calls the first N-version's worth of
 * table slots, and because those slots are byte-identical in every later
 * table, a NEWER loader can safely run an OLDER mod. The loader hands over
 * its full (larger) table; the mod simply never reaches the trailing slots
 * it doesn't know about.
 *
 * So this floor is 1: all historical versions are forward-compatible into
 * the current table. If a future major version ever makes a *non-additive*
 * change (reorders/removes a field, or changes an existing field's
 * signature), bump this to that version — it's the single knob that says
 * "tables below here are NOT a prefix of mine, refuse them."
 *
 * The reverse direction (older loader, newer mod) is guarded on the mod
 * side: __init_runtime compares the loader's `struct_size` against the
 * mod's own compiled size and refuses a loader whose table is too small.
 */
#define LEVI_RS_ABI_MIN_SUPPORTED 1u

/**
 * UTF-8 string view — an alias for std::string_view, not a custom struct.
 * Rust (`levilamina_sys::LeviRsStr`) still declares its own independent
 * #[repr(C)] { ptr, len } — it can't depend on a C++ type, so it mirrors
 * whatever layout string_view actually has here.
 *
 * That {pointer, size_t} layout isn't standard-guaranteed; it's an MSVC STL
 * detail that could even vary by build config (checked iterators). See
 * leviRsVerifyStrLayout() (BridgeApi.cpp, run once from Entry.cpp) for the
 * runtime check, and the static_assert below for the compile-time one.
 */
using LeviRsStr = std::string_view;

static_assert(
    sizeof(LeviRsStr) == sizeof(const char*) + sizeof(size_t),
    "std::string_view is no longer {pointer, size_t} on this toolchain — "
    "the Rust-side repr(C) mirror in levilamina-sys will not match. Do not "
    "proceed without updating both sides and re-verifying the layout."
);

/** Opaque handle to the RustMod instance managed by the loader. */
typedef void* LeviRsModHandle;
/** Opaque handle to an event listener. */
typedef void* LeviRsListenerHandle;

/** Generic "run this" callback. */
typedef void (*LeviRsTaskCb)(void* user);

/** Generic string sink: receives a string within the current call frame. */
typedef void (*LeviRsStrSink)(void* ctx, LeviRsStr s);

/**
 * Event callback.
 *   event_id : the full event id this listener fired for.
 *   snbt     : event data serialized as SNBT (CompoundTag). For cancellable
 *              events it contains a `cancelled` byte field.
 *   write_ctx / write_back : to mutate the event (e.g. cancel it, edit the
 *              chat message), call write_back(write_ctx, new_snbt) with the
 *              modified SNBT before returning. The loader deserializes it
 *              back into the event. Calling it zero times leaves the event
 *              untouched; the last call wins.
 */
typedef void (*LeviRsEventCb)(
    void* user,
    LeviRsStr event_id,
    LeviRsStr snbt,
    void* write_ctx,
    LeviRsStrSink write_back
);

/**
 * Custom command callback.
 *   args        : raw text following the command name (may be empty).
 *   origin_name : display name of the command origin (player name / "Server").
 *   out_success / out_error : call any number of times to emit output lines.
 */
typedef void (*LeviRsCommandCb)(
    void* user,
    LeviRsStr args,
    LeviRsStr origin_name,
    void* out_ctx,
    LeviRsStrSink out_success,
    LeviRsStrSink out_error
);

/** Output sink for execute_command: full command output + success flag. */
typedef void (*LeviRsCmdOutputSink)(void* ctx, bool success, LeviRsStr output);

/* ─────────────────── ABI v3: world reading (scan) ─────────────────── */

/** A player's feet position + dimension. `found` is false if no such player. */
typedef struct LeviRsPlayerPos
{
    double x;
    double y;
    double z;
    int32_t dimension;
    bool found;
} LeviRsPlayerPos;

/**
 * Block sink: invoked once per cell during scan_region.
 *   x, y, z : the cell's world coordinates.
 *   name    : block type name, e.g. "minecraft:redstone_wire".
 *   snbt    : full block serialization (name + states + version) as SNBT.
 */
typedef void (*LeviRsBlockSink)(void* ctx, int32_t x, int32_t y, int32_t z, LeviRsStr name, LeviRsStr snbt);

/**
 * Entity sink: invoked once per entity whose position falls inside the region.
 *   x, y, z : the block cell that contains the entity (floor of its position).
 *   type    : entity type name, e.g. "minecraft:creeper".
 *   snbt    : the entity's serialized NBT (Actor::save) as SNBT.
 */
typedef void (*LeviRsEntitySink)(void* ctx, int32_t x, int32_t y, int32_t z, LeviRsStr type, LeviRsStr snbt);


/* ═════════════════════════ ABI v5: types ═════════════════════════ */

/**
 * Player selector — the identifier half of the "handles are identifiers,
 * not pointers" rule. Resolved against the live player list on every call.
 *   kind: 0 = name (getRealName, falling back to getNameTag),
 *         1 = xuid, 2 = uuid (canonical string form).
 */
typedef struct LeviRsPlayerSel
{
    int32_t kind;
    LeviRsStr value;
} LeviRsPlayerSel;

/** ActorUniqueID raw value. 0 / negative-invalid never resolves. */
typedef int64_t LeviRsActorId;

/**
 * Container reference — "owner + which container".
 *   which: 0=inventory 1=ender_chest 2=armor 3=offhand 4=block container.
 *   player: valid for which 0..3.   dim/x/y/z: valid for which == 4.
 */
typedef struct LeviRsContainerRef
{
    int32_t which{};
    LeviRsPlayerSel player;
    int32_t dim{};
    int32_t x{};
    int32_t y{};
    int32_t z{};
} LeviRsContainerRef;

/** Raw byte sink (binary NBT). Bytes valid only within the call frame. */
typedef void (*LeviRsBytesSink)(void* ctx, uint8_t const* data, size_t len);
/** Key/value sink (kvdb_iter). Views valid only within the call frame. */
typedef void (*LeviRsKvSink)(void* ctx, LeviRsStr key, LeviRsStr value);
/** Actor sink (list_actors). */
typedef void (*LeviRsActorSink)(void* ctx, LeviRsActorId id, LeviRsStr type_name);
/**
 * Form result callback. Invoked ONCE on the server thread when the player
 * responds (or the form is cancelled). result_snbt:
 *   cancelled       : {cancelled:1b, reason:N}
 *   SimpleForm      : {button:N}
 *   CustomForm      : {values:{<name>: string|double|int64 …}}
 *   ModalForm       : {button:"upper"|"lower"}
 * Muted (never called) if the mod is disabled before the player responds.
 */
typedef void (*LeviRsFormResultCb)(void* user, LeviRsStr result_snbt);

/** Opaque handle to an open key-value database owned by the loader. */
typedef void* LeviRsKvDbHandle;

/* ═════════════════ Client-only FFI types ═════════════════
 * Compiled only when building the loader against the CLIENT target
 * (LEVI_RS_TARGET_CLIENT defined by xmake when target_type=client).
 * The Rust levilamina-sys crate mirrors these under #[cfg(feature="client")].
 * A server build never sees them — the struct_size stops before this block. */

#ifdef LEVI_RS_TARGET_CLIENT
/** Opaque handle to a registered key binding owned by the loader's
 * ll::input::KeyRegistry. Drop via client_unregister_key. */
typedef void* LeviRsKeyHandle;

/** Key action: 0 = released (up), 1 = pressed (down).
 * Mirrors ll::event::KeyInputEvent::Action. */
typedef int32_t LeviRsKeyAction;

/** Focus impact level: 0=Neutral 1=ActivateFocus 2=DeactivateFocus.
 * Mirrors ::FocusImpact. */
typedef int32_t LeviRsFocusImpact;

/** Callback for key press/release events. Runs on the client thread.
 *  user   — pointer passed to client_register_key
 *  action — 0=released 1=pressed (see LeviRsKeyAction)
 *  impact — current focus impact (see LeviRsFocusImpact) */
typedef void (*LeviRsKeyCb)(void* user, LeviRsKeyAction action, LeviRsFocusImpact impact);
#endif

/* ── v5 property / action keys.  APPEND-ONLY: never renumber or remove. ──
 * Unknown values make the call return false; the Rust safe layer maps that
 * to Err("unsupported"), which is the forward-compat negotiation.        */

/** player_get_num / player_set_num keys. (G)=get-only, (S)=settable. */
enum LeviRsPlayerNumProp
{
    LEVI_RS_PPROP_GAME_TYPE = 0, /* (G) Player::getPlayerGameType; write via player_set_gamemode */
    LEVI_RS_PPROP_LEVEL = 1, /* (S) attribute Player::LEVEL() */
    LEVI_RS_PPROP_EXPERIENCE = 2, /* (S) attribute Player::EXPERIENCE() (progress 0..1) */
    LEVI_RS_PPROP_HUNGER = 3, /* (S) attribute Player::HUNGER() */
    LEVI_RS_PPROP_SATURATION = 4, /* (S) attribute Player::SATURATION() */
    LEVI_RS_PPROP_EXHAUSTION = 5, /* (S) attribute Player::EXHAUSTION() */
    LEVI_RS_PPROP_XP_NEEDED_NEXT_LEVEL = 6, /* (G) Player::getXpNeededForNextLevel */
    LEVI_RS_PPROP_LUCK = 7, /* (G) Player::getLuck */
    LEVI_RS_PPROP_SELECTED_SLOT = 8, /* (G) Player::getSelectedItemSlot; set via LEVI_RS_PACT_SET_SELECTED_SLOT */
    LEVI_RS_PPROP_IS_OPERATOR = 9, /* (G) Player::isOperator */
    LEVI_RS_PPROP_CAN_USE_OPERATOR_BLOCKS = 10, /* (G) Player::canUseOperatorBlocks */
    LEVI_RS_PPROP_IS_FLYING = 11, /* (G) Player::isFlying */
    LEVI_RS_PPROP_CAN_JUMP = 12, /* (G) Player::canJump */
    LEVI_RS_PPROP_IS_EMOTING = 13, /* (G) Player::isEmoting */
    LEVI_RS_PPROP_IS_IN_RAID = 14, /* (G) Player::isInRaid */
    LEVI_RS_PPROP_IS_HURT = 15, /* (G) Player::isHurt */
    LEVI_RS_PPROP_IS_SCOPING = 16, /* (G) Player::isScoping */
    LEVI_RS_PPROP_CAN_SLEEP = 17, /* (G) Player::canSleep */
    LEVI_RS_PPROP_HAS_RESPAWN_POSITION = 18, /* (G) Player::hasRespawnPosition */
    LEVI_RS_PPROP_CLIENT_SUB_ID = 19, /* (G) Player::getClientSubId */
    LEVI_RS_PPROP_CAN_USE_ABILITY = 20,
    /* (G) Player::canUseAbility; ability index passed via player_action GET path — see LEVI_RS_PACT_CAN_USE_ABILITY */
    /* ── v5 additive: player gap fill ── */
    LEVI_RS_PPROP_DIRECTION = 21,          /* (G) Player::getDirection (0=S,1=W,2=N,3=E) */
    LEVI_RS_PPROP_CHUNK_RADIUS = 22,        /* (G) Player::getChunkRadius */
    LEVI_RS_PPROP_NETWORK_RTT = 23,         /* (G) getNetworkStatus().mPing (ms) */
    LEVI_RS_PPROP_PLATFORM = 24,            /* (G) Player::getPlatform */
    LEVI_RS_PPROP_ENCHANTMENT_SEED = 25,    /* (G) Player::getEnchantmentSeed */
    LEVI_RS_PPROP_IS_USING_ITEM = 26,       /* (G) Player::isUsingItem */
    LEVI_RS_PPROP_IS_BLOCKING = 27,         /* (G) Player::isBlocking */
    LEVI_RS_PPROP_IS_GLIDING = 28,          /* (G) Player::isGliding */
    LEVI_RS_PPROP_IS_SWIMMING = 29,         /* (G) Player::isSwimming */
    LEVI_RS_PPROP_PERMISSION_LEVEL = 30,    /* (G) Player::getPlayerPermissionLevel */
    LEVI_RS_PPROP_SCORE = 31,               /* (G) Player::getScore */
    LEVI_RS_PPROP_FALL_DISTANCE = 32,       /* (G) Actor::getFallDistance */
    LEVI_RS_PPROP_IS_DEAD = 33,             /* (G) Actor::isDead */
    LEVI_RS_PPROP_HAS_DIED_BEFORE = 34,     /* (G) Player::hasDiedBefore */
    LEVI_RS_PPROP_DIMENSION = 35,           /* (G) Actor::getDimensionId */
};

/** player_get_str keys. */
enum LeviRsPlayerStrProp
{
    LEVI_RS_PSTR_REAL_NAME = 0, /* Player::getRealName */
    LEVI_RS_PSTR_UUID = 1, /* Player::getUuid().asString() */
    LEVI_RS_PSTR_XUID = 2, /* Player::getXuid */
    LEVI_RS_PSTR_IP_AND_PORT = 3, /* Player::getIPAndPort */
    LEVI_RS_PSTR_LOCALE_CODE = 4, /* Player::getLocaleCode */
    LEVI_RS_PSTR_NAME_TAG = 5, /* Actor::getNameTag (display name) */
    /* ── v5 additive ── */
    LEVI_RS_PSTR_LAST_DEATH_POS = 6,       /* SNBT {x,y,z} or "" if none */
    LEVI_RS_PSTR_LAST_DEATH_DIMENSION = 7, /* dimension id as string */
    LEVI_RS_PSTR_NETWORK_STATUS = 8,       /* SNBT {ping,avg_ping,packet_loss,max_ping} */
    LEVI_RS_PSTR_PLATFORM_ONLINE_ID = 9,   /* Player::getPlatformOnlineId */
};

/**
 * player_action verbs.  Args are (sarg, a, b, c); unused args are ignored.
 * `out` (when non-NULL) receives a result string where noted.
 */
enum LeviRsPlayerAction
{
    LEVI_RS_PACT_SET_ABILITY = 0, /* a=AbilitiesIndex, b=0/1        Player::setAbility */
    LEVI_RS_PACT_CAN_USE_ABILITY = 1, /* a=AbilitiesIndex → out "0"/"1" Player::canUseAbility */
    LEVI_RS_PACT_SET_SELECTED_SLOT = 2, /* a=slot                          Player::setSelectedSlot */
    LEVI_RS_PACT_GIVE_ITEM = 3, /* sarg=item SNBT                  ItemStack::fromTag + Player::addAndRefresh */
    LEVI_RS_PACT_SET_SPAWN_POINT = 4, /* a,b,c=pos, sarg=dim ("0".."2")  via /spawnpoint */
    LEVI_RS_PACT_CLEAR_TITLE = 5, /* via /title clear */
    LEVI_RS_PACT_SET_TITLE = 6, /* sarg=text, a=slot(0 title,1 subtitle,2 actionbar) via /title */
    /* ── v5 additive ── */
    LEVI_RS_PACT_ADD_EXPERIENCE = 7,         /* a=xp                  Player::addExperience */
    LEVI_RS_PACT_ADD_LEVELS = 8,             /* a=levels              Player::addLevels */
    LEVI_RS_PACT_START_COOLDOWN = 9,         /* sarg=item name, a=ticks Player::startItemCooldown */
    LEVI_RS_PACT_START_RIDING = 10,          /* a=vehicle ActorUniqueID (lower 64b) Player::startRiding */
    LEVI_RS_PACT_STOP_RIDING = 11,           /*                       Player::stopRiding */
    LEVI_RS_PACT_ATTACK = 12,                /* a=target ActorUniqueID (lower 64b) Player::attack */
    LEVI_RS_PACT_DROP = 13,                  /* sarg=item SNBT, a=random(0/1) Player::drop */
    LEVI_RS_PACT_INTERACT = 14,              /* a=target ActorUniqueID        Player::interact */
    LEVI_RS_PACT_START_USING_ITEM = 15,      /* sarg=item SNBT, a=duration    Player::startUsingItem */
    LEVI_RS_PACT_STOP_USING_ITEM = 16,       /*                       Player::stopUsingItem */
    LEVI_RS_PACT_SET_CHUNK_RADIUS = 17,      /* a=radius              Player::setChunkRadius */
    LEVI_RS_PACT_SET_ENCHANTMENT_SEED = 18,  /* a=seed                Player::setEnchantmentSeed */
    LEVI_RS_PACT_REGISTER_TRACKED_BOSS = 19, /* a=boss ActorUniqueID  Player::registerTrackedBoss */
    LEVI_RS_PACT_UNREGISTER_TRACKED_BOSS = 20,/* a=boss ActorUniqueID Player::unRegisterTrackedBoss */
    LEVI_RS_PACT_PLAY_EMOTE = 21,            /* sarg=piece id         Player::playEmote */
    LEVI_RS_PACT_RESEND_ALL_CHUNKS = 22,     /*                       Player::resendAllChunks */
    LEVI_RS_PACT_OPEN_INVENTORY = 23,        /*                       Player::openInventory */
};

/** actor_get_num / actor_set_num keys. (S)=settable via actor_set_num. */
enum LeviRsActorNumProp
{
    LEVI_RS_APROP_POS_X = 0, /* (G) Actor::getPosition().x (feet: getFeetPos for players; POS_* uses getPosition) */
    LEVI_RS_APROP_POS_Y = 1, /* (G) */
    LEVI_RS_APROP_POS_Z = 2, /* (G) */
    LEVI_RS_APROP_ROT_PITCH = 3, /* (G) Actor::getRotation().x */
    LEVI_RS_APROP_ROT_YAW = 4, /* (G) Actor::getRotation().y */
    LEVI_RS_APROP_DIMENSION = 5, /* (G) Actor::getDimensionId */
    LEVI_RS_APROP_HEALTH = 6, /* (G) Actor::getHealth; heal/hurt via actions */
    LEVI_RS_APROP_MAX_HEALTH = 7, /* (G) Actor::getMaxHealth */
    LEVI_RS_APROP_IS_ALIVE = 8, /* (G) Actor::isAlive */
    LEVI_RS_APROP_IS_ON_GROUND = 9, /* (G) Actor::isOnGround */
    LEVI_RS_APROP_IS_IN_WATER = 10, /* (G) Actor::isInWater */
    LEVI_RS_APROP_IS_IN_LAVA = 11, /* (G) Actor::isInLava */
    LEVI_RS_APROP_IS_ON_FIRE = 12, /* (G) Actor::isOnFire */
    LEVI_RS_APROP_IS_INVISIBLE = 13, /* (G) Actor::isInvisible */
    LEVI_RS_APROP_IS_SNEAKING = 14, /* (G) Actor::isSneaking */
    LEVI_RS_APROP_IS_BABY = 15, /* (G) Actor::isBaby */
    LEVI_RS_APROP_IS_RIDING = 16, /* (G) Actor::isRiding */
    LEVI_RS_APROP_IS_TAME = 17, /* (G) Actor::isTame */
    LEVI_RS_APROP_SPEED = 18, /* (G) Actor::getSpeedInMetersPerSecond */
    /* ── v5 additive: actor gap fill ── */
    LEVI_RS_APROP_VIEW_X = 19,              /* (G) Actor::getViewVector().x */
    LEVI_RS_APROP_VIEW_Y = 20,              /* (G) Actor::getViewVector().y */
    LEVI_RS_APROP_VIEW_Z = 21,              /* (G) Actor::getViewVector().z */
    LEVI_RS_APROP_VEL_X = 22,               /* (G) Actor::getVelocity().x */
    LEVI_RS_APROP_VEL_Y = 23,               /* (G) Actor::getVelocity().y */
    LEVI_RS_APROP_VEL_Z = 24,               /* (G) Actor::getVelocity().z */
    LEVI_RS_APROP_HEAD_X = 25,              /* (G) Actor::getHeadPos().x */
    LEVI_RS_APROP_HEAD_Y = 26,              /* (G) Actor::getHeadPos().y */
    LEVI_RS_APROP_HEAD_Z = 27,              /* (G) Actor::getHeadPos().z */
    LEVI_RS_APROP_FEET_X = 28,              /* (G) Actor::getFeetPos().x */
    LEVI_RS_APROP_FEET_Y = 29,              /* (G) Actor::getFeetPos().y */
    LEVI_RS_APROP_FEET_Z = 30,              /* (G) Actor::getFeetPos().z */
    LEVI_RS_APROP_FALL_DISTANCE = 31,       /* (G) Actor::getFallDistance */
    LEVI_RS_APROP_IS_PERSISTENT = 32,       /* (G) Actor::isPersistent */
    LEVI_RS_APROP_IS_LEASHED = 33,          /* (G) Actor::isLeashed */
    LEVI_RS_APROP_IS_INVULNERABLE = 34,     /* (G) Actor::isInvulnerable */
    LEVI_RS_APROP_VARIANT = 35,             /* (G) Actor::getVariant */
    LEVI_RS_APROP_MARK_VARIANT = 36,        /* (G) Actor::getMarkVariant */
    LEVI_RS_APROP_SCALE = 37,               /* (G) Actor::getScaleFactor */
    LEVI_RS_APROP_BRIGHTNESS = 38,          /* (G) Actor::getBrightness */
    LEVI_RS_APROP_RADIUS = 39,              /* (G) Actor::getRadius */
    LEVI_RS_APROP_HAS_TOTEM = 40,           /* (G) Actor::hasTotemEquipped */
    LEVI_RS_APROP_IS_IN_RAIN = 41,          /* (G) Actor::isInRain */
    LEVI_RS_APROP_IS_IN_SNOW = 42,          /* (G) Actor::isInSnow */
    LEVI_RS_APROP_IS_IN_THUNDERSTORM = 43,  /* (G) Actor::isInThunderstorm */
    LEVI_RS_APROP_IS_FROZEN = 44,           /* (G) Actor::isFrozen */
    LEVI_RS_APROP_IS_IN_LOVE = 45,          /* (G) Actor::isInLove */
    LEVI_RS_APROP_DEATH_TIME = 46,          /* (G) Actor::getDeathTime */
    LEVI_RS_APROP_HAS_PASSENGER = 47,       /* (G) Actor::hasPassenger */
};

/** actor_get_str keys. */
enum LeviRsActorStrProp
{
    LEVI_RS_ASTR_TYPE_NAME = 0, /* Actor::getTypeName */
    LEVI_RS_ASTR_NAME_TAG = 1, /* Actor::getNameTag */
    /* ── v5 additive ── */
    LEVI_RS_ASTR_SCORE_TAG = 2,            /* Actor::getScoreTag */
    LEVI_RS_ASTR_FILTERED_NAME = 3,        /* Actor::getFilteredNameTag */
};

/** actor_action verbs. Args (sarg, a, b, c); `out` receives a result where noted. */
enum LeviRsActorAction
{
    LEVI_RS_AACT_KILL = 0, /* Actor::kill */
    LEVI_RS_AACT_DESPAWN = 1, /* Actor::despawn */
    LEVI_RS_AACT_HEAL = 2, /* a=amount                            Actor::heal */
    LEVI_RS_AACT_SET_ON_FIRE = 3, /* a=seconds                           Actor::setOnFire */
    LEVI_RS_AACT_TELEPORT = 4, /* a,b,c=pos, sarg=dim ("0".."2")      Actor::teleport */
    LEVI_RS_AACT_SET_NAME_TAG = 5, /* sarg=name                           Actor::setNameTag */
    LEVI_RS_AACT_ADD_TAG = 6, /* sarg=tag → out "0"/"1"              Actor::addTag */
    LEVI_RS_AACT_REMOVE_TAG = 7, /* sarg=tag → out "0"/"1"              Actor::removeTag */
    LEVI_RS_AACT_HAS_TAG = 8, /* sarg=tag → out "0"/"1"              Actor::hasTag */
    LEVI_RS_AACT_ADD_EFFECT = 9, /* sarg=effect name, a=ticks, b=amplifier, c=visible(0/1)
                                         MobEffect::getByName + Actor::addEffect */
    LEVI_RS_AACT_REMOVE_EFFECT = 10, /* sarg=effect name                    Actor::removeEffect(id) */
    LEVI_RS_AACT_CLEAR_EFFECTS = 11, /* Actor::removeAllEffects */
    LEVI_RS_AACT_HURT = 12, /* a=damage (generic damage source)    Actor::hurt */
    LEVI_RS_AACT_ATTRIBUTE_GET = 13, /* sarg=attribute name ("minecraft:health" …) → out value */
    /* ── v5 additive ── */
    LEVI_RS_AACT_SET_VARIANT = 14,          /* a=variant             Actor::setVariant */
    LEVI_RS_AACT_SET_MARK_VARIANT = 15,     /* a=variant             Actor::setMarkVariant */
    LEVI_RS_AACT_SET_PERSISTENT = 16,       /*                       Actor::setPersistent */
    LEVI_RS_AACT_SET_LEASH_HOLDER = 17,     /* a=holder ActorUniqueID Actor::setLeashHolder */
    LEVI_RS_AACT_SET_INVISIBLE = 18,        /* a=0/1                 Actor::setInvisible */
    LEVI_RS_AACT_SET_SNEAKING = 19,         /* a=0/1                 Actor::setSneaking */
    LEVI_RS_AACT_SET_NAME_TAG_VISIBLE = 20, /* a=0/1                 Actor::setNameTagVisible */
    LEVI_RS_AACT_SET_TARGET = 21,           /* a=target ActorUniqueID Actor::setTarget */
    LEVI_RS_AACT_SET_OWNER = 22,            /* a=owner ActorUniqueID  Actor::setOwner */
    LEVI_RS_AACT_BURN = 23,                 /* a=damage              Actor::burn */
    LEVI_RS_AACT_STOP_FIRE = 24,            /*                       Actor::extinguishFire */
    LEVI_RS_AACT_SET_VELOCITY = 25,         /* a,b,c=vel             Actor::setVelocity */
    LEVI_RS_AACT_APPLY_IMPULSE = 26,        /* a,b,c=impulse         Actor::applyImpulse */
    LEVI_RS_AACT_SET_SCORE_TAG = 27,        /* sarg=text             Actor::setScoreTag */
    LEVI_RS_AACT_SET_SKIN_ID = 28,          /* a=skin id             Actor::setSkinID */
    LEVI_RS_AACT_SET_STRENGTH = 29,         /* a=strength            Actor::setStrength */
    LEVI_RS_AACT_REMOVE_ALL_PASSENGERS = 30,/*                       Actor::removeAllPassengers */
};

/** block_get_num keys. */
enum LeviRsBlockNumProp
{
    LEVI_RS_BPROP_IS_AIR = 0, /* Block::isAir */
    LEVI_RS_BPROP_DATA = 1, /* Block::getData (legacy data value) */
    LEVI_RS_BPROP_BLOCK_ITEM_ID = 2, /* Block::getBlockItemId */
    LEVI_RS_BPROP_IS_CRAFTING_BLOCK = 3, /* Block::isCraftingBlock */
    LEVI_RS_BPROP_IS_INTERACTIVE_BLOCK = 4, /* Block::isInteractiveBlock */
    LEVI_RS_BPROP_HAS_BLOCK_ENTITY = 5, /* BlockSource::getBlockEntity(pos) != null */
    /* ── v5 additive: block gap fill ── */
    LEVI_RS_BPROP_LIGHT = 6,               /* Block::getLight */
    LEVI_RS_BPROP_LIGHT_EMISSION = 7,      /* Block::getLightEmission */
    LEVI_RS_BPROP_DESTROY_SPEED = 8,       /* Block::getDestroySpeed */
    LEVI_RS_BPROP_EXPLOSION_RESISTANCE = 9,/* Block::getExplosionResistance */
    LEVI_RS_BPROP_FRICTION = 10,           /* Block::getFriction */
    LEVI_RS_BPROP_IS_CONTAINER = 11,       /* Block::isContainerBlock */
    LEVI_RS_BPROP_IS_DOOR = 12,            /* Block::isDoorBlock */
    LEVI_RS_BPROP_IS_FENCE = 13,           /* Block::isFenceBlock */
    LEVI_RS_BPROP_IS_RAIL = 14,            /* Block::isRailBlock */
    LEVI_RS_BPROP_IS_SLAB = 15,            /* Block::isSlabBlock */
    LEVI_RS_BPROP_IS_STAIR = 16,           /* Block::isStairBlock */
    LEVI_RS_BPROP_IS_WALL = 17,            /* Block::isWallBlock */
    LEVI_RS_BPROP_IS_CROP = 18,            /* Block::isCropBlock */
    LEVI_RS_BPROP_IS_UNBREAKABLE = 19,     /* Block::isUnbreakable */
    LEVI_RS_BPROP_REDSTONE_SIGNAL = 20,    /* Block::getDirectSignal */
    LEVI_RS_BPROP_COMPARATOR_SIGNAL = 21,  /* Block::getComparatorSignal */
    LEVI_RS_BPROP_IS_SIGNAL_SOURCE = 22,   /* Block::isSignalSource */
    LEVI_RS_BPROP_VARIANT = 23,            /* Block::getVariant */
    LEVI_RS_BPROP_BURN_ODDS = 24,          /* Block::getBurnOdds */
    LEVI_RS_BPROP_FLAME_ODDS = 25,         /* Block::getFlameOdds */
    LEVI_RS_BPROP_BOUNCINESS = 26,         /* Block::getBounciness */
    LEVI_RS_BPROP_IS_SOLID = 27,           /* Block::isSolid */
    LEVI_RS_BPROP_REQUIRES_TOOL = 28,      /* Block::requiresCorrectToolForDrops */
};

/** block_get_str keys. */
enum LeviRsBlockStrProp
{
    LEVI_RS_BSTR_TYPE_NAME = 0, /* Block::getTypeName */
    LEVI_RS_BSTR_SNBT = 1, /* Block::mSerializationId → SNBT {name,states,version} */
    LEVI_RS_BSTR_DESCRIPTION_ID = 2, /* Block::getDescriptionId */
    LEVI_RS_BSTR_DEBUG_STRING = 3, /* Block::toDebugString */
    LEVI_RS_BSTR_TAGS = 4, /* Block::mTags → SNBT string list ["a","b"] */
    /* ── v5 additive ── */
    LEVI_RS_BSTR_STATE = 5,                /* SNBT {state_name:value, …} all block states */
    LEVI_RS_BSTR_COLLISION_SHAPE = 6,      /* SNBT [{min:[x,y,z],max:[x,y,z]}, …] */
    LEVI_RS_BSTR_OUTLINE_SHAPE = 7,        /* SNBT [{min,max}] render outline */
    LEVI_RS_BSTR_DISPLAY_NAME = 8,         /* Block::getDisplayName */
};

/** block_action verbs. */
enum LeviRsBlockAction
{
    LEVI_RS_BACT_HAS_TAG = 0, /* sarg=tag → out "0"/"1"  Block::hasTag */
    /* ── v5 additive ── */
    LEVI_RS_BACT_GET_STATE = 1, /* sarg=state name → out value string  Block::getState */
    LEVI_RS_BACT_POP_RESOURCE = 2, /* sarg=item SNBT → pop resource at pos  Block::popResource */
    LEVI_RS_BACT_AS_ITEM = 3,   /* → out item SNBT   Block::asItemInstance */
};

/** item_get_num keys (query a transient ItemStack rebuilt from SNBT). */
enum LeviRsItemNumProp
{
    LEVI_RS_IPROP_COUNT = 0, /* ItemStackBase::mCount */
    LEVI_RS_IPROP_MAX_STACK_SIZE = 1, /* ItemStackBase::getMaxStackSize */
    LEVI_RS_IPROP_AUX_VALUE = 2, /* ItemStackBase::getAuxValue */
    LEVI_RS_IPROP_ID = 3, /* ItemStackBase::getId */
    LEVI_RS_IPROP_DAMAGE = 4, /* ItemStackBase::getDamageValue */
    LEVI_RS_IPROP_IS_NULL = 5, /* ItemStackBase::isNull */
    LEVI_RS_IPROP_IS_BLOCK = 6, /* ItemStackBase::isBlock */
    LEVI_RS_IPROP_IS_ENCHANTED = 7, /* ItemStackBase::isEnchanted */
    LEVI_RS_IPROP_IS_ARMOR = 8, /* ItemStackBase::isArmorItem */
    LEVI_RS_IPROP_IS_DAMAGEABLE = 9, /* ItemStackBase::isDamageableItem */
    LEVI_RS_IPROP_IS_DAMAGED = 10, /* ItemStackBase::isDamaged */
    /* ── v5 additive: item gap fill ── */
    LEVI_RS_IPROP_MAX_DAMAGE = 11,         /* ItemStackBase::getMaxDamage */
    LEVI_RS_IPROP_IS_UNBREAKABLE = 12,     /* ItemStackBase::isUnbreakable */
    LEVI_RS_IPROP_HAS_DURABILITY = 13,     /* ItemStackBase::hasDurability */
    LEVI_RS_IPROP_IS_POTION = 14,          /* ItemStackBase::isPotionItem */
    LEVI_RS_IPROP_IS_THROWABLE = 15,       /* ItemStackBase::isThrowable */
    LEVI_RS_IPROP_IS_FIRE_RESISTANT = 16,  /* ItemStackBase::isFireResistant */
    LEVI_RS_IPROP_ATTACK_DAMAGE = 17,      /* ItemStackBase::getAttackDamage */
    LEVI_RS_IPROP_REPAIR_COST = 18,        /* ItemStackBase::getBaseRepairCost */
    LEVI_RS_IPROP_ENCHANT_VALUE = 19,      /* ItemStackBase::getEnchantValue */
    LEVI_RS_IPROP_IS_STACKABLE = 20,       /* ItemStackBase::isStackable */
    LEVI_RS_IPROP_IS_MUSIC_DISC = 21,      /* ItemStackBase::isMusicDiscItem */
    LEVI_RS_IPROP_IS_OFFHAND = 22,         /* ItemStackBase::isOffhandItem */
    LEVI_RS_IPROP_USE_DURATION = 23,       /* ItemStackBase::getMaxUseDuration */
    LEVI_RS_IPROP_IS_GLINT = 24,           /* ItemStackBase::isGlint */
    LEVI_RS_IPROP_IS_BUNDLE = 25,          /* ItemStackBase::isBundle */
    LEVI_RS_IPROP_HAS_USER_DATA = 26,      /* ItemStackBase::hasUserData */
    LEVI_RS_IPROP_HAS_CUSTOM_NAME = 27,    /* ItemStackBase::hasCustomHoverName */
};

/** item_get_str keys. */
enum LeviRsItemStrProp
{
    LEVI_RS_ISTR_TYPE_NAME = 0, /* ItemStackBase::getTypeName ("minecraft:apple") */
    LEVI_RS_ISTR_NAME = 1, /* ItemStackBase::getName (display) */
    LEVI_RS_ISTR_CUSTOM_NAME = 2, /* ItemStackBase::getCustomName */
    LEVI_RS_ISTR_RAW_NAME_ID = 3, /* ItemStackBase::getRawNameId */
    /* ── v5 additive ── */
    LEVI_RS_ISTR_LORE = 4,                /* SNBT list ["l1","l2"]  ItemStackBase::getCustomLore */
    LEVI_RS_ISTR_CAN_DESTROY = 5,         /* SNBT list ["minecraft:stone", …] */
    LEVI_RS_ISTR_CAN_PLACE_ON = 6,        /* SNBT list */
    LEVI_RS_ISTR_USER_DATA = 7,           /* full NBT user data as SNBT */
    LEVI_RS_ISTR_HOVER_NAME = 8,          /* ItemStackBase::getHoverName */
    LEVI_RS_ISTR_EFFECT_NAME = 9,         /* ItemStackBase::getEffectName */
    LEVI_RS_ISTR_COLOR = 10,              /* SNBT {r,g,b}  ItemStackBase::getColor */
};

/** item_transform ops: rebuild → mutate → serialize back (out = new SNBT). */
enum LeviRsItemOp
{
    LEVI_RS_IOP_SET_CUSTOM_NAME = 0, /* sarg=name             ItemStackBase::setCustomName */
    LEVI_RS_IOP_SET_DAMAGE = 1, /* narg=damage           ItemStackBase::setDamageValue */
    LEVI_RS_IOP_SET_COUNT = 2, /* narg=count            ItemStackBase::mCount */
    LEVI_RS_IOP_SET_LORE = 3, /* sarg=SNBT list ["l1","l2"]  ItemStackBase::setCustomLore */
    /* ── v5 additive ── */
    LEVI_RS_IOP_SET_UNBREAKABLE = 4,    /* narg=0/1               ItemStackBase::setUnbreakable */
    LEVI_RS_IOP_HURT_AND_BREAK = 5,     /* narg=damage            ItemStackBase::hurtAndBreak */
    LEVI_RS_IOP_SET_REPAIR_COST = 6,    /* narg=cost              ItemStackBase::setRepairCost */
    LEVI_RS_IOP_ADD_ENCHANT = 7,        /* sarg="name:level"      saveEnchantsToUserData */
    LEVI_RS_IOP_REMOVE_ENCHANTS = 8,    /*                        ItemStackBase::removeEnchants */
    LEVI_RS_IOP_CLEAR_LORE = 9,         /*                        ItemStackBase::clearCustomLore */
    LEVI_RS_IOP_RESET_NAME = 10,        /*                        ItemStackBase::resetHoverName */
    LEVI_RS_IOP_SET_CAN_DESTROY = 11,   /* sarg=SNBT list         ItemStackBase::setCanDestroy */
    LEVI_RS_IOP_SET_CAN_PLACE_ON = 12,  /* sarg=SNBT list         ItemStackBase::setCanPlaceOn */
};

/** scoreboard_op verbs (args a=objective/slot, b=target, n=value). */
enum LeviRsScoreboardOp
{
    LEVI_RS_SB_ADD_OBJECTIVE = 0, /* a=name, b=display name → out "1"      Scoreboard::addObjective("dummy") */
    LEVI_RS_SB_REMOVE_OBJECTIVE = 1, /* a=name                                Scoreboard::removeObjective */
    LEVI_RS_SB_LIST_OBJECTIVES = 2, /* → out SNBT [{name,display}, …]        Scoreboard::getObjectives */
    LEVI_RS_SB_GET_SCORE = 3, /* a=objective, b=fake-player name → out value  Objective::getPlayerScore */
    LEVI_RS_SB_SET_SCORE = 4, /* a=objective, b=name, n=value          Scoreboard::modifyPlayerScore(Set) */
    LEVI_RS_SB_ADD_SCORE = 5, /* a=objective, b=name, n=value          … (Add) */
    LEVI_RS_SB_REDUCE_SCORE = 6, /* a=objective, b=name, n=value          … (Subtract) */
    LEVI_RS_SB_RESET_SCORE = 7, /* a=objective, b=name                   Scoreboard::resetPlayerScore */
    LEVI_RS_SB_SET_DISPLAY = 8, /* a=slot("sidebar"/"list"/"belowname"), b=objective  setDisplayObjective */
    LEVI_RS_SB_CLEAR_DISPLAY = 9, /* a=slot                                clearDisplayObjective */
};

/** sys_info_str keys. */
/** Per-dimension behaviour rules for md_set_dimension_rule.
 *
 *  These are deliberately NOT a mirror of any engine enum: they name things
 *  the loader intercepts itself. Values are ABI — append only, never renumber.
 */
enum LeviRsDimRule
{
    LEVI_RS_DIMRULE_SPAWN_MONSTER = 0,  /* natural hostile spawns */
    LEVI_RS_DIMRULE_SPAWN_ANIMAL  = 1,  /* natural passive spawns */
    LEVI_RS_DIMRULE_SPAWN_SPAWNER = 2,  /* spawns from mob spawners */
    LEVI_RS_DIMRULE_EXPLODE_BLOCKS = 3, /* explosions damaging terrain */
    LEVI_RS_DIMRULE_FIRE_SPREAD   = 4,  /* fire spreading to neighbours */
    LEVI_RS_DIMRULE_MOB_GRIEFING  = 5,  /* mobs changing blocks */
    LEVI_RS_DIMRULE_PROJECTILE    = 6,  /* projectile spawns */
    /* ── 第二批（挂载点参考 LegacyScriptEngine 的同名事件） ── */
    LEVI_RS_DIMRULE_PISTON_PUSH   = 7,  /* pistons moving blocks */
    LEVI_RS_DIMRULE_LIQUID_FLOW   = 8,  /* water/lava spreading */
    LEVI_RS_DIMRULE_FARMLAND_DECAY = 9, /* farmland trampled back to dirt */
    LEVI_RS_DIMRULE_RIDE          = 10, /* mounting boats/minecarts/animals */
};

enum LeviRsSysInfoProp
{
    LEVI_RS_SYS_OS_NAME = 0, /* sys_utils::getSystemName */
    LEVI_RS_SYS_OS_VERSION = 1, /* sys_utils::getSystemVersion → string */
    LEVI_RS_SYS_LOCALE = 2, /* sys_utils::getSystemLocaleCode */
    LEVI_RS_SYS_LOCAL_TIME = 3, /* sys_utils::getLocalTime → SNBT {year,month,day,hour,minute,second,ms} */
};

/** server_info_str keys. */
enum LeviRsServerInfoProp
{
    LEVI_RS_SRV_BDS_VERSION = 0, /* Common::getGameVersionString */
    LEVI_RS_SRV_PROTOCOL_VERSION = 1, /* SharedConstants::NetworkProtocolVersion → string */
};

/**
 * Function table handed to the Rust mod at load time.
 * Pointer remains valid for the whole lifetime of the mod.
 */
typedef struct LeviRsApi
{
    /** == LEVI_RS_ABI_VERSION of the loader. */
    uint32_t abi_version;
    /** sizeof(LeviRsApi) as compiled into the loader; enables forward-compat checks. */
    uint32_t struct_size;

    enum class LLMoneyEvent { Set, Add, Reduce, Trans };

    typedef bool (*LLMoneyCallback)(LLMoneyEvent type, LeviRsStr from, LeviRsStr to, long long value);

    /**
     * Log a message through the mod's own LeviLamina logger.
     * level: -1=Off, 0=Fatal, 1=Error, 2=Warn, 3=Info, 4=Debug, 5=Trace
     * (mirrors ll::io::LogLevel). Thread-safe.
     */
    void (*log)(LeviRsModHandle mod, int32_t level, LeviRsStr msg);

    /**
     * Current gaming status: 0=Default, 1=Starting, 2=Running, 3=Stopping
     * (mirrors ll::GamingStatus). Thread-safe.
     */
    int32_t (*gaming_status)();

    /** Queue a task onto the server thread ASAP. Thread-safe. */
    void (*schedule)(LeviRsTaskCb cb, void* user);

    /** Queue a task onto the server thread after `delay_ms`. Thread-safe. */
    void (*schedule_after)(LeviRsTaskCb cb, void* user, uint64_t delay_ms);

    /**
     * Subscribe to a LeviLamina event by id (server thread only).
     *   event_id : full id, e.g. "ll::event::PlayerChatEvent". If no exact
     *              match exists, the loader falls back to a unique suffix
     *              match ("PlayerChatEvent" works if unambiguous).
     *   priority : 0..4 (Highest..Lowest), 2 = Normal
     *              (mirrors ll::event::EventPriority).
     * Returns NULL on failure (unknown/ambiguous id).
     */
    LeviRsListenerHandle (*subscribe_event)(
        LeviRsModHandle mod,
        LeviRsStr event_id,
        int32_t priority,
        LeviRsEventCb cb,
        void* user
    );

    /** Remove a listener previously returned by subscribe_event. Server thread only. */
    bool (*unsubscribe_event)(LeviRsModHandle mod, LeviRsListenerHandle listener);

    /** Enumerate all currently registered event ids. Server thread only. */
    void (*list_events)(void* ctx, LeviRsStrSink sink);

    /**
     * Execute a command as the server console (permission: Owner) and collect
     * its output. Server thread only. Returns false if the level is not ready.
     */
    bool (*execute_command)(LeviRsStr cmd, void* ctx, LeviRsCmdOutputSink sink);

    /**
     * Register a custom command `/name [args: raw text]`.
     *   permission: 0=Any,1=GameDirectors,2=Admin,3=Host,4=Owner
     *               (mirrors CommandPermissionLevel).
     * Call during on_enable, on the server thread. The command stays
     * registered for the lifetime of the server (Bedrock cannot unregister
     * commands); callbacks for disabled mods are muted by the loader.
     */
    bool (*register_command)(
        LeviRsModHandle mod,
        LeviRsStr name,
        LeviRsStr description,
        int32_t permission,
        LeviRsCommandCb cb,
        void* user
    );

    /**
     * Current server tick (the tickID from Level::getCurrentTick()).
     * Returns 0 when the level is not ready. Server thread only.
     */
    uint64_t (*get_current_tick)();

    /**
     * Seconds taken by the last tick (mTickDeltaTime; 0.05 at 20 TPS).
     * TPS = 1.0 / tick_delta_time when > 0. Returns -1.0 if unavailable.
     * Server thread only.
     */
    double (*get_tick_delta_time)();

    /**
     * Number of currently connected players
     * (Level::getActivePlayerCount()). Server thread only.
     */
    int32_t (*get_player_count)();

    /**
     * Whether the simulation is currently paused
     * (Level::getSimPaused()). Server thread only.
     */
    bool (*get_sim_paused)();

    /* ── ABI v3 ── */

    /**
     * Spawn a particle effect at a world coordinate. Used to outline a
     * selection box edge-by-edge. Server thread only. Returns false if the
     * level/dimension is not ready.
     *   dimension   : 0 = overworld, 1 = nether, 2 = the end.
     *   effect_name : e.g. "minecraft:basic_flame_particle" / "minecraft:redstone_wire_dust_particle".
     */
    bool (*spawn_particle)(int32_t dimension, LeviRsStr effect_name, double x, double y, double z);

    /**
     * Look up a connected player's feet position and dimension by name.
     * Used to pick selection corners from where the player is standing.
     * Server thread only.
     */
    LeviRsPlayerPos (*get_player_position)(LeviRsStr name);

    /**
     * Scan a cuboid region, corners inclusive (order-independent). For every
     * cell in the box, blocks_sink is called with the block name + full SNBT.
     * For every entity whose position lies within the box, entities_sink is
     * called with the containing cell and the entity's SNBT. Both sinks run
     * synchronously within this call; nothing is retained afterwards.
     * Server thread only. Returns false if the level/dimension is not ready.
     */
    bool (*scan_region)(
        int32_t dimension,
        int32_t x1,
        int32_t y1,
        int32_t z1,
        int32_t x2,
        int32_t y2,
        int32_t z2,
        void* ctx,
        LeviRsBlockSink blocks_sink,
        LeviRsEntitySink entities_sink
    );


    /* ═════════════════ ABI v5 (v1.0.0) — append-only ═════════════════
     * Everything below: SERVER THREAD ONLY unless noted. All calls return
     * false / do nothing while the level is not ready. Unknown enum keys
     * return false (forward-compat negotiation).                        */

    /* ── §A world read/write & clock ── */

    /** Read one block: sink called once with (x,y,z, type name, full SNBT). */
    bool (*get_block)(int32_t dim, int32_t x, int32_t y, int32_t z, void* ctx, LeviRsBlockSink sink);
    /** Place a block via /setblock (version-stable path). block_spec = id or id [states]. */
    bool (*set_block)(int32_t dim, int32_t x, int32_t y, int32_t z, LeviRsStr block_spec);
    /** World time (Level::getTime). */
    bool (*get_time)(int64_t* out);
    /** Set world time via /time set. */
    bool (*set_time)(int64_t t);
    /** 0=clear 1=rain 2=thunder, via /weather. */
    bool (*set_weather)(int32_t weather);

    /* ── §B player management ── */

    /** One SNBT per online player: {name,xuid,uuid,dim,x,y,z}. */
    void (*list_players)(void* ctx, LeviRsStrSink snbt_sink);
    /** Resolve a player selector to their ActorUniqueID (bridges into the actor_* API). */
    bool (*player_resolve)(LeviRsPlayerSel sel, LeviRsActorId* out);
    bool (*player_send_message)(LeviRsPlayerSel sel, LeviRsStr msg);
    bool (*player_disconnect)(LeviRsPlayerSel sel, LeviRsStr reason);
    /** sendMessage to every online player. */
    void (*broadcast_message)(LeviRsStr msg);
    /** 0=survival 1=creative 2=adventure 6=spectator, via /gamemode. */
    bool (*player_set_gamemode)(LeviRsPlayerSel sel, int32_t mode);
    /** Teleport via /execute in <dim> run tp. */
    bool (*player_teleport)(LeviRsPlayerSel sel, int32_t dim, double x, double y, double z);
    bool (*player_get_num)(LeviRsPlayerSel sel, int32_t prop, double* out);
    bool (*player_get_str)(LeviRsPlayerSel sel, int32_t prop, void* ctx, LeviRsStrSink sink);
    bool (*player_set_num)(LeviRsPlayerSel sel, int32_t prop, double v);
    bool (*player_action)(
        LeviRsPlayerSel sel,
        int32_t action,
        LeviRsStr sarg,
        double a,
        double b,
        double c,
        void* ctx,
        LeviRsStrSink out
    );

    /* ── §C actors (players resolve here too, via player_resolve) ── */

    /** Enumerate live actors; dim = -1 for all dimensions. */
    void (*list_actors)(int32_t dim, void* ctx, LeviRsActorSink sink);
    /** Full Actor::save NBT as SNBT. */
    bool (*actor_snapshot)(LeviRsActorId id, void* ctx, LeviRsStrSink snbt_sink);
    bool (*actor_get_num)(LeviRsActorId id, int32_t prop, double* out);
    bool (*actor_get_str)(LeviRsActorId id, int32_t prop, void* ctx, LeviRsStrSink sink);
    bool (*actor_action)(
        LeviRsActorId id,
        int32_t action,
        LeviRsStr sarg,
        double a,
        double b,
        double c,
        void* ctx,
        LeviRsStrSink out
    );
    /** Spawn a mob (Spawner::spawnMob); on success *out = its ActorUniqueID. */
    bool (*spawn_mob)(int32_t dim, LeviRsStr type_name, double x, double y, double z, LeviRsActorId* out);
    /** Level::explode. source may be 0 (no source actor). */
    bool (*explode)(
        int32_t dim,
        double x,
        double y,
        double z,
        float radius,
        float max_resistance,
        LeviRsActorId source,
        bool fire,
        bool breaks_blocks,
        bool allow_underwater
    );

    /* ── §D blocks & block entities ── */

    bool (*block_get_num)(int32_t dim, int32_t x, int32_t y, int32_t z, int32_t prop, double* out);
    bool (*block_get_str)(int32_t dim, int32_t x, int32_t y, int32_t z, int32_t prop, void* ctx, LeviRsStrSink sink);
    bool (*block_action)(
        int32_t dim,
        int32_t x,
        int32_t y,
        int32_t z,
        int32_t action,
        LeviRsStr sarg,
        void* ctx,
        LeviRsStrSink out
    );
    /** BlockActor::save (with default SaveContext) as SNBT; false if none there. */
    bool (*block_entity_snbt)(int32_t dim, int32_t x, int32_t y, int32_t z, void* ctx, LeviRsStrSink sink);

    /* ── §E items (SNBT value objects) & containers ── */

    bool (*item_get_num)(LeviRsStr item_snbt, int32_t prop, double* out);
    bool (*item_get_str)(LeviRsStr item_snbt, int32_t prop, void* ctx, LeviRsStrSink sink);
    /** Rebuild → mutate → serialize; out receives the NEW item SNBT. */
    bool (*item_transform)(LeviRsStr item_snbt, int32_t op, LeviRsStr sarg, double narg, void* ctx, LeviRsStrSink out);
    bool (*container_size)(LeviRsContainerRef ref, int32_t* out);
    /** Slot content as item SNBT (empty slots yield the air item's SNBT). */
    bool (*container_get_item)(LeviRsContainerRef ref, int32_t slot, void* ctx, LeviRsStrSink sink);
    bool (*container_set_item)(LeviRsContainerRef ref, int32_t slot, LeviRsStr item_snbt);
    bool (*container_add_item)(LeviRsContainerRef ref, LeviRsStr item_snbt);
    bool (*container_remove_item)(LeviRsContainerRef ref, int32_t slot, int32_t count);
    bool (*container_clear)(LeviRsContainerRef ref);

    /* ── §F scoreboard ── */

    bool (*scoreboard_op)(int32_t op, LeviRsStr a, LeviRsStr b, int64_t n, void* ctx, LeviRsStrSink out);

    /* ── §G forms (async result callback) ── */

    /**
     * kind: 0=SimpleForm 1=CustomForm 2=ModalForm. form_snbt describes the
     * form (see docs/api/gui). The callback fires once, on the server thread,
     * and is muted if the mod is disabled before the player responds.
     */
    bool (*form_send)(
        LeviRsModHandle mod,
        LeviRsPlayerSel sel,
        int32_t kind,
        LeviRsStr form_snbt,
        LeviRsFormResultCb cb,
        void* user
    );

    /* ── §H parameterized commands & enums ── */

    /**
     * Like register_command, but with typed overloads. overloads_snbt:
     *   {overloads:[[{name:"target",kind:"player",optional:0b}, …], …]}
     * kinds: int|bool|float|string|enum|soft_enum|actor|player|block_pos|vec3|
     *        raw_text|message|json|item|block_name|effect|actor_type|command|
     *        relative_float|file_path (enum/soft_enum also need "enum":"Name").
     * The callback's `args` receives the parse result as SNBT
     *   {overload:N, args:{<name>: …}}   and `origin_name` becomes origin SNBT
     *   {name,type,dim,x,y,z}.
     */
    bool (*register_command_ex)(
        LeviRsModHandle mod,
        LeviRsStr name,
        LeviRsStr description,
        int32_t permission,
        LeviRsStr overloads_snbt,
        LeviRsCommandCb cb,
        void* user
    );
    /** values_snbt = {values:[["name",1L], …]}  → tryRegisterRuntimeEnum. */
    bool (*register_command_enum)(LeviRsStr name, LeviRsStr values_snbt);
    /** values_snbt = {values:["a","b"]}         → tryRegisterSoftEnum. */
    bool (*register_command_soft_enum)(LeviRsStr name, LeviRsStr values_snbt);
    /** op: 0=set 1=add 2=remove. */
    bool (*update_command_soft_enum)(LeviRsStr name, int32_t op, LeviRsStr values_snbt);

    /* ── §I NBT binary, KvDb (thread-safe), system & server info ── */

    /** fmt: 0=disk little-endian, 1=network. */
    bool (*nbt_snbt_to_binary)(LeviRsStr snbt, int32_t fmt, void* ctx, LeviRsBytesSink sink);
    bool (*nbt_binary_to_snbt)(uint8_t const* data, size_t len, int32_t fmt, void* ctx, LeviRsStrSink sink);

    /* KvDb: THREAD-SAFE (internal mutex). Paths are confined to the mod's
     * own data directory; ".." and absolute paths are rejected. Handles are
     * owned by the loader and force-closed (with a warning) at mod unload. */
    LeviRsKvDbHandle (*kvdb_open)(LeviRsModHandle mod, LeviRsStr path, bool create_if_missing);
    void (*kvdb_close)(LeviRsKvDbHandle h);
    bool (*kvdb_get)(LeviRsKvDbHandle h, LeviRsStr key, void* ctx, LeviRsStrSink sink);
    bool (*kvdb_set)(LeviRsKvDbHandle h, LeviRsStr key, LeviRsStr value);
    bool (*kvdb_del)(LeviRsKvDbHandle h, LeviRsStr key);
    bool (*kvdb_has)(LeviRsKvDbHandle h, LeviRsStr key);
    bool (*kvdb_is_empty)(LeviRsKvDbHandle h);
    void (*kvdb_iter)(LeviRsKvDbHandle h, void* ctx, LeviRsKvSink sink);

    /* System info: THREAD-SAFE (plain OS calls). */
    bool (*sys_info_str)(int32_t prop, void* ctx, LeviRsStrSink sink);
    bool (*sys_get_env)(LeviRsStr name, void* ctx, LeviRsStrSink sink);
    bool (*sys_set_env)(LeviRsStr name, LeviRsStr value);
    bool (*sys_is_wine)();

    /* Server / world-level settings. */
    bool (*get_difficulty)(int32_t* out); /* Level::getDifficulty */
    bool (*set_difficulty)(int32_t d); /* /difficulty */
    bool (*get_seed)(int64_t* out); /* Level::getLevelSeed64 */
    /** out sink receives SNBT {type:"bool"|"int"|"float", value:…}; false if unknown rule. */
    bool (*game_rule_get)(LeviRsStr name, void* ctx, LeviRsStrSink sink);
    bool (*game_rule_set)(LeviRsStr name, LeviRsStr value); /* /gamerule */
    bool (*server_info_str)(int32_t prop, void* ctx, LeviRsStrSink sink);

    /*
     * Per-player particle packet (additive, gated by struct_size).
     * Sends a SpawnParticleEffectPacket ONLY to the resolved player
     * (Player::sendNetworkPacket) instead of Level::spawnParticleEffect's
     * dimension-wide broadcast — other clients never receive it.
     * `dimension` is the vanilla dimension id carried in the packet; pass the
     * dimension the coordinates refer to (normally the player's own — clients
     * don't render particles for another dimension).
     * False if the player is offline / can't be resolved.
     */
    bool (*spawn_particle_for)(
        LeviRsPlayerSel sel, int32_t dimension, LeviRsStr effect_name, double x, double y, double z);

    /*
     * Raw per-connection packet send (additive, gated by struct_size) — the
     * generic primitive spawn_particle_for derives from.
     * `packet_id` is a MinecraftPacketIds value; `body`/`body_len` is the
     * packet's wire-format body for the CURRENT game version. The bridge
     * deserialises it into a real packet object (MinecraftPackets::createPacket
     * + Packet::read) and delivers it to the resolved player's connection only.
     * False if: player offline, unknown/unconstructible id, body fails to
     * parse, or bytes are left over after parsing (wrong shape for this
     * version). ESCAPE HATCH: the wire format is version-specific and is the
     * caller's responsibility; prefer typed entries when one exists.
     */
    bool (*send_packet)(LeviRsPlayerSel sel, int32_t packet_id, uint8_t const* body, size_t body_len);

    /*
     * Tick control (additive, gated by struct_size). Backed by a bridge-owned
     * detour on Level::tick, installed lazily on the first control call and
     * left in place (idle cost: one predictable branch per frame — a control
     * call can arrive from a command handler that is executing INSIDE the
     * tick, where unpatching would not be safe). Server thread only.
     * While frozen, mobs/blocks/redstone/time stop; players can still move
     * and chat (movement is client-authoritative, network runs outside the
     * level tick).
     */
    bool (*tick_freeze)(bool on);
    /** Only while frozen: queue exactly n extra frames. False if not frozen or n == 0. */
    bool (*tick_step)(uint32_t n);
    /** 0 < factor <= 100. Fractional = slow motion (accumulator), 1.0 restores normal. */
    bool (*tick_warp)(double factor);

    /*
     * Per-subsystem MSPT profiler (additive, gated by struct_size). Backed by
     * five timing detours (Level/Dimension tick, redstone, chunk block ticks,
     * block entities), installed lazily on the first profile_begin and left
     * in place. One sampling window at a time. Server thread only.
     */
    /** Arm a window of `ticks` level ticks (1..12000). False if 0, too big, or already sampling. */
    bool (*profile_begin)(uint32_t ticks);
    /**
     * Poll for the finished report. False while sampling / nothing armed;
     * true exactly once per window, sinking one SNBT report:
     * {ticks:N, buckets:{level_tick:{us,calls}, dimension_tick:{…}, redstone:{…},
     *  chunk_blocks:{…}, block_entities:{…}}}. Bucket times are INCLUSIVE
     * (nested subsystems), report side by side, don't sum.
     */
    bool (*profile_take)(void* ctx, LeviRsStrSink sink);

    /*
     * Simulated ("fake") players (additive, gated by struct_size).
     * sim_spawn creates a real ServerPlayer with that name — every existing
     * per-player entry (teleport, health, inventory, kick, …) works on it via
     * the usual name selector. sim_do multiplexes the simulate* verb family:
     * the action vocabulary grows bridge-side without new table slots
     * (verbs: despawn stop jump attack interact use_item drop respawn
     * move_to navigate_to look_at destroy_block destroy_look stop_destroy
     * interact_block sneak fly chat — args as SNBT, see docs). Gated on
     * isSimulatedPlayer(): a real player can never be puppeted. False on
     * unknown verb, malformed args, offline/non-sim target.
     */
    bool (*sim_spawn)(LeviRsStr name, int32_t dimension, double x, double y, double z);
    bool (*sim_do)(LeviRsPlayerSel sel, LeviRsStr action, LeviRsStr args_snbt);
    /** True if the selector resolves to a live simulated player. Lets a mod
     *  re-validate a bot after a restart (the SimulatedPlayer persists in the
     *  world, but in-memory handles don't). */
    bool (*sim_is)(LeviRsPlayerSel sel);
    /** Enumerate the names of all live simulated players (sink receives each
     *  name). Rebuild a handle from a name to drive a bot that outlived the
     *  session that spawned it. */
    void (*sim_list)(void* ctx, LeviRsStrSink name_sink);

    /*
     * Read-only world-data queries (additive, gated by struct_size). Both
     * stream one SNBT object per result through the sink; observational only.
     * Server thread only.
     */
    /** Enumerate villages in a dimension. Each: {uuid, center:[x,y,z],
     *  bounds:{min,max}, poi_count}. */
    void (*villages)(int32_t dimension, void* ctx, LeviRsStrSink snbt_sink);
    /** Hardcoded spawn areas (nether fortress / witch hut / ocean monument /
     *  pillager outpost) whose chunks intersect a radius around (x,y,z). Each:
     *  {type, bounds:{min,max}}. Only LOADED chunks are inspected — a
     *  read-only query never force-loads. */
    void (*structures_near)(
        int32_t dimension, int32_t x, int32_t y, int32_t z, int32_t radius, void* ctx,
        LeviRsStrSink snbt_sink);

    /*
     * Send a message of a specific TextPacketType to one player (additive,
     * gated by struct_size). `type` is a TextPacketType value:
     *   0 Raw · 1 Chat · 2 Translate · 3 Popup · 4 JukeboxPopup · 5 Tip ·
     *   6 SystemMessage · 7 Whisper · 8 Announcement · 9 TextObjectWhisper ·
     *   10 TextObject · 11 TextObjectAnnouncement.
     * Out-of-range falls back to Raw. Single-string body (like LSE tell): the
     * author/param kinds (Chat/Whisper/Translate) arrive as plain text.
     * plain `player_send_message` remains the Raw/Chat convenience path.
     */
    bool (*player_send_message_typed)(LeviRsPlayerSel sel, LeviRsStr msg, int32_t type);

    /* —— Money (ABI v5 Additive) —— */
    long long (*get_money)(LeviRsStr xuid);
    bool (*set_money)(LeviRsStr xuid, long long money);
    bool (*add_money)(LeviRsStr xuid, long long money);
    bool (*reduce_money)(LeviRsStr xuid, long long money);
    bool (*trans_money)(LeviRsStr from, LeviRsStr to, long long val, LeviRsStr note);
    void (*money_get_hist)(LeviRsStr xuid, int timediff, void* ctx, LeviRsStrSink sink);
    void (*money_clear_hist)(int difftime);
    void (*money_listen_before_event)(LLMoneyCallback callback);
    void (*money_listen_after_event)(LLMoneyCallback callback);
    void (*money_ranking)(unsigned short num, void* ctx, LeviRsStrSink sink);

    /* ═════════════════ ABI v5 Additive — API gap fill (struct_size-gated) ═════════════════
     * All entries below are additive: older loaders (smaller struct_size)
     * simply won't have these fields. The Rust __init_runtime check rejects
     * mods built against a larger table than the loader provides. Unknown enum
     * keys return false. SERVER THREAD ONLY unless noted.                    */

    /* ── Player: equipment, cooldown, network (dedicated fns) ── */
    bool (*player_get_carried_item)(LeviRsPlayerSel sel, void* ctx, LeviRsStrSink sink);
    bool (*player_get_item)(LeviRsPlayerSel sel, int32_t slot, void* ctx, LeviRsStrSink sink);
    bool (*player_set_item)(LeviRsPlayerSel sel, int32_t slot, LeviRsStr item_snbt);
    /** All equipment as SNBT: [{slot, item_snbt}, …] slot: 0=mainhand 1=offhand 2-5=armor */
    bool (*player_get_equipment)(LeviRsPlayerSel sel, void* ctx, LeviRsStrSink sink);
    /** Ticks remaining for an item cooldown (-1 if not on cooldown / player offline). */
    int32_t (*player_get_cooldown)(LeviRsPlayerSel sel, LeviRsStr item_name);
    bool (*player_start_cooldown)(LeviRsPlayerSel sel, LeviRsStr item_name, int32_t ticks);
    bool (*player_get_network_status)(LeviRsPlayerSel sel, void* ctx, LeviRsStrSink sink);

    /* ── Actor: relationships, equipment, effects, geometry (dedicated fns) ── */
    bool (*actor_get_vehicle)(LeviRsActorId id, LeviRsActorId* out);
    bool (*actor_get_first_passenger)(LeviRsActorId id, LeviRsActorId* out);
    bool (*actor_get_owner)(LeviRsActorId id, LeviRsActorId* out);
    bool (*actor_get_target)(LeviRsActorId id, LeviRsActorId* out);
    /** slot: 0=mainhand 1=offhand 2=helmet 3=chestplate 4=leggings 5=boots */
    bool (*actor_get_equipped_item)(LeviRsActorId id, int32_t slot, void* ctx, LeviRsStrSink sink);
    bool (*actor_set_equipped_item)(LeviRsActorId id, int32_t slot, LeviRsStr item_snbt);
    /** SNBT [{id, ticks, amplifier, visible}, …] */
    bool (*actor_get_effects)(LeviRsActorId id, void* ctx, LeviRsStrSink sink);
    /** flag_index: ActorFlags enum value (0-based). */
    bool (*actor_get_status_flag)(LeviRsActorId id, int32_t flag_index);
    bool (*actor_set_status_flag)(LeviRsActorId id, int32_t flag_index, bool value);
    /** SNBT {type:"entity"|"block"|"none", pos:[x,y,z], entity_id?, block_name?} */
    bool (*actor_trace_ray)(LeviRsActorId id, float max_dist, bool include_actors, bool include_blocks, void* ctx, LeviRsStrSink sink);
    bool (*actor_distance_to)(LeviRsActorId id, LeviRsActorId other, double* out);
    /** SNBT {min:[x,y,z], max:[x,y,z]} */
    bool (*actor_get_aabb)(LeviRsActorId id, void* ctx, LeviRsStrSink sink);
    bool (*actor_clone)(LeviRsActorId id, int32_t dim, double x, double y, double z, LeviRsActorId* out);

    /* ── Block: state get/set, collision shape (dedicated fns) ── */
    bool (*block_get_state)(int32_t dim, int32_t x, int32_t y, int32_t z, LeviRsStr state_name, void* ctx, LeviRsStrSink sink);
    bool (*block_set_state)(int32_t dim, int32_t x, int32_t y, int32_t z, LeviRsStr state_name, LeviRsStr value);
    bool (*block_get_collision_shape)(int32_t dim, int32_t x, int32_t y, int32_t z, void* ctx, LeviRsStrSink sink);

    /* ── Item: enchants, matching, NBT (dedicated fns) ── */
    /** SNBT [{id, level}, …] */
    bool (*item_get_enchants)(LeviRsStr item_snbt, void* ctx, LeviRsStrSink sink);
    /** enchants_snbt = [{id, level}, …]; out = new item SNBT. */
    bool (*item_set_enchants)(LeviRsStr item_snbt, LeviRsStr enchants_snbt, void* ctx, LeviRsStrSink out);
    bool (*item_matches)(LeviRsStr a, LeviRsStr b);
    bool (*item_get_user_data)(LeviRsStr item_snbt, void* ctx, LeviRsStrSink sink);

    /* ── Level: biome, spawn, save, weather, path, sleep (dedicated fns) ── */
    bool (*level_get_biome)(int32_t dim, int32_t x, int32_t y, int32_t z, void* ctx, LeviRsStrSink sink);
    bool (*level_get_default_spawn)(int32_t* x, int32_t* y, int32_t* z);
    bool (*level_set_default_spawn)(int32_t x, int32_t y, int32_t z);
    bool (*level_save)();
    /** SNBT {sleeping, total_players, active_sleeping} */
    bool (*level_get_sleep_status)(void* ctx, LeviRsStrSink sink);
    bool (*level_update_weather)(float rain_level, int32_t rain_time, float lightning_level, int32_t lightning_time);
    /** SNBT {nodes:[{x,y,z}, …], reached:1b/0b} */
    bool (*level_find_path)(LeviRsActorId id, int32_t x, int32_t y, int32_t z, void* ctx, LeviRsStrSink sink);

    /* Future additive fields: append here only. */

    /* ═════════════════ Client-only function pointers (ABI v5 additive, client target) ═════════════════
     * Present ONLY when the loader is built against the client target
     * (LEVI_RS_TARGET_CLIENT). The server build's struct_size stops before
     * this block, so a server mod never sees these slots (and vice-versa).
     * The Rust levilamina-sys crate mirrors these under #[cfg(feature="client")].
     * All callbacks run on the CLIENT THREAD. */
#ifdef LEVI_RS_TARGET_CLIENT
    /** Local player's name via ll::service::getClientInstance()->getLocalPlayer().
     *  sink receives the name, or the call returns false if not in a level. */
    bool (*client_get_local_player)(void* ctx, LeviRsStrSink sink);

    /** True when the client is inside a level (a world is loaded). */
    bool (*client_is_in_level)();

    /** Current screen / UI name (e.g. "hud_screen", "pause_screen"). */
    bool (*client_get_screen_name)(void* ctx, LeviRsStrSink sink);

    /** Register a key binding via ll::input::KeyRegistry::getOrCreateKey.
     *  Returns NULL on failure. The handle is owned by the caller; drop with
     *  client_unregister_key. down_cb/up_cb fire on the client thread. */
    LeviRsKeyHandle (*client_register_key)(
        LeviRsModHandle mod,
        LeviRsStr name,
        int32_t const* key_codes,
        int32_t key_count,
        bool allow_remap,
        LeviRsKeyCb down_cb,
        LeviRsKeyCb up_cb,
        void* user
    );

    /** Unregister a key binding (destroys the ll::input::KeyHandle). */
    bool (*client_unregister_key)(LeviRsKeyHandle handle);

    /** Currently assigned key codes (may differ from defaults if remapped).
     *  sink receives a JSON-style array string "[1,2,3]". */
    bool (*client_get_key_codes)(LeviRsKeyHandle handle, void* ctx, LeviRsStrSink sink);
#endif

    // ── MoreDimensions (feature-gated; server build only) ──
    // Present when the loader is built with LEVI_RS_FEATURE_MORE_DIMENSIONS.
    // The Rust side gates these behind the `more_dimensions` feature.
#ifdef LEVI_RS_FEATURE_MORE_DIMENSIONS
    /** Check whether MoreDimensions is available in this loader build. */
    bool (*md_is_available)(void);

    /** Add a SimpleCustomDimension.
     *
     *  generatorType is ::GeneratorType **verbatim** — 1=Overworld, 2=Flat,
     *  3=Nether, 4=TheEnd, 5=Void. (This comment used to claim
     *  "0=Overworld 1=Nether 2=TheEnd 3=Flat 4=Void", which is the numbering
     *  bug that made "superflat" generate a nether. Values outside 1..5 are
     *  rejected rather than silently building a void world.)
     *
     *  Returns dim id (>=3) or -1 on failure. */
    int32_t (*md_add_simple_dimension)(LeviRsStr name, uint32_t seed, int32_t generatorType);

    /** Per-dimension rules, consulted by the loader's own hooks.
     *
     *  Why this exists instead of gamerules: Bedrock gamerules are
     *  server-wide. Setting `doMobSpawning=false` to quiet a creative plot
     *  world also stops spawning in the survival world. These flags are
     *  checked inside hooks on the actual call sites (Spawner::spawnMob,
     *  Level::explode, ...), so they really are per-dimension.
     *
     *  `rule` is one of LeviRsDimRule. Setting a rule on a dimension the
     *  loader doesn't know about is harmless — the tables are keyed by raw
     *  dimension id and consulted only when that id shows up in a hook.
     *
     *  Dimensions with no entry are left completely alone: the hooks fall
     *  through to origin(), so vanilla dimensions keep vanilla behaviour
     *  without the caller having to opt out. */
    void (*md_set_dimension_rule)(int32_t dimension, int32_t rule, bool allow);

    /** Read back a rule. `outAllow` is only written when the dimension has an
     *  explicit entry for that rule; returns false otherwise. */
    bool (*md_get_dimension_rule)(int32_t dimension, int32_t rule, bool* outAllow);

    /** Drop every rule for a dimension (used when a world is deleted). */
    void (*md_clear_dimension_rules)(int32_t dimension);

    /** Resolve a dimension name to its id. Returns -1 if not found.
     *
     *  Only returns an id for names that are ACTUALLY registered: unknown
     *  names yield -1, never VanillaDimensions::Undefined() (whose numeric
     *  value is mutated at runtime and looks like a valid id).
     *
     *  Note this is rarely what you want. `md_add_simple_dimension` and
     *  `md_add_plot_dimension` are idempotent — re-registering the same name
     *  on a later boot returns the same persisted id — so callers should
     *  register unconditionally at startup instead of probing first. */
    int32_t (*md_get_dimension_id)(LeviRsStr name);

    /** Add a plot-world dimension: a custom dimension whose chunk generator
     *  produces a plot grid (plots / roads / borders) at generation time,
     *  instead of the caller painting blocks afterwards.
     *
     *  `layout_snbt` is a CompoundTag SNBT string:
     *    {plotSize:64, roadWidth:7, borderWidth:1, floorY:64,
     *     floorBlock:"minecraft:grass_block", fillBlock:"minecraft:dirt",
     *     roadBlock:"minecraft:birch_planks",
     *     borderBlock:"minecraft:stone_block_slab", biome:"minecraft:plains"}
     *  Missing keys fall back to those defaults; all values are clamped to a
     *  safe range on the C++ side. The layout is persisted with the dimension,
     *  so it stays fixed across restarts even if the caller's config changes.
     *
     *  Grid convention (the Rust side MUST match): with cell = plotSize +
     *  roadWidth, a column at world (x, z) is road when
     *  mod(x,cell) >= plotSize || mod(z,cell) >= plotSize; otherwise it is
     *  border when within borderWidth of the plot edge; otherwise plot.
     *
     *  Idempotent, like md_add_simple_dimension. Returns dim id (>=3) or -1. */
    int32_t (*md_add_plot_dimension)(LeviRsStr name, uint32_t seed, LeviRsStr layout_snbt);
#endif
} LeviRsApi;

/**
 * Filled in by the Rust mod inside levi_rs_main.
 * `instance` is an opaque pointer owned by the Rust side.
 * Callbacks may be NULL (treated as "always succeeds").
 */
typedef struct LeviRsModVTable
{
    uint32_t abi_version; /* must be set to LEVI_RS_ABI_VERSION by the mod */
    void* instance;
    bool (*on_enable)(void* instance);
    bool (*on_disable)(void* instance);
    bool (*on_unload)(void* instance);
} LeviRsModVTable;

/**
 * The single symbol every Rust mod must export:
 *
 *   bool levi_rs_main(const LeviRsApi* api, LeviRsModHandle self,
 *                     LeviRsModVTable* out_vtable);
 *
 * Called once on the server thread while the mod is being loaded.
 * Return false to abort loading.
 */
typedef bool (*LeviRsMainFn)(const LeviRsApi* api, LeviRsModHandle self, LeviRsModVTable* out_vtable);

#define LEVI_RS_MAIN_SYMBOL "levi_rs_main"

/**
 * Runtime check for the LeviRsStr layout assumption (see comment above).
 * Called once from Entry.cpp before any Rust mod loads. Returns false if
 * the assumption fails — refuse to continue rather than risk corrupted
 * strings crossing the boundary.
 */
bool leviRsVerifyStrLayout();
} // extern "C"
