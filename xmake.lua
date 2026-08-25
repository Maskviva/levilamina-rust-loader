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
    "src/core/Entry.cpp",
    "src/core/MemoryOperators.cpp",
    "src/core/RustModManager.cpp",
    "src/bridge/core/Common.cpp",
    "src/bridge/core/LogScheduler.cpp",
    "src/bridge/runtime/Events.cpp",
    "src/bridge/actors/Players.cpp",
    "src/bridge/actors/Actors.cpp",
    -- 追加槽（190..192）：删实体、补血、设生物群系。
    -- **单独一个文件**，因为它们要靠 struct_size 守卫，而且随时可能因为
    -- BDS 改签名单独调整 —— 混进 Actors.cpp 的话，下次排查"哪些是新加的"
    -- 要一行行看 git。
    "src/bridge/runtime/Extras.cpp",
    "src/bridge/world/World.cpp",
    "src/bridge/world/Items.cpp",
    "src/bridge/world/Containers.cpp",
    "src/bridge/runtime/data/NbtApi.cpp",
    "src/bridge/runtime/data/KvDbApi.cpp",
    "src/bridge/runtime/SysInfo.cpp",
    -- 跨 mod 事件总线：不碰任何服务端专属头文件，客户端构建也编进去
    -- （客户端一样可以装多个 rust mod，互相通信的需求是一样的）。
    "src/bridge/core/Bus.cpp",
    -- 跨 mod 服务注册（查询式调用）。和总线一样不碰服务端专属头文件。
    "src/bridge/core/Services.cpp",
    "src/bridge/core/Lane.cpp",
    "src/bridge/core/ApiTable.cpp",
}

-- Server-only source files (excluded from client build).
-- These reference server-only headers (ll/api/command, mc/server/*, legacymoney).
local server_only_sources = {
    -- Runtime rust-mod control (/llr list|load|unload|reload). Server only:
    -- it needs the command registrar.
    "src/core/ModControl.cpp",
    "src/bridge/runtime/Commands.cpp",
    "src/bridge/runtime/Server.cpp",
    "src/bridge/actors/Money.cpp",
    "src/bridge/actors/MoneyGuard.cpp",
    "src/bridge/actors/SimPlayer.cpp",
    "src/bridge/net/ScoreboardApi.cpp",
    "src/bridge/actors/Forms.cpp",
    "src/bridge/world/WorldInfo.cpp",
    "src/bridge/net/Packets.cpp",
    "src/bridge/net/PacketHooks.cpp",
    "src/bridge/world/GapFill.cpp",
    "src/bridge/world/Edit.cpp",
    "src/bridge/hooks/player/AttackEvent.cpp",
    "src/bridge/hooks/world/DestroyEvents.cpp",
    "src/bridge/hooks/world/ContainerEvents.cpp",
    "src/bridge/hooks/world/DimensionEvents.cpp",
    "src/bridge/hooks/world/HopperEvents.cpp",
    "src/bridge/hooks/engine/HookEvents.cpp",
    "src/bridge/hooks/engine/Profiler.cpp",
    "src/bridge/hooks/engine/TickControl.cpp",
    "src/bridge/hooks/world/UseItemOnEvent.cpp",
    -- Protection hooks added to close the "guessed event id" holes: drop item,
    -- ride, interact-with-entity, throw projectile, pressure plate / tripwire.
    -- None of these had a subscribable event before, in LL or in vanilla.
    "src/bridge/hooks/protect/DropItemEvent.cpp",
    "src/bridge/hooks/protect/TakeEntityEvent.cpp",
    "src/bridge/hooks/protect/RideEvent.cpp",
    "src/bridge/hooks/protect/InteractEntityEvent.cpp",
    "src/bridge/hooks/protect/ProjectileEvent.cpp",
    "src/bridge/hooks/protect/PressurePlateEvent.cpp",
    -- Pushing entities is the one griefing method that survives a fully
    -- locked-down plot: no click, no log line. Its own TU so it can be
    -- dropped independently if PushableByEntityUtility drifts upstream.
    "src/bridge/hooks/protect/PushEntityEvent.cpp",
    -- 游戏模式强制。挂 Player::$setPlayerGameType —— 进世界时套一次只覆盖入口，
    -- 玩家进去之后 /gamemode 就绕过去了。
    "src/bridge/hooks/player/GameModeEvent.cpp",
}

-- Client-only source files (excluded from server build).
local client_only_sources = {
    "src/bridge/net/Client.cpp",
    "src/bridge/net/ClientStubs.cpp",
}

-- MoreDimensions source files (always compiled into server builds).
local more_dims_sources = {
    "src/more_dimensions/rt/ChunkTrace.cpp",
    "src/more_dimensions/dim/CustomDimensionConfig.cpp",
    "src/more_dimensions/dim/CustomDimensionManager.cpp",
    "src/more_dimensions/dim/DimensionRules.cpp",
    "src/more_dimensions/dim/NativeDimensions.cpp",
    "src/more_dimensions/rt/MoreDimensionsBridge.cpp",
    "src/more_dimensions/dim/SimpleCustomDimension.cpp",
    "src/more_dimensions/plot/PlotDimension.cpp",
    "src/more_dimensions/plot/PlotGenerator.cpp",
    -- 地皮边界约束（活塞 / 实体不出地皮）。网格和合并表由 Rust 侧推过来。
    "src/more_dimensions/plot/PlotConfine.cpp",
    "src/more_dimensions/rt/Utils.cpp",
}

local target_name = is_client and "levilamina-rust-loader-client" or "levilamina-rust-loader"

target(target_name)
    on_load(function (target)
        target:add("rules", "@levibuildscript/linkrule")
        target:add("rules", "@levibuildscript/modpacker")
    end)
    -- /EHsc，不是 /EHa。
    --
    -- /EHa（异步异常）会让 `catch (...)` 连 SEH 一起接住，包括访问违例。于是
    -- 这个仓库里每一处防御性的 `catch (...)` 都变成崩溃报告抑制器：吞掉错误、
    -- 返回一个兜底值、让服务器带着半更新的引擎状态继续跑。更糟的是
    -- LeviLamina 自己装了 CrashLogger（AddVectoredExceptionHandler +
    -- SetUnhandledExceptionFilter），它的全部工作就是把这类错误变成 dump ——
    -- 上游有个吞掉 AV 的 catch，dump 就永远写不出来。
    --
    -- LeviLamina 和 LegacyMoney 都已经这么做了：给 MSVC 设 /EHa，再用
    -- `{tools = {"clang_cl"}}` 覆盖成 /EHs，而且两者都 set_toolchains("clang-cl")
    -- 写死。也就是说这个生态里实际编译出来的东西全是 /EHs，只有本 loader
    -- 无条件用 /EHa。这里只是把它变成无条件的一致。
    --
    -- 异常本身照常工作，消失的只是接住 SEH 的行为。
    add_cxflags("/EHsc", "/utf-8", "/W4")
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
    -- xmake 自己的异常处理关掉，由上面的 /EHsc 决定。
    set_exceptions("none")
    set_kind("shared")
    set_languages("c++20")
    set_symbols("debug")