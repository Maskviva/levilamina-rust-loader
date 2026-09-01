/**
 * bridge/hooks/ProjectileEvent.cpp — "PlayerSpawnProjectileEvent": a player is
 * about to launch a projectile, and it **can be cancelled**.
 *
 * # Why `PlayerUseItemEvent` was never enough
 *
 * The plugin originally routed throwing through `PlayerUseItemEvent` and then
 * guessed at `PlayerThrowProjectileEvent` / `PlayerShootEvent` (neither exists
 * on the LL bus). `PlayerUseItemEvent` fires from `GameMode::useItem`, which
 * covers the click-to-throw items but not the charge-and-release path used by
 * bow, crossbow and trident.
 *
 * # Why the first rewrite of this file also did not work
 *
 * The first version hooked `BedrockSpawner::spawnProjectile`, copying
 * LegacyScriptEngine's `onSpawnProjectile`. It installs, and it never fires for
 * a player throw on this build — snowballs kept flying for denied players.
 *
 * The reason is a version drift LSE has not caught up with: vanilla projectiles
 * are **item components** now. A snowball is a `ComponentItem` carrying a
 * `ThrowableItemComponent` plus a `ProjectileItemComponent`, and the throw goes
 *
 *   `ThrowableItemComponent::_doThrow`
 *     → `ProjectileItemComponent::shootProjectile(region, aimPos, aimDir, power, player)`
 *       → `Item::createProjectileActor` (per-item override — `SnowballItem`,
 *          `EggItem`, `SplashPotionItem`, `ArrowItem`, …)
 *
 * `Spawner::spawnProjectile` is no longer on that path. Hooking it is not
 * *wrong*, it is simply upstream of nothing the player does.
 *
 * # Where the hooks are now
 *
 * Ordered by how much of the problem each one covers:
 *
 *   1. `ProjectileItemComponent::shootProjectile` — every component-driven
 *      projectile: snowball, egg, ender pearl, splash and lingering potions,
 *      experience bottle, wind charge, fire charge, and the arrows a bow or
 *      crossbow releases. Carries `Player*` directly. Returns `Actor*`, so
 *      cancelling is `nullptr`.
 *
 *   2. `ShooterItemComponent::_shootProjectiles` — the bow/crossbow release
 *      itself. Redundant with (1) for the projectile, but it fires *before* the
 *      per-arrow loop, so cancelling here refuses a multishot crossbow in one
 *      decision instead of three.
 *
 *   3. `TridentItem::releaseUsing` — tridents are still a bespoke item and
 *      reach neither (1) nor (2).
 *
 *   4. `CrossbowItem::_shootFirework` — firework rockets from a crossbow, same
 *      reason.
 *
 *   5. `BedrockSpawner::spawnProjectile` — kept as a backstop for anything that
 *      still uses the old path (add-on entities, some dispenser-adjacent code).
 *
 * # Re-entrancy
 *
 * Hooks 1, 2 and 5 nest: a bow release can pass through all three for one
 * arrow. `gDispatching` collapses that to a single decision per launch —
 * without it a denied player would get three "no permission" evaluations and,
 * more visibly, three log lines per shot.
 *
 * # Ammo is not refunded
 *
 * By the time these run the arrow has already left the inventory. Cancelling
 * stops the *projectile*, not the consumption; the client re-syncs within a
 * tick. That is the deliberate trade — refusing the shot is the security
 * property. If the lost arrow ever matters, the fix belongs upstream at
 * `GameMode::releaseUsingItem`, not here.
 *
 * # Payload
 *
 * ```text
 * {eventId, x, y, z, dim, projectile, _player:{name,xuid,uuid}}
 * ```
 *
 * `projectile` is a best-effort name and may be empty for hooks that do not
 * know the entity type yet (2 and 4 fire before it is resolved). Subscribers
 * must treat it as informational, not as the thing they key the decision on.
 */
#include "bridge/Common.h"
#include "bridge/hooks/HookEvents.h"

#include <string>

#include "ll/api/memory/Hook.h"

#include "mc/common/Globals.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/VanillaActorRendererId.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/CrossbowItem.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/TridentItem.h"
#include "mc/world/item/components/ProjectileItemComponent.h"
#include "mc/world/item/components/ShooterItemComponent.h"
#include "mc/world/level/BedrockSpawner.h"
#include "mc/world/level/BlockSource.h"

namespace levi_rs::bridge
{
    namespace
    {
        HookEventDef& projectileDef(); // fwd

        /**
         * Set while a decision is in flight, so nested hooks don't re-ask.
         * Server thread only, and every hook below restores it on the way out.
         */
        bool gDispatching = false;

        struct DispatchGuard
        {
            DispatchGuard() { gDispatching = true; }
            ~DispatchGuard() { gDispatching = false; }
        };

        /** Shared payload builder — every hook reports the same event shape. */
        std::string buildSnbt(Player& p, std::string const& projectile, ::Vec3 const& at, int dim)
        {
            return "{\"eventId\":\"PlayerSpawnProjectileEvent\""
                ",\"x\":" + snbtNum(static_cast<int>(at.x))
                + ",\"y\":" + snbtNum(static_cast<int>(at.y))
                + ",\"z\":" + snbtNum(static_cast<int>(at.z))
                + ",\"dim\":" + snbtNum(dim)
                + ",\"projectile\":\"" + snbtEscape(projectile)
                + "\",\"_player\":{\"name\":\"" + snbtEscape(p.getRealName())
                + "\",\"xuid\":\"" + snbtEscape(p.getXuid())
                + "\",\"uuid\":\"" + snbtEscape(p.getUuid().asString()) + "\"}}";
        }

        /** Ask once. Returns true when the launch must be refused. */
        bool refuseLaunch(Player& p, std::string const& projectile, ::Vec3 const& at, int dim)
        {
            DispatchGuard guard;
            return dispatchHookEventCancellable(projectileDef(), buildSnbt(p, projectile, at, dim));
        }

        // ── 1. Every component-driven projectile ─────────────────────────────

        LL_TYPE_INSTANCE_HOOK(
            ShootProjectileHook,
            ll::memory::HookPriority::Normal,
            ProjectileItemComponent,
            &ProjectileItemComponent::shootProjectile,
            ::Actor*,
            ::BlockSource& region,
            ::Vec3 const& aimPos,
            ::Vec3 const& aimDir,
            float power,
            ::Player* player)
        {
            auto& def = projectileDef();
            if (!def.live() || gDispatching || player == nullptr)
            {
                return origin(region, aimPos, aimDir, power, player);
            }

            if (refuseLaunch(*player, {}, aimPos, static_cast<int>(region.getDimensionId())))
            {
                return nullptr;
            }
            return origin(region, aimPos, aimDir, power, player);
        }

        // ── 2. Bow / crossbow release ────────────────────────────────────────

        LL_TYPE_INSTANCE_HOOK(
            ShooterReleaseHook,
            ll::memory::HookPriority::Normal,
            ShooterItemComponent,
            &ShooterItemComponent::_shootProjectiles,
            void,
            ::ItemStack& shooterStack,
            ::Player* player,
            int durationLeft)
        {
            auto& def = projectileDef();
            if (!def.live() || gDispatching || player == nullptr)
            {
                return origin(shooterStack, player, durationLeft);
            }

            std::string shooterName;
            if (!shooterStack.isNull()) shooterName = shooterStack.getTypeName();

            if (refuseLaunch(
                *player,
                shooterName,
                player->getPosition(),
                static_cast<int>(player->getDimensionId())))
            {
                return;
            }
            origin(shooterStack, player, durationLeft);
        }

        // ── 3. Thrown trident ────────────────────────────────────────────────

        LL_TYPE_INSTANCE_HOOK(
            TridentReleaseHook,
            ll::memory::HookPriority::Normal,
            TridentItem,
            &TridentItem::$releaseUsing,
            void,
            ::ItemStack& item,
            ::Player* player,
            int durationLeft)
        {
            auto& def = projectileDef();
            if (!def.live() || gDispatching || player == nullptr)
            {
                return origin(item, player, durationLeft);
            }

            std::string projName;
            projName = ::VanillaActorRendererId::trident().getString();

            if (refuseLaunch(
                *player,
                projName,
                player->getPosition(),
                static_cast<int>(player->getDimensionId())))
            {
                return;
            }
            origin(item, player, durationLeft);
        }

        // ── 4. Crossbow loaded with a firework rocket ────────────────────────

        LL_TYPE_INSTANCE_HOOK(
            CrossbowFireworkHook,
            ll::memory::HookPriority::Normal,
            CrossbowItem,
            &CrossbowItem::_shootFirework,
            void,
            ::ItemInstance const& projectileInstance,
            ::Player& player)
        {
            auto& def = projectileDef();
            if (!def.live() || gDispatching)
            {
                return origin(projectileInstance, player);
            }

            std::string projName;
            projName = projectileInstance.getTypeName();

            if (refuseLaunch(
                player,
                projName,
                player.getPosition(),
                static_cast<int>(player.getDimensionId())))
            {
                return;
            }
            origin(projectileInstance, player);
        }

        // ── 5. Legacy spawner path, kept as a backstop ───────────────────────

        LL_TYPE_INSTANCE_HOOK(
            SpawnProjectileHook,
            ll::memory::HookPriority::Normal,
            BedrockSpawner,
            &BedrockSpawner::$spawnProjectile,
            ::Actor*,
            ::BlockSource& region,
            ::ActorDefinitionIdentifier const& id,
            ::Actor* spawner,
            ::Vec3 const& position,
            ::Vec3 const& direction)
        {
            auto& def = projectileDef();
            if (!def.live() || gDispatching || spawner == nullptr || !spawner->isPlayer())
            {
                return origin(region, id, spawner, position, direction);
            }

            // Tridents are handled by TridentReleaseHook; reporting them here
            // too would fire the event twice for one throw.
            static auto& tridentName = EntityCanonicalName(::ActorType::Trident);
            if (*id.mCanonicalName == tridentName)
            {
                return origin(region, id, spawner, position, direction);
            }

            auto& p = *static_cast<Player*>(spawner);

            std::string projName;
            projName = id.mCanonicalName->getString();

            if (refuseLaunch(p, projName, position, static_cast<int>(region.getDimensionId())))
            {
                return nullptr;
            }
            return origin(region, id, spawner, position, direction);
        }

        HookEventDef gDef{
            "PlayerSpawnProjectileEvent",
            []
            {
                ShootProjectileHook::hook();
                ShooterReleaseHook::hook();
                TridentReleaseHook::hook();
                CrossbowFireworkHook::hook();
                SpawnProjectileHook::hook();
            }
        };
        HookEventDef& projectileDef() { return gDef; }

        HookEventRegistrar gReg{gDef};
    } // namespace
} // namespace levi_rs::bridge
