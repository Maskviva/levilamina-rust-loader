/**
 * bridge/hooks/DestroyEvents.cpp — "PlayerStartDestroyBlockEvent": fires
 * when a player STARTS breaking a block (GameMode::startDestroyBlock) —
 * earlier than LeviLamina's built-in PlayerDestroyBlockEvent, which fires on
 * completion. This is the autotool timing: the event is dispatched BEFORE
 * origin and callbacks run synchronously, so a subscriber that swaps the
 * selected hotbar slot does it before the destroy logic reads the tool in
 * hand. Lifecycle rules in HookEvents.h apply.
 */
#include "bridge/Common.h"
#include "bridge/hooks/HookEvents.h"

#include <string>

#include "ll/api/memory/Hook.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/level/BlockPos.h"

namespace levi_rs::bridge
{
    namespace
    {
        HookEventDef& destroyDef(); // fwd

        LL_TYPE_INSTANCE_HOOK(
            StartDestroyBlockHook,
            ll::memory::HookPriority::Normal,
            GameMode,
            &GameMode::$startDestroyBlock,
            bool,
            ::BlockPos const& pos,
            uchar face,
            bool& hasDestroyedBlock)
        {
            auto& def = destroyDef();
            if (!def.live())
            {
                return origin(pos, face, hasDestroyedBlock); // installed but idle
            }

            // GameMode::mPlayer is TypedStorage over Player& — reference
            // specialisation, so the member IS the reference (no .get()).
            Player& p = this->mPlayer;

            std::string snbt = "{\"eventId\":\"PlayerStartDestroyBlockEvent\""
                               ",\"x\":" + std::to_string(pos.x)
                             + ",\"y\":" + std::to_string(pos.y)
                             + ",\"z\":" + std::to_string(pos.z)
                             + ",\"face\":" + std::to_string(static_cast<int>(face))
                             + ",\"_player\":{\"name\":\"" + snbtEscape(p.getRealName())
                             + "\",\"xuid\":\"" + snbtEscape(p.getXuid())
                             + "\",\"uuid\":\"" + snbtEscape(p.getUuid().asString()) + "\"}}";
            dispatchHookEvent(def, snbt); // BEFORE origin — see file header

            return origin(pos, face, hasDestroyedBlock);
        }

        HookEventDef gDef{"PlayerStartDestroyBlockEvent", [] { StartDestroyBlockHook::hook(); }};
        HookEventDef& destroyDef() { return gDef; }

        HookEventRegistrar gReg{gDef};
    } // namespace
} // namespace levi_rs::bridge
