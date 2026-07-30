add_rules("mode.debug", "mode.release")

add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")

-- ── Dual-target build: server (default) or client ─────────────────────
-- Select with:  xmake f --target_type=client
-- The two targets produce SEPARATE dlls (levilamina-rust-loader.dll for
-- server, levilamina-rust-loader-client.dll for client). The Rust SDK
-- picks a side via features (server/client, mutually exclusive).
option("target_type")
    set_default("server")
    set_values("server", "client")
    set_description("Build target: server (BDS) or client (MC client)")
option_end()

-- MoreDimensions is always compiled into server builds — no build flag
-- needed. Client builds never include it (the Bedrock client can't host
-- custom dimensions). The Rust side decides at runtime whether to call
-- the API via more_dimensions::is_available().
local target_type = get_config("target_type") or "server"
local is_client = (target_type == "client")
local more_dims = not is_client

if is_client then
    add_requires("levilamina 26.20.4", {configs = {target_type = "client"}})
else
    add_requires("levilamina 26.20.4", {configs = {target_type = "server"}})
    add_requires("legacymoney 0.19.0", {configs = {target_type = "server"}})
    add_requires("bedrockdata v26.20.5-server.4")
end

add_requires("prelink v0.7.1")
add_requires("levibuildscript")
add_requires("zlib 1.3.1")

if more_dims then
    add_requires("snappy")
    add_requires("magic_enum")
end

if not has_config("vs_runtime") then
    set_runtimes("MD")
end

-- Source files shared by both targets (compile on server AND client).
local shared_sources = {
    "src/Entry.cpp",
    "src/MemoryOperators.cpp",
    "src/RustModManager.cpp",
    "src/bridge/Common.cpp",
    "src/bridge/LogScheduler.cpp",
    "src/bridge/Events.cpp",
    "src/bridge/Players.cpp",
    "src/bridge/Actors.cpp",
    "src/bridge/World.cpp",
    "src/bridge/Items.cpp",
    "src/bridge/Containers.cpp",
    "src/bridge/NbtApi.cpp",
    "src/bridge/KvDbApi.cpp",
    "src/bridge/SysInfo.cpp",
    "src/bridge/ApiTable.cpp",
}

-- Server-only source files (excluded from client build).
-- These reference server-only headers (ll/api/command, mc/server/*, legacymoney).
local server_only_sources = {
    "src/bridge/Commands.cpp",
    "src/bridge/Server.cpp",
    "src/bridge/Money.cpp",
    "src/bridge/MoneyGuard.cpp",
    "src/bridge/SimPlayer.cpp",
    "src/bridge/ScoreboardApi.cpp",
    "src/bridge/Forms.cpp",
    "src/bridge/WorldInfo.cpp",
    "src/bridge/Packets.cpp",
    "src/bridge/PacketHooks.cpp",
    "src/bridge/GapFill.cpp",
    "src/bridge/hooks/DestroyEvents.cpp",
    "src/bridge/hooks/ContainerEvents.cpp",
    "src/bridge/hooks/DimensionEvents.cpp",
    "src/bridge/hooks/HopperEvents.cpp",
    "src/bridge/hooks/HookEvents.cpp",
    "src/bridge/hooks/Profiler.cpp",
    "src/bridge/hooks/TickControl.cpp",
    "src/bridge/hooks/UseItemOnEvent.cpp",
    -- Protection hooks added to close the "guessed event id" holes: drop item,
    -- ride, interact-with-entity, throw projectile, pressure plate / tripwire.
    -- None of these had a subscribable event before, in LL or in vanilla.
    "src/bridge/hooks/DropItemEvent.cpp",
    "src/bridge/hooks/RideEvent.cpp",
    "src/bridge/hooks/InteractEntityEvent.cpp",
    "src/bridge/hooks/ProjectileEvent.cpp",
    "src/bridge/hooks/PressurePlateEvent.cpp",
    -- Pushing entities is the one griefing method that survives a fully
    -- locked-down plot: no click, no log line. Its own TU so it can be
    -- dropped independently if PushableByEntityUtility drifts upstream.
    "src/bridge/hooks/PushEntityEvent.cpp",
}

-- Client-only source files (excluded from server build).
local client_only_sources = {
    "src/bridge/Client.cpp",
    "src/bridge/ClientStubs.cpp",
}

-- MoreDimensions source files (always compiled into server builds).
local more_dims_sources = {
    "src/more_dimensions/ChunkTrace.cpp",
    "src/more_dimensions/CustomDimensionConfig.cpp",
    "src/more_dimensions/CustomDimensionManager.cpp",
    "src/more_dimensions/DimensionRules.cpp",
    "src/more_dimensions/NativeDimensions.cpp",
    "src/more_dimensions/MoreDimensionsBridge.cpp",
    "src/more_dimensions/SimpleCustomDimension.cpp",
    "src/more_dimensions/PlotDimension.cpp",
    "src/more_dimensions/PlotGenerator.cpp",
    "src/more_dimensions/Utils.cpp",
}

local target_name = is_client and "levilamina-rust-loader-client" or "levilamina-rust-loader"

target(target_name)
    on_load(function (target)
        target:add("rules", "@levibuildscript/linkrule")
        target:add("rules", "@levibuildscript/modpacker")
    end)
    add_cxflags("/EHa", "/utf-8", "/W4")
    add_defines("NOMINMAX", "UNICODE")

    if is_client then
        add_defines("LEVI_RS_TARGET_CLIENT")
    else
        add_defines("LEVI_RS_TARGET_SERVER")
    end

    if more_dims then
        add_defines("LEVI_RS_FEATURE_MORE_DIMENSIONS")
    end

    add_files(unpack(shared_sources))
    if is_client then
        add_files(unpack(client_only_sources))
    else
        add_files(unpack(server_only_sources))
    end

    if more_dims then
        add_files(unpack(more_dims_sources))
    end

    add_includedirs("src")
    if is_client then
        add_packages("levilamina")
    else
        add_packages("levilamina", "legacymoney")
        add_shflags("/DELAYLOAD:LegacyMoney.dll", {force = true})
        add_syslinks("delayimp")
    end
    if more_dims then
        add_packages("snappy", "magic_enum")
    end
    set_exceptions("none") -- /EHa
    set_kind("shared")
    set_languages("c++20")
    set_symbols("debug")