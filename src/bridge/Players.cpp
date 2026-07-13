/**
 * bridge/Players.cpp — player management, properties, actions (ABI v5 §B).
 *
 * Player handles are selectors (name / xuid / uuid), re-resolved against the
 * live player list on every call — never cached pointers. Version-sensitive
 * writes (gamemode, teleport, spawnpoint, title) go through vanilla commands.
 */
#include "bridge/Api.h"
#include "bridge/Common.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/AbilitiesIndex.h"
#include "mc/network/packet/TextPacket.h"
#include "mc/network/packet/TextPacketPayload.h"
#include "mc/network/packet/TextPacketType.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/Attribute.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/attribute/AttributeInstanceForwarder.h"
#include "mc/world/attribute/MutableAttributeWithContext.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"

namespace levi_rs::bridge
{
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
        char const* name;
        switch (mode)
        {
        case 0:
            name = "survival";
            break;
        case 1:
            name = "creative";
            break;
        case 2:
            name = "adventure";
            break;
        case 6:
            name = "spectator";
            break;
        default:
            return false;
        }
        return runConsoleCommand("gamemode " + std::string{name} + " \"" + p->getRealName() + "\"");
    }

    bool api_player_teleport(LeviRsPlayerSel sel, int32_t dim, double x, double y, double z)
    {
        Player* p = resolvePlayer(sel);
        if (!p) return false;
        if (dim < 0 || dim > 2) return false;
        std::string cmd = std::string("execute in ") + dimensionName(dim) + " run tp \"" + p->getRealName() + "\" "
            + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(z);
        return runConsoleCommand(cmd);
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
                p->setAbility(static_cast<AbilitiesIndex>(idx), b != 0.0);
                return true;
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
                        dim = std::clamp(std::stoi(dimStr), 0, 2);
                    }
                    catch (...)
                    {
                        return false;
                    }
                }
                return runConsoleCommand(
                    std::string("execute in ") + dimensionName(dim) + " run spawnpoint \"" + p->getRealName() + "\" "
                    + std::to_string(static_cast<int>(a)) + " " + std::to_string(static_cast<int>(b)) + " "
                    + std::to_string(static_cast<int>(c))
                );
            }
        case LEVI_RS_PACT_CLEAR_TITLE:
            return runConsoleCommand("title \"" + p->getRealName() + "\" clear");
        case LEVI_RS_PACT_SET_TITLE:
            {
                char const* slot = "title";
                int kind = static_cast<int>(a);
                if (kind == 1) slot = "subtitle";
                else if (kind == 2) slot = "actionbar";
                return runConsoleCommand(
                    "title \"" + p->getRealName() + "\" " + slot + " " + std::string{sarg}
                );
            }
        default:
            return false;
        }
    }
} // namespace levi_rs::bridge
