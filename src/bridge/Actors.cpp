/**
 * bridge/Actors.cpp — actor enumeration, snapshots, properties, actions and
 * spawning (ABI v4 §C). Actor handles are ActorUniqueIDs, re-resolved via
 * Level::fetchEntity on every call.
 */
#include "bridge/Api.h"
#include "bridge/Common.h"

#include <string>

#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/effect/MobEffect.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/Spawner.h"

namespace levi_rs::bridge
{
    void api_list_actors(int32_t dim, void* ctx, LeviRsActorSink sink)
    {
        auto* level = levelReady();
        if (!level || !sink) return;
        for (auto* actor : level->getRuntimeActorList())
        {
            if (!actor) continue;
            if (dim >= 0 && static_cast<int>(actor->getDimensionId()) != dim) continue;
            sink(ctx, actor->getOrCreateUniqueID().rawID, actor->getTypeName());
        }
    }

    bool api_actor_snapshot(LeviRsActorId id, void* ctx, LeviRsStrSink snbtSink)
    {
        Actor* actor = resolveActor(id);
        if (!actor || !snbtSink) return false;
        CompoundTag tag;
        if (!actor->save(tag)) return false;
        snbtSink(ctx, tag.toSnbt(SnbtFormat::Minimize));
        return true;
    }

    bool api_actor_get_num(LeviRsActorId id, int32_t prop, double* out)
    {
        Actor* actor = resolveActor(id);
        if (!actor || !out) return false;
        switch (prop)
        {
        case LEVI_RS_APROP_POS_X:
            *out = actor->getPosition().x;
            return true;
        case LEVI_RS_APROP_POS_Y:
            *out = actor->getPosition().y;
            return true;
        case LEVI_RS_APROP_POS_Z:
            *out = actor->getPosition().z;
            return true;
        case LEVI_RS_APROP_ROT_PITCH:
            *out = actor->getRotation().x;
            return true;
        case LEVI_RS_APROP_ROT_YAW:
            *out = actor->getRotation().y;
            return true;
        case LEVI_RS_APROP_DIMENSION:
            *out = static_cast<double>(static_cast<int>(actor->getDimensionId()));
            return true;
        case LEVI_RS_APROP_HEALTH:
            *out = static_cast<double>(actor->getHealth());
            return true;
        case LEVI_RS_APROP_MAX_HEALTH:
            *out = static_cast<double>(actor->getMaxHealth());
            return true;
        case LEVI_RS_APROP_IS_ALIVE:
            *out = actor->isAlive() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_APROP_IS_ON_GROUND:
            *out = actor->isOnGround() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_APROP_IS_IN_WATER:
            *out = actor->isInWater() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_APROP_IS_IN_LAVA:
            *out = actor->isInLava() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_APROP_IS_ON_FIRE:
            *out = actor->isOnFire() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_APROP_IS_INVISIBLE:
            *out = actor->isInvisible() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_APROP_IS_SNEAKING:
            *out = actor->isSneaking() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_APROP_IS_BABY:
            *out = actor->isBaby() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_APROP_IS_RIDING:
            *out = actor->isRiding() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_APROP_IS_TAME:
            *out = actor->isTame() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_APROP_SPEED:
            *out = static_cast<double>(actor->getSpeedInMetersPerSecond());
            return true;
        default:
            return false;
        }
    }

    bool api_actor_get_str(LeviRsActorId id, int32_t prop, void* ctx, LeviRsStrSink sink)
    {
        Actor* actor = resolveActor(id);
        if (!actor || !sink) return false;
        switch (prop)
        {
        case LEVI_RS_ASTR_TYPE_NAME:
            sink(ctx, actor->getTypeName());
            return true;
        case LEVI_RS_ASTR_NAME_TAG:
            sink(ctx, actor->getNameTag());
            return true;
        default:
            return false;
        }
    }

    bool api_actor_action(
        LeviRsActorId id,
        int32_t action,
        LeviRsStr sarg,
        double a,
        double b,
        double c,
        void* ctx,
        LeviRsStrSink out
    )
    {
        Actor* actor = resolveActor(id);
        if (!actor) return false;
        switch (action)
        {
        case LEVI_RS_AACT_KILL:
            actor->kill();
            return true;
        case LEVI_RS_AACT_DESPAWN:
            actor->despawn();
            return true;
        case LEVI_RS_AACT_HEAL:
            actor->heal(static_cast<int>(a));
            return true;
        case LEVI_RS_AACT_SET_ON_FIRE:
            actor->setOnFire(static_cast<int>(a));
            return true;
        case LEVI_RS_AACT_TELEPORT:
            {
                std::string dimStr{sarg};
                int dim = static_cast<int>(actor->getDimensionId());
                if (!dimStr.empty())
                {
                    try
                    {
                        dim = std::stoi(dimStr);
                    }
                    catch (...)
                    {
                        return false;
                    }
                }
                // teleport(pos, dim, rotation) — preserve the actor's current facing.
                actor->teleport(Vec3{(float)a, (float)b, (float)c}, DimensionType{dim}, actor->getRotation());
                return true;
            }
        case LEVI_RS_AACT_SET_NAME_TAG:
            actor->setNameTag(std::string{sarg});
            return true;
        case LEVI_RS_AACT_ADD_TAG:
            {
                bool ok = actor->addTag(std::string{sarg});
                if (out) out(ctx, ok ? "1" : "0");
                return true;
            }
        case LEVI_RS_AACT_REMOVE_TAG:
            {
                bool ok = actor->removeTag(std::string{sarg});
                if (out) out(ctx, ok ? "1" : "0");
                return true;
            }
        case LEVI_RS_AACT_HAS_TAG:
            {
                bool has = actor->hasTag(std::string{sarg});
                if (out) out(ctx, has ? "1" : "0");
                return true;
            }
        case LEVI_RS_AACT_ADD_EFFECT:
            {
                auto* effect = MobEffect::getByName(std::string{sarg});
                if (!effect) return false;
                MobEffectInstance inst{effect->getId()};
                inst.mDuration.get().mValue = static_cast<int>(a);
                inst.mAmplifier = static_cast<int>(b);
                inst.mEffectVisible = (c != 0.0);
                actor->addEffect(inst);
                return true;
            }
        case LEVI_RS_AACT_REMOVE_EFFECT:
            {
                auto* effect = MobEffect::getByName(std::string{sarg});
                if (!effect) return false;
                actor->removeEffect(static_cast<int>(effect->getId()));
                return true;
            }
        case LEVI_RS_AACT_CLEAR_EFFECTS:
            actor->removeAllEffects();
            return true;
        case LEVI_RS_AACT_HURT:
            {
                // Generic damage without a typed ActorDamageSource: route through
                // /damage so cause bookkeeping stays engine-side (decision #3).
                // Target by runtime id is impossible in vanilla commands; use the
                // engine hurt() with a default source instead when that lands. For
                // players we can fall back to /damage by name.
                if (actor->isPlayer())
                {
                    auto* p = static_cast<Player*>(actor);
                    return runConsoleCommand(
                        "damage \"" + p->getRealName() + "\" " + std::to_string(static_cast<int>(a))
                    );
                }
                return false; // non-player hurt: unsupported in v4.0 (needs ActorDamageSource plumbing)
            }
        case LEVI_RS_AACT_ATTRIBUTE_GET:
            return false; // reserved: generic attribute-by-name (post-v1.0.0)
        default:
            return false;
        }
    }

    bool api_spawn_mob(int32_t dim, LeviRsStr typeName, double x, double y, double z, LeviRsActorId* out)
    {
        auto* level = levelReady();
        auto* bs = blockSourceOf(dim);
        if (!level || !bs) return false;
        ActorDefinitionIdentifier ident{std::string{typeName}};
        auto* mob = level->getSpawner().spawnMob(
            *bs,
            ident,
            /*spawner*/ nullptr,
            Vec3{(float)x, (float)y, (float)z},
            /*naturalSpawn*/ false,
            /*surface*/ true,
            /*fromSpawner*/ false
        );
        if (!mob) return false;
        if (out) *out = mob->getOrCreateUniqueID().rawID;
        return true;
    }
} // namespace levi_rs::bridge
