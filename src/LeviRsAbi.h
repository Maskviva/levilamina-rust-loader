/**
 * levilamina-rs C++ ABI — v2
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

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" {

#define LEVI_RS_ABI_VERSION 2u

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
    void*         user,
    LeviRsStr     event_id,
    LeviRsStr     snbt,
    void*         write_ctx,
    LeviRsStrSink write_back
);

/**
 * Custom command callback.
 *   args        : raw text following the command name (may be empty).
 *   origin_name : display name of the command origin (player name / "Server").
 *   out_success / out_error : call any number of times to emit output lines.
 */
typedef void (*LeviRsCommandCb)(
    void*         user,
    LeviRsStr     args,
    LeviRsStr     origin_name,
    void*         out_ctx,
    LeviRsStrSink out_success,
    LeviRsStrSink out_error
);

/** Output sink for execute_command: full command output + success flag. */
typedef void (*LeviRsCmdOutputSink)(void* ctx, bool success, LeviRsStr output);

/**
 * Function table handed to the Rust mod at load time.
 * Pointer remains valid for the whole lifetime of the mod.
 */
typedef struct LeviRsApi {
    /** == LEVI_RS_ABI_VERSION of the loader. */
    uint32_t abi_version;
    /** sizeof(LeviRsApi) as compiled into the loader; enables forward-compat checks. */
    uint32_t struct_size;

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
    int32_t (*gaming_status)(void);

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
        LeviRsStr       event_id,
        int32_t         priority,
        LeviRsEventCb   cb,
        void*           user
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
        LeviRsStr       name,
        LeviRsStr       description,
        int32_t         permission,
        LeviRsCommandCb cb,
        void*           user
    );

    /**
     * Current server tick (the tickID from Level::getCurrentTick()).
     * Returns 0 when the level is not ready. Server thread only.
     */
    uint64_t (*get_current_tick)(void);

    /**
     * Seconds taken by the last tick (mTickDeltaTime; 0.05 at 20 TPS).
     * TPS = 1.0 / tick_delta_time when > 0. Returns -1.0 if unavailable.
     * Server thread only.
     */
    double (*get_tick_delta_time)(void);

    /**
     * Number of currently connected players
     * (Level::getActivePlayerCount()). Server thread only.
     */
    int32_t (*get_player_count)(void);

    /**
     * Whether the simulation is currently paused
     * (Level::getSimPaused()). Server thread only.
     */
    bool (*get_sim_paused)(void);

    /* ABI v3+: append new fields here only. */
} LeviRsApi;

/**
 * Filled in by the Rust mod inside levi_rs_main.
 * `instance` is an opaque pointer owned by the Rust side.
 * Callbacks may be NULL (treated as "always succeeds").
 */
typedef struct LeviRsModVTable {
    uint32_t abi_version; /* must be set to LEVI_RS_ABI_VERSION by the mod */
    void*    instance;
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