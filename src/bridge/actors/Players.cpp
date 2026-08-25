/**
 * bridge/Players.cpp — player management, properties, actions (ABI v5 §B).
 *
 * Player handles are selectors (name / xuid / uuid), re-resolved against the
 * live player list on every call — never cached pointers. Version-sensitive
 * writes (gamemode, teleport, spawnpoint, title) go through vanilla commands.
 */
#include "bridge/Api.h"
#include "bridge/Common.h"
#include "mc/network/packet/SetTitlePacket.h"
#include "mc/network/packet/SetTitlePacketPayload.h"
#include "ll/api/io/Logger.h"
#ifdef LEVI_RS_FEATURE_MORE_DIMENSIONS
#include "more_dimensions/include/base/NativeDimensions.h"
#include "mc/world/level/dimension/Dimension.h"
#endif


#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/network/NetworkPeer.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorHurtResult.h"
#include "mc/world/actor/player/AbilitiesIndex.h"
#include "mc/world/actor/player/Abilities.h"
#include "mc/world/actor/player/LayeredAbilities.h"
#include "mc/network/packet/UpdateAbilitiesPacket.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/packet/RemoveObjectivePacket.h"
#include "mc/network/packet/ScorePacketInfo.h"
#include "mc/network/packet/ScorePacketType.h"
#include "mc/network/packet/SetDisplayObjectivePacket.h"
#include "mc/network/packet/SetScorePacket.h"
#include "mc/network/packet/TextPacket.h"
#include "mc/world/scores/IdentityDefinition.h"
#include "mc/world/scores/ObjectiveSortOrder.h"
#include "mc/world/scores/ScoreboardId.h"
#include "mc/network/packet/TextPacketPayload.h"
#include "mc/network/packet/TextPacketType.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/Attribute.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/attribute/AttributeInstanceForwarder.h"
#include "mc/world/attribute/MutableAttributeWithContext.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/world/level/dimension/DimensionType.h"


namespace levi_rs::bridge
{
    namespace
    {
        /// Split on '\n'. Used by the sidebar opcodes, whose whole payload is
        /// one newline-joined string — one FFI string beats N calls, and the
        /// sidebar is rebuilt as a unit anyway.
        /**
         * objective 名 → 一段独占的 ScoreboardId 槽位号。
         *
         * 侧边栏的每一行是一个 FakePlayer 计分条目，键是 ScoreboardId。id 段
         * 原来对所有 objective 是同一个常量，于是两个插件的第 N 行是同一个条
         * 目，互相覆盖 —— 屏幕上两套内容穿插、右侧两组分数。
         *
         * 用 FNV-1a 把名字散到 [0, 2^18) 上，乘以 4096 行的间距。同名必定同段
         * （幂等，重复设置不会漂移），不同名几乎必定不同段。
         */
        uint32_t objectiveSlotHash(std::string_view name)
        {
            uint32_t h = 2166136261u;
            for (char c : name)
            {
                h ^= static_cast<unsigned char>(c);
                h *= 16777619u;
            }
            // 2^18 段 × 4096 行 = 2^30，正好填满 0x40000000 之上的一个象限。
            return h & 0x3FFFFu;
        }

        std::vector<std::string> splitLines(std::string_view text)
        {
            std::vector<std::string> out;
            while (true)
            {
                auto const nl = text.find('\n');
                if (nl == std::string_view::npos)
                {
                    out.emplace_back(text);
                    return out;
                }
                out.emplace_back(text.substr(0, nl));
                text.remove_prefix(nl + 1);
            }
        }
    } // namespace

    void api_list_players(void* ctx, LeviRsStrSink snbtSink)
    {
        auto* level = levelReady();
        if (!level || !snbtSink) return;
        level->forEachPlayer([&](Player& p)
        {
            snbtSink(ctx, playerSummarySnbt(p));
            return true;
        });
    }

    bool api_player_resolve(LeviRsPlayerSel sel, LeviRsActorId* out)
    {
        Player* p = resolvePlayer(sel);
        if (!p || !out) return false;
        *out = p->getOrCreateUniqueID().rawID;
        return true;
    }

    bool api_player_send_message(LeviRsPlayerSel sel, LeviRsStr msg)
    {
        Player* p = resolvePlayer(sel);
        if (!p) return false;
        p->sendMessage(std::string_view{msg});
        return true;
    }

    bool api_player_send_message_typed(LeviRsPlayerSel sel, LeviRsStr msg, int32_t type)
    {
        Player* p = resolvePlayer(sel);
        if (!p) return false;

        // Map the ABI int to TextPacketType; anything out of range falls back
        // to Raw (a plain client-side line) rather than being rejected.
        auto ptype = TextPacketType::Raw;
        if (type >= 0 && type <= 11) ptype = static_cast<TextPacketType>(static_cast<uchar>(type));

        // Build a TextPacket carrying a MessageOnly body — the shape
        // createRawMessage uses, but with the caller's type. This covers every
        // single-string kind (Tip, Popup, JukeboxPopup, SystemMessage,
        // Announcement, …). Author/param-bearing kinds (Chat/Whisper/Translate)
        // still arrive as a plain message here; that's the same simplification
        // LSE's tell(msg, type) makes.
        TextPacket pkt{};
        TextPacketPayload::MessageOnly body;
        body.mType = ptype;
        body.mMessage->assign(std::string_view{msg});
        pkt.mBody = body;

        p->sendNetworkPacket(pkt);
        return true;
    }

    bool api_player_disconnect(LeviRsPlayerSel sel, LeviRsStr reason)
    {
        Player* p = resolvePlayer(sel);
        if (!p) return false;
        p->disconnect(std::string_view{reason});
        return true;
    }

    void api_broadcast_message(LeviRsStr msg)
    {
        auto* level = levelReady();
        if (!level) return;
        std::string_view text{msg};
        level->forEachPlayer([&](Player& p)
        {
            p.sendMessage(text);
            return true;
        });
    }

    bool api_player_set_gamemode(LeviRsPlayerSel sel, int32_t mode)
    {
        Player* p = resolvePlayer(sel);
        if (!p) return false;
        // mode 用的就是引擎的 GameType 判别值（0=生存 1=创造 2=冒险 6=旁观），
        // 这里只做白名单校验，不再翻译成命令里的名字。
        //
        // 顺带修掉命令路径藏着的一个坑：玩家名原来直接拼进带引号的命令，名字
        // 里有引号或反斜杠就能把命令撕开。
        switch (mode)
        {
        case 0:
        case 1:
        case 2:
        case 6:
            break;
        default:
            return false;
        }
        p->setPlayerGameType(static_cast<::GameType>(mode));
        return true;
    }

    /**
     * Teleport a player, across dimensions if needed.
     *
     * Two things were wrong with the previous implementation:
     *
     *  1. `if (dim < 0 || dim > 2) return false;` rejected every custom
     *     dimension (MoreDimensions ids start at 3), so entering a custom
     *     dimension through this API was simply impossible. Note the *actor*
     *     teleport path in Actors.cpp never had that restriction — the player
     *     one was just an oversight.
     *
     *  2. It shelled out to `/execute in <name> run tp`. The `execute in`
     *     subcommand takes a *command enum* of dimensions built from the
     *     vanilla set, so a custom dimension name isn't necessarily a valid
     *     token there — the command could fail to parse, or parse into the
     *     wrong dimension, even after the name was registered.
     *
     * Actor::teleport is LeviLamina's own cross-dimension helper (already used
     * by api_actor_action), so it goes through the engine's own dimension-change
     * machinery. With navdim the client knows the custom dimension for real
     * (it is described to the client by DimensionDataPacket), so the vanilla
     * path is all that is needed — there is no packet-rewriting layer left to
     * go through. The command path bypassed the engine machinery entirely.
     */
    bool api_player_teleport(LeviRsPlayerSel sel, int32_t dim, double x, double y, double z)
    {
        Player* p = resolvePlayer(sel);
        if (!p) return false;

        auto const name = dimensionSelector(dim);
        if (name.empty()) return false;

#ifdef LEVI_RS_FEATURE_MORE_DIMENSIONS
        if (dim >= 3)
        {
            // blockSourceOf() 只保证"有个维度对象",不保证它的 id 就是 dim。
            // 两者不一致时把玩家送进 dim,引擎会在区块工作线程上抛出未捕获异常,
            // 整个进程 fastfail(0xC0000409) —— 不是一句"传送失败"能兜住的。
            auto* real = ::more_dimensions::native::getOrCreateByName(name);
            if (!real) return false;
            if (real->getDimensionId().value() != dim)
            {
                bridgeLogger().error(
                    "拒绝传送：维度 '{}' 台账 id {}，引擎实例 id {}",
                    name, dim, real->getDimensionId().value());
                return false;
            }
        }
#endif

        if (!blockSourceOf(dim)) return false;

        p->teleport(Vec3{(float)x, (float)y, (float)z}, DimensionType{dim}, p->getRotation());
        return true;
    }

    // ───────────────────────── attributes helper ─────────────────────────

    namespace
    {
        /** Read an attribute's current value; NaN-free: ok=false when missing. */
        bool readAttribute(Player& p, Attribute const& attr, double* out)
        {
            auto cref = p.getAttribute(attr);
            // mPtr is a scalar TypedStorage (a raw pointer), so no .get() wrapper.
            auto* inst = cref.mPtr;
            if (!inst) return false;
            *out = static_cast<double>(inst->getCurrentValue());
            return true;
        }

        /**
         * Write an attribute's current value through AttributeInstanceForwarder so
         * listeners fire and player-synced attributes reach the client.
         */
        bool writeAttribute(Player& p, Attribute const& attr, float value)
        {
            // getMutableAttribute bundles the instance + modification context and
            // exposes the forwarder via operator->; its bool test guards absence.
            auto mut = p.getMutableAttribute(attr);
            if (!mut) return false;
            mut->setCurrentValue(value);
            return true;
        }
    } // namespace

    // ───────────────────────── properties ─────────────────────────

    bool api_player_get_num(LeviRsPlayerSel sel, int32_t prop, double* out)
    {
        Player* p = resolvePlayer(sel);
        if (!p || !out) return false;
        switch (prop)
        {
        case LEVI_RS_PPROP_GAME_TYPE:
            *out = static_cast<double>(static_cast<int>(p->getPlayerGameType()));
            return true;
        case LEVI_RS_PPROP_LEVEL:
            return readAttribute(*p, Player::LEVEL(), out);
        case LEVI_RS_PPROP_EXPERIENCE:
            return readAttribute(*p, Player::EXPERIENCE(), out);
        case LEVI_RS_PPROP_HUNGER:
            return readAttribute(*p, Player::HUNGER(), out);
        case LEVI_RS_PPROP_SATURATION:
            return readAttribute(*p, Player::SATURATION(), out);
        case LEVI_RS_PPROP_EXHAUSTION:
            return readAttribute(*p, Player::EXHAUSTION(), out);
        case LEVI_RS_PPROP_XP_NEEDED_NEXT_LEVEL:
            *out = static_cast<double>(p->getXpNeededForNextLevel());
            return true;
        case LEVI_RS_PPROP_LUCK:
            *out = static_cast<double>(p->getLuck());
            return true;
        case LEVI_RS_PPROP_SELECTED_SLOT:
            *out = static_cast<double>(p->getSelectedItemSlot());
            return true;
        case LEVI_RS_PPROP_IS_OPERATOR:
            *out = p->isOperator() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_PPROP_CAN_USE_OPERATOR_BLOCKS:
            *out = p->canUseOperatorBlocks() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_PPROP_IS_FLYING:
            *out = p->isFlying() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_PPROP_CAN_JUMP:
            *out = p->canJump() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_PPROP_IS_EMOTING:
            *out = p->isEmoting() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_PPROP_IS_IN_RAID:
            *out = p->isInRaid() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_PPROP_IS_HURT:
            *out = p->isHurt() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_PPROP_IS_SCOPING:
            // Player::isScoping() is declared inside #ifdef LL_PLAT_C in the
            // generated headers, so it isn't available in a normal build. Report
            // "unsupported" for this one property instead of failing to compile.
            return false;
        case LEVI_RS_PPROP_CAN_SLEEP:
            *out = p->canSleep() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_PPROP_HAS_RESPAWN_POSITION:
            *out = p->hasRespawnPosition() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_PPROP_CLIENT_SUB_ID:
            *out = static_cast<double>(static_cast<int>(p->getClientSubId()));
            return true;
        /* ── v5 additive: player gap fill ── */
        case LEVI_RS_PPROP_DIRECTION:
            *out = static_cast<double>(p->getDirection());
            return true;
        case LEVI_RS_PPROP_CHUNK_RADIUS:
            *out = static_cast<double>(p->getChunkRadius());
            return true;
        case LEVI_RS_PPROP_NETWORK_RTT:
            {
                // getNetworkStatus() returns std::optional<NetworkPeer::NetworkStatus>;
                // mCurrentPing is a wrapped chrono::milliseconds, so use ->count().
                auto opt = p->getNetworkStatus();
                if (!opt) return false;
                *out = static_cast<double>(opt->mCurrentPing->count());
                return true;
            }
        case LEVI_RS_PPROP_PLATFORM:
            *out = static_cast<double>(static_cast<int>(p->getPlatform()));
            return true;
        case LEVI_RS_PPROP_ENCHANTMENT_SEED:
            *out = static_cast<double>(p->getEnchantmentSeed());
            return true;
        case LEVI_RS_PPROP_IS_USING_ITEM:
            *out = p->isUsingItem() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_PPROP_IS_BLOCKING:
            *out = p->isBlocking() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_PPROP_IS_GLIDING:
            *out = p->isGliding() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_PPROP_IS_SWIMMING:
            *out = p->isSwimming() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_PPROP_PERMISSION_LEVEL:
            *out = static_cast<double>(static_cast<int>(p->getPlayerPermissionLevel()));
            return true;
        case LEVI_RS_PPROP_SCORE:
            // Player has no getScore(); the value lives in the public mScore
            // member (TypedStorage<int> collapses to a raw int on access).
            *out = static_cast<double>(p->mScore);
            return true;
        case LEVI_RS_PPROP_FALL_DISTANCE:
            *out = static_cast<double>(p->getFallDistance());
            return true;
        case LEVI_RS_PPROP_IS_DEAD:
            *out = p->isDead() ? 1.0 : 0.0;
            return true;
        case LEVI_RS_PPROP_HAS_DIED_BEFORE:
            *out = p->hasDiedBefore() ? 1.0 : 0.0;
            return true;
        // 玩家当前所在维度。自定义维度的 id >= 3，所以调用方不能假设只有 0/1/2。
        // 写法照抄 Actors.cpp 里已有的那一处，Player 继承自 Actor。
        case LEVI_RS_PPROP_DIMENSION:
            *out = static_cast<double>(static_cast<int>(p->getDimensionId()));
            return true;
        default:
            return false;
        }
    }

    bool api_player_get_str(LeviRsPlayerSel sel, int32_t prop, void* ctx, LeviRsStrSink sink)
    {
        Player* p = resolvePlayer(sel);
        if (!p || !sink) return false;
        switch (prop)
        {
        case LEVI_RS_PSTR_REAL_NAME:
            sink(ctx, p->getRealName());
            return true;
        case LEVI_RS_PSTR_UUID:
            sink(ctx, p->getUuid().asString());
            return true;
        case LEVI_RS_PSTR_XUID:
            sink(ctx, p->getXuid());
            return true;
        case LEVI_RS_PSTR_IP_AND_PORT:
            sink(ctx, p->getIPAndPort());
            return true;
        case LEVI_RS_PSTR_LOCALE_CODE:
            sink(ctx, p->getLocaleCode());
            return true;
        case LEVI_RS_PSTR_NAME_TAG:
            sink(ctx, p->getNameTag());
            return true;
        /* ── v5 additive ── */
        case LEVI_RS_PSTR_LAST_DEATH_POS:
            {
                auto pos = p->getLastDeathPos();
                if (!pos.has_value())
                {
                    sink(ctx, "");
                    return true;
                }
                std::string snbt = "{x:" + snbtNum(pos->x) + ",y:" + snbtNum(pos->y)
                    + ",z:" + snbtNum(pos->z) + "}";
                sink(ctx, snbt);
                return true;
            }
        case LEVI_RS_PSTR_LAST_DEATH_DIMENSION:
            {
                auto dim = p->getLastDeathDimension();
                if (!dim.has_value())
                {
                    sink(ctx, "");
                    return true;
                }
                sink(ctx, snbtNum(static_cast<int>(*dim)));
                return true;
            }
        case LEVI_RS_PSTR_NETWORK_STATUS:
            {
                // NetworkStatus fields: mCurrentPing/mAveragePing are wrapped
                // chrono::milliseconds (use ->count()); the packet-loss fields
                // are plain float (use directly). Returns optional, so check.
                auto opt = p->getNetworkStatus();
                if (!opt) return false;
                auto const& ns = *opt;
                std::string snbt = "{ping:" + snbtNum(ns.mCurrentPing->count());
                snbt += ",avg_ping:" + snbtNum(ns.mAveragePing->count());
                snbt += ",packet_loss:" + snbtNum(ns.mCurrentPacketLoss);
                snbt += ",avg_packet_loss:" + snbtNum(ns.mAveragePacketLoss);
                snbt += ",max_bps:" + snbtNum(ns.mApproximateMaxBps) + "}";
                sink(ctx, snbt);
                return true;
            }
        case LEVI_RS_PSTR_PLATFORM_ONLINE_ID:
            sink(ctx, p->getPlatformOnlineId());
            return true;
        default:
            return false;
        }
    }

    bool api_player_set_num(LeviRsPlayerSel sel, int32_t prop, double v)
    {
        Player* p = resolvePlayer(sel);
        if (!p) return false;
        switch (prop)
        {
        case LEVI_RS_PPROP_LEVEL:
            return writeAttribute(*p, Player::LEVEL(), static_cast<float>(v));
        case LEVI_RS_PPROP_EXPERIENCE:
            return writeAttribute(*p, Player::EXPERIENCE(), static_cast<float>(v));
        case LEVI_RS_PPROP_HUNGER:
            return writeAttribute(*p, Player::HUNGER(), static_cast<float>(v));
        case LEVI_RS_PPROP_SATURATION:
            return writeAttribute(*p, Player::SATURATION(), static_cast<float>(v));
        case LEVI_RS_PPROP_EXHAUSTION:
            return writeAttribute(*p, Player::EXHAUSTION(), static_cast<float>(v));
        default:
            return false; // get-only or unknown
        }
    }

    // ───────────────────────── actions ─────────────────────────

    namespace
    {
        /**
         * Set one ability slot, dispatching bool vs float correctly.
         *
         * Three separate bugs met here, which is why this is a helper rather
         * than a one-line change:
         *
         *  1. The old dispatch used `idx < 32` to decide bool vs float. That
         *     is simply wrong — AbilitiesIndex only runs 0..19, and the float
         *     slots sit at 13 (FlySpeed), 14 (WalkSpeed) and 19
         *     (VerticalFlySpeed). Every ability therefore took the bool path.
         *
         *  2. `Player::setAbility` has ONLY a bool overload (Player.h:286), so
         *     even the "float" branch resolved to it via an implicit
         *     float→bool conversion: any non-zero speed silently became
         *     `true`. Float abilities have never worked. The float path has to
         *     go through LayeredAbilities::setAbility(idx, float)
         *     (LayeredAbilities.h:25), which does have both overloads.
         *
         *  3. Writing the layer server-side does not tell the client. Movement
         *     and flight speed are applied client-side, so without an
         *     UpdateAbilitiesPacket the player keeps moving at the old speed.
         *
         * The bool path deliberately still goes through Player::setAbility:
         * that is LeviLamina's own helper, it already syncs, and boolean
         * abilities are the ones currently working. No reason to disturb them.
         */
        bool setPlayerAbility(Player& p, int idx, double value)
        {
            if (idx < 0 || idx >= static_cast<int>(AbilitiesIndex::AbilityCount))
            {
                return false;
            }
            auto index = static_cast<AbilitiesIndex>(idx);

            switch (index)
            {
            case AbilitiesIndex::FlySpeed:
            case AbilitiesIndex::WalkSpeed:
            case AbilitiesIndex::VerticalFlySpeed:
                {
                    if (!p.getAbilities().setAbility(index, static_cast<float>(value)))
                    {
                        return false;
                    }
                    // Push the whole layered set to the client; speed is
                    // applied client-side and will not change without this.
                    UpdateAbilitiesPacket pkt{p.getOrCreateUniqueID(), p.getAbilities()};
                    p.sendNetworkPacket(pkt);
                    return true;
                }
            default:
                p.setAbility(index, value != 0.0);
                return true;
            }
        }
    } // namespace

    bool api_player_action(
        LeviRsPlayerSel sel,
        int32_t action,
        LeviRsStr sarg,
        double a,
        double b,
        double c,
        void* ctx,
        LeviRsStrSink out
    )
    {
        Player* p = resolvePlayer(sel);
        if (!p) return false;
        switch (action)
        {
        case LEVI_RS_PACT_SET_ABILITY:
            {
                int idx = static_cast<int>(a);
                return setPlayerAbility(*p, idx, b);
            }
        case LEVI_RS_PACT_CAN_USE_ABILITY:
            {
                int idx = static_cast<int>(a);
                bool can = p->canUseAbility(static_cast<AbilitiesIndex>(idx));
                if (out) out(ctx, can ? "1" : "0");
                return true;
            }
        case LEVI_RS_PACT_SET_SELECTED_SLOT:
            {
                int slot = static_cast<int>(a);
                if (slot < 0 || slot > 8) return false;
                p->setSelectedSlot(slot);
                return true;
            }
        case LEVI_RS_PACT_GIVE_ITEM:
            {
                auto opt = itemFromSnbt(std::string_view{sarg});
                if (!opt) return false;
                ItemStack item = std::move(*opt);
                if (item.isNull()) return false;
                return p->addAndRefresh(item);
            }
        case LEVI_RS_PACT_SET_SPAWN_POINT:
            {
                std::string dimStr{sarg};
                int dim = 0;
                if (!dimStr.empty())
                {
                    try
                    {
                        // Was clamped to 0..2, which silently moved a spawn
                        // point meant for a custom dimension into the end.
                        dim = std::stoi(dimStr);
                    }
                    catch (...)
                    {
                        return false;
                    }
                }
                // 原生。同上，不再把玩家名拼进命令字符串。
                p->setRespawnPosition(
                    BlockPos{static_cast<int>(a), static_cast<int>(b), static_cast<int>(c)},
                    static_cast<::DimensionType>(dim)
                );
                return true;
            }
        case LEVI_RS_PACT_CLEAR_TITLE:
            {
                // 原生数据包。命令路径要把玩家名拼进带引号的字符串里，名字含
                // 引号就撕开命令；而且 /title 会走一遍命令解析和权限检查，对一
                // 个「给这个玩家发个包」的动作来说全是白付的。
                SetTitlePacketPayload payload{SetTitlePacketPayload::TitleType::Clear};
                SetTitlePacket{std::move(payload)}.sendTo(*p);
                return true;
            }
        case LEVI_RS_PACT_SET_TITLE:
            {
                auto kind = static_cast<int>(a);
                auto type = kind == 1   ? SetTitlePacketPayload::TitleType::Subtitle
                          : kind == 2   ? SetTitlePacketPayload::TitleType::Actionbar
                                        : SetTitlePacketPayload::TitleType::Title;
                // filteredTitleText 传 nullopt：那是给聊天过滤用的备用文本，
                // 这条链路上的文本来自 mod 而不是玩家输入，没有可过滤的东西。
                SetTitlePacketPayload payload{type, std::string{sarg}, std::nullopt};
                SetTitlePacket{std::move(payload)}.sendTo(*p);
                return true;
            }
        /* ── v5 additive ── */
        case LEVI_RS_PACT_ADD_EXPERIENCE:
            p->addExperience(static_cast<int>(a));
            return true;
        case LEVI_RS_PACT_ADD_LEVELS:
            p->addLevels(static_cast<int>(a));
            return true;
        case LEVI_RS_PACT_START_COOLDOWN:
            // startItemCooldown(HashedString const&, int ticks, bool updateClient)
            p->startItemCooldown(HashedString{std::string{sarg}}, static_cast<int>(a), true);
            return true;
        case LEVI_RS_PACT_START_RIDING:
            {
                auto* vehicle = resolveActor(static_cast<LeviRsActorId>(a));
                if (!vehicle) return false;
                // startRiding(Actor&, bool forceRiding) — force=true so the
                // request succeeds even if the vehicle is full.
                return p->startRiding(*vehicle, true);
            }
        case LEVI_RS_PACT_STOP_RIDING:
            // stopRiding(bool exitFromPassenger, bool actorIsBeingDestroyed,
            //            bool switchingVehicles, bool isBeingTeleported)
            p->stopRiding(true, false, false, false);
            return true;
        case LEVI_RS_PACT_ATTACK:
            {
                auto* target = resolveActor(static_cast<LeviRsActorId>(a));
                if (!target) return false;
                // attack(Actor&, ActorDamageCause const&) — use Override (the
                // generic cause) since the caller didn't specify one.
                p->attack(*target, ::SharedTypes::Legacy::ActorDamageCause::Override);
                return true;
            }
        case LEVI_RS_PACT_DROP:
            {
                auto opt = itemFromSnbt(std::string_view{sarg});
                if (!opt) return false;
                return p->drop(std::move(*opt), a != 0.0);
            }
        case LEVI_RS_PACT_INTERACT:
            {
                auto* target = resolveActor(static_cast<LeviRsActorId>(a));
                if (!target) return false;
                // interact(Actor&, Vec3 const& location) returns InteractionResult;
                // surface its mSuccess bit as the bool return.
                auto result = p->interact(*target, target->getPosition());
                return result.mSuccess;
            }
        case LEVI_RS_PACT_START_USING_ITEM:
            {
                auto opt = itemFromSnbt(std::string_view{sarg});
                if (!opt) return false;
                p->startUsingItem(std::move(*opt), static_cast<int>(a));
                return true;
            }
        case LEVI_RS_PACT_STOP_USING_ITEM:
            p->stopUsingItem();
            return true;
        case LEVI_RS_PACT_SET_CHUNK_RADIUS:
            p->setChunkRadius(static_cast<int>(a));
            return true;
        case LEVI_RS_PACT_SET_ENCHANTMENT_SEED:
            p->setEnchantmentSeed(static_cast<int>(a));
            return true;
        case LEVI_RS_PACT_REGISTER_TRACKED_BOSS:
            {
                auto* boss = resolveActor(static_cast<LeviRsActorId>(a));
                if (!boss) return false;
                // registerTrackedBoss takes an ActorUniqueID, not an Actor ref.
                p->registerTrackedBoss(boss->getOrCreateUniqueID());
                return true;
            }
        case LEVI_RS_PACT_UNREGISTER_TRACKED_BOSS:
            {
                auto* boss = resolveActor(static_cast<LeviRsActorId>(a));
                if (!boss) return false;
                p->unRegisterTrackedBoss(boss->getOrCreateUniqueID());
                return true;
            }
        case LEVI_RS_PACT_PLAY_EMOTE:
            // playEmote(string const& pieceId, bool playChatMessage)
            p->playEmote(std::string{sarg}, false);
            return true;
        case LEVI_RS_PACT_RESEND_ALL_CHUNKS:
            p->resendAllChunks();
            return true;
        case LEVI_RS_PACT_OPEN_INVENTORY:
            p->openInventory();
            return true;
        /* ── v5 additive (2026-08-19) ── */
        case LEVI_RS_PACT_SIDEBAR_SET:
            {
                // sarg = "objective\ntitle\nline1\nline2…". One player's
                // sidebar only — the server-side Scoreboard is global, so a
                // per-player board can only be a packet the client never
                // correlates with real scoreboard state.
                //
                // The packets are built here rather than handed over the FFI
                // as bytes: SetDisplayObjective/RemoveObjective are
                // PayloadPackets now (reflection-serialised), so their wire
                // shape is not something a mod can hand-roll and keep working
                // across versions. Same reason api_player_send_title exists.
                auto const lines = splitLines(sarg);
                if (lines.size() < 2)
                {
                    bridgeLogger().error("sidebar: payload 少于两行（objective + title）");
                    return false;
                }
                std::string const& objective = lines[0];
                if (objective.empty())
                {
                    bridgeLogger().error("sidebar: objective 是空的");
                    return false;
                }


                // Rebuild from scratch every time: the client keys entries by
                // scoreboard id, and reusing ids across a changed line set is
                // exactly where stale rows come from.
                if (auto gone = MinecraftPackets::createPacket(MinecraftPacketIds::RemoveObjective))
                {
                    static_cast<RemoveObjectivePacket*>(gone.get())->mObjectiveName = objective;
                    p->sendNetworkPacket(*gone);
                }
                else
                {
                    bridgeLogger().error("sidebar: createPacket(RemoveObjective) 返回空");
                }

                auto shown = MinecraftPackets::createPacket(MinecraftPacketIds::SetDisplayObjective);
                if (!shown)
                {
                    bridgeLogger().error("sidebar: createPacket(SetDisplayObjective) 返回空");
                    return false;
                }
                {
                    auto* d                  = static_cast<SetDisplayObjectivePacket*>(shown.get());
                    d->mDisplaySlotName      = std::string{"sidebar"};
                    d->mObjectiveName        = objective;
                    d->mObjectiveDisplayName = lines[1];
                    d->mCriteriaName         = std::string{"dummy"};
                    d->mSortOrder            = ObjectiveSortOrder::Descending;
                    p->sendNetworkPacket(*shown);
                }

                if (lines.size() == 2) return true;

                // ScoreboardId 段按 objective 名分开。
                //
                // 这里原来是一个写死的常量 0x40000000，**所有插件共用**。两个
                // 插件同时开侧边栏时，各自的第 1 行都落在 0x40000001 —— 那是
                // 同一个 scoreboard 条目，谁后发谁覆盖。屏幕上就是两套内容穿插
                // 在一起、右侧出现两组分数，而且谁都清不掉对方的。
                //
                // 按名字哈希出各自的段位。段间距 4096 行，远超 MAX_ROWS，所以
                // 不会有实际重叠；哈希冲突的概率是 1/(2^30/4096)，而且真撞上也
                // 只影响同时开两个侧边栏的场景 —— 比现在这个必然冲突好得多。
                //
                // 高位固定 0x4 是为了避开原版计分板真实用到的低位 id 段。
                int64_t const kSidebarIdBase = INT64_C(0x40000000)
                    + (static_cast<int64_t>(objectiveSlotHash(objective)) * INT64_C(4096));
                std::vector<ScorePacketInfo> infos;
                infos.reserve(lines.size() - 2);
                int score = static_cast<int>(lines.size()) - 2;
                for (size_t i = 2; i < lines.size(); ++i, --score)
                {
                    ScorePacketInfo info{};
                    info.mScoreboardId->mRawID = kSidebarIdBase + static_cast<int64_t>(i - 1);
                    info.mObjectiveName        = objective;
                    info.mScoreValue           = score;
                    info.mIdentityType         = IdentityDefinition::Type::FakePlayer;
                    info.mFakePlayerName       = lines[i].empty() ? std::string{" "} : lines[i];
                    infos.push_back(std::move(info));
                }

                auto scores = MinecraftPackets::createPacket(MinecraftPacketIds::SetScore);
                if (!scores)
                {
                    bridgeLogger().error("sidebar: createPacket(SetScore) 返回空");
                    return false;
                }
                auto* sp        = static_cast<SetScorePacket*>(scores.get());
                auto const rows = infos.size();
                sp->mType       = ScorePacketType::Change;
                sp->mScoreInfo  = std::move(infos);
                p->sendNetworkPacket(*scores);

                // 每个 objective 打一次到达证明，不是全局一次 —— 全局一次的话
                // 第二个插件的侧边栏有没有真的发出去，日志里根本看不出来。
                // 到达证明：每个 objective 打一次。
                //
                // 原来是全局一次（static bool），于是第二个插件的侧边栏有没有真的
                // 发出去、用的是哪一段 id，日志里完全看不出来 —— 而那正是两个侧
                // 边栏互相覆盖时唯一需要知道的事。
                static std::set<std::string> announcedObjectives;
                if (announcedObjectives.insert(objective).second)
                {
                    bridgeLogger().info(
                        "sidebar: '{}' 已发出 {} 行（FakePlayer，id 段 0x{:x}..0x{:x}）",
                        objective, rows,
                        static_cast<uint64_t>(kSidebarIdBase + 1),
                        static_cast<uint64_t>(kSidebarIdBase + static_cast<int64_t>(rows)));
                }
                return true;
            }
        case LEVI_RS_PACT_SIDEBAR_CLEAR:
            {
                if (sarg.empty()) return false;

                // **先解绑显示槽，再删 objective。** 顺序反了等于没清。
                //
                // SIDEBAR_SET 是三步：RemoveObjective → 建 objective →
                // SetDisplayObjective 把它挂到 "sidebar" 槽。而这里原来只做了
                // 删 objective 这一步 —— 客户端会删掉计分项，但**槽位仍然绑在
                // 这个名字上**，屏幕上的旧内容不会消失。
                //
                // 更麻烦的是它把槽位占着不放：另一个插件随后调 SIDEBAR_SET，
                // 它发的 RemoveObjective 移除的是**自己的**名字，动不了这条陈
                // 旧绑定，于是它的侧边栏也显示不出来。两个插件轮流用一个槽位时
                // （起床战争维度进出）表现就是「出来之后卡在旧内容，别的插件也
                // 抢不回来」。
                //
                // SetDisplayObjective 带空的 mObjectiveName 就是「这个槽不显示
                // 任何东西」—— 这是原版 `/scoreboard objectives setdisplay
                // sidebar`（不带目标名）走的同一条线。
                if (auto blank =
                        MinecraftPackets::createPacket(MinecraftPacketIds::SetDisplayObjective))
                {
                    auto* d                  = static_cast<SetDisplayObjectivePacket*>(blank.get());
                    d->mDisplaySlotName      = std::string{"sidebar"};
                    d->mObjectiveName        = std::string{};
                    d->mObjectiveDisplayName = std::string{};
                    d->mCriteriaName         = std::string{"dummy"};
                    d->mSortOrder            = ObjectiveSortOrder::Descending;
                    p->sendNetworkPacket(*d);
                }

                auto gone = MinecraftPackets::createPacket(MinecraftPacketIds::RemoveObjective);
                if (!gone) return false;
                static_cast<RemoveObjectivePacket*>(gone.get())->mObjectiveName = std::string{sarg};
                p->sendNetworkPacket(*gone);

                bridgeLogger().debug("sidebar: 已清除 '{}'（解绑槽位 + 删 objective）", sarg);
                return true;
            }

        default:
            return false;
        }
    }
} // namespace levi_rs::bridge
