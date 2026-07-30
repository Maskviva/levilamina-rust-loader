/**
 * bridge/Actors.cpp — actor enumeration, snapshots, properties, actions and
 * spawning (ABI v5 §C). Actor handles are ActorUniqueIDs, re-resolved via
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
        /* ── v5 additive: actor gap fill ── */
        case LEVI_RS_APROP_VIEW_X:
            *out = actor->getViewVector().x;
            return true;
        case LEVI_RS_APROP_VIEW_Y:
            *out = actor->getViewVector().y;
            return true;
        case LEVI_RS_APROP_VIEW_Z:
            *out = actor->getViewVector().z;
            return true;
        case LEVI_RS_APROP_VEL_X:
            *out = actor->getVelocity().x;
            return true;
        case LEVI_RS_APROP_VEL_Y:
            *out = actor->getVelocity().y;
            return true;
        case LEVI_RS_APROP_VEL_Z:
            *out = actor->getVelocity().z;
            return true;
        case LEVI_RS_APROP_HEAD_X:
            *out = actor->getHeadPos().x;
            return true;
        case LEVI_RS_APROP_HEAD_Y:
            *out = actor->getHeadPos().y;
            return true;
        case LEVI_RS_APROP_HEAD_Z:
            *out = actor->getHeadPos().z;
            return true;
        case LEVI_RS_APROP_FEET_X:
            *out = actor->getFeetPos().x;
            return true;
        case LEVI_RS_APROP_FEET_Y:
            *out = actor->getFeetPos().y;
            return true;
        case LEVI_RS_APROP_FEET_Z:
            *out = actor->getFeetPos().z;
            return true;
        case LEVI_RS_APROP_FALL_DISTANCE:
            *out = static_cast<double>(actor->getFallDistance());
            return true;
        case LEVI_RS_APROP_IS_PERSISTENT:
            *out = actor->isPersistent() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_APROP_IS_LEASHED:
            *out = actor->isLeashed() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_APROP_IS_INVULNERABLE:
            // Actor has only isInvulnerableTo(ActorDamageSource const&), no no-arg
            // form, and ActorFlags::Invulnerable doesn't exist. Report unsupported.
            return false;
        case LEVI_RS_APROP_VARIANT:
            *out = static_cast<double>(actor->getVariant());
            return true;
        case LEVI_RS_APROP_MARK_VARIANT:
            *out = static_cast<double>(actor->getMarkVariant());
            return true;
        case LEVI_RS_APROP_SCALE:
            // getScaleFactor(float) is behind #ifdef LL_PLAT_C — unavailable.
            return false;
        case LEVI_RS_APROP_BRIGHTNESS:
            *out = static_cast<double>(actor->getBrightness());
            return true;
        case LEVI_RS_APROP_RADIUS:
            *out = static_cast<double>(actor->getRadius());
            return true;
        case LEVI_RS_APROP_HAS_TOTEM:
            *out = actor->hasTotemEquipped() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_APROP_IS_IN_RAIN:
            *out = actor->isInRain() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_APROP_IS_IN_SNOW:
            *out = actor->isInSnow() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_APROP_IS_IN_THUNDERSTORM:
            *out = actor->isInThunderstorm() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_APROP_IS_FROZEN:
            // No isFrozen() on Actor; ActorFlags::Frozen doesn't exist either.
            // isImmobile() covers a broader set of immobility causes, so we
            // don't use it here to avoid false positives. Report unsupported.
            return false;
        case LEVI_RS_APROP_IS_IN_LOVE:
            *out = actor->isInLove() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_APROP_DEATH_TIME:
            *out = static_cast<double>(actor->getDeathTime());
            return true;
        case LEVI_RS_APROP_HAS_PASSENGER:
            *out = actor->hasPassenger() ? 1.0 : 0.0;
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
        /* ── v5 additive ── */
        case LEVI_RS_ASTR_SCORE_TAG:
            // getScoreTag() is behind #ifdef LL_PLAT_C — unavailable on server.
            // setScoreTag() exists, but the getter is client-only.
            return false;
        case LEVI_RS_ASTR_FILTERED_NAME:
            // getFilteredNameTag() is behind #ifdef LL_PLAT_C; read the public
            // mFilteredNameTag member (TypedStorage<string>) directly. Bind to
            // a std::string const& first so string_view construction sees a
            // real std::string (TypedStorage -> string -> string_view needs an
            // explicit hop; two implicit UDCs aren't allowed).
            {
                std::string const& name = actor->mFilteredNameTag;
                sink(ctx, name);
                return true;
            }
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
                return false; // non-player hurt: unsupported in v5.0 (needs ActorDamageSource plumbing)
            }
        case LEVI_RS_AACT_ATTRIBUTE_GET:
            return false; // reserved: generic attribute-by-name (post-v1.0.0)
        /* ── v5 additive ── */
        case LEVI_RS_AACT_SET_VARIANT:
            actor->setVariant(static_cast<int>(a));
            return true;
        case LEVI_RS_AACT_SET_MARK_VARIANT:
            actor->setMarkVariant(static_cast<int>(a));
            return true;
        case LEVI_RS_AACT_SET_PERSISTENT:
            actor->setPersistent();
            return true;
        case LEVI_RS_AACT_SET_LEASH_HOLDER:
            actor->setLeashHolder(ActorUniqueID{static_cast<int64_t>(a)});
            return true;
        case LEVI_RS_AACT_SET_INVISIBLE:
            actor->setInvisible(a != 0.0);
            return true;
        case LEVI_RS_AACT_SET_SNEAKING:
            actor->setSneaking(a != 0.0);
            return true;
        case LEVI_RS_AACT_SET_NAME_TAG_VISIBLE:
            actor->setNameTagVisible(a != 0.0);
            return true;
        case LEVI_RS_AACT_SET_TARGET:
            {
                auto* target = resolveActor(static_cast<LeviRsActorId>(a));
                if (!target) return false;
                actor->setTarget(target);
                return true;
            }
        case LEVI_RS_AACT_SET_OWNER:
            actor->setOwner(ActorUniqueID{static_cast<int64_t>(a)});
            return true;
        case LEVI_RS_AACT_BURN:
            // burn(int damage, bool inFire) — inFire=true means the source is
            // a fire block (vs. a flame enchant / lava tick).
            actor->burn(static_cast<int>(a), true);
            return true;
        case LEVI_RS_AACT_STOP_FIRE:
            // Actor has no extinguishFire(); stopFire() is the LL-exposed API.
            actor->stopFire();
            return true;
        case LEVI_RS_AACT_SET_VELOCITY:
            actor->setVelocity(Vec3{static_cast<float>(a), static_cast<float>(b), static_cast<float>(c)});
            return true;
        case LEVI_RS_AACT_APPLY_IMPULSE:
            actor->applyImpulse(Vec3{static_cast<float>(a), static_cast<float>(b), static_cast<float>(c)});
            return true;
        case LEVI_RS_AACT_SET_SCORE_TAG:
            actor->setScoreTag(std::string{sarg});
            return true;
        case LEVI_RS_AACT_SET_SKIN_ID:
            actor->setSkinID(static_cast<int>(a));
            return true;
        case LEVI_RS_AACT_SET_STRENGTH:
            actor->setStrength(static_cast<int>(a));
            return true;
        case LEVI_RS_AACT_REMOVE_ALL_PASSENGERS:
            // removeAllPassengers(bool actorIsBeingDestroyed, bool exitFromPassenger)
            actor->removeAllPassengers(false, true);
            return true;
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
