/**
 * bridge/Items.cpp — item value objects (ABI v5 §E).
 *
 * Items cross the boundary as ItemStack::save SNBT. Every call rebuilds a
 * transient ItemStack (ItemStack::fromTag), queries or mutates it, and —
 * for transforms — serializes it right back. Zero cross-boundary ownership.
 */
#include "bridge/Api.h"
#include "bridge/Common.h"

#include <string>
#include <vector>

#include "mc/deps/core/math/Color.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/safety/RedactableString.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/block/BlockType.h"

namespace levi_rs::bridge
{
    bool api_item_get_num(LeviRsStr itemSnbt, int32_t prop, double* out)
    {
        LEVI_RS_API_GUARD_BEGIN
            auto item = itemFromSnbt(std::string_view{itemSnbt});
            if (!item || !out) return false;
            switch (prop)
            {
            case LEVI_RS_IPROP_COUNT:
                *out = static_cast<double>(item->mCount);
                return true;
            case LEVI_RS_IPROP_MAX_STACK_SIZE:
                *out = static_cast<double>(item->getMaxStackSize());
                return true;
            case LEVI_RS_IPROP_AUX_VALUE:
                *out = static_cast<double>(item->getAuxValue());
                return true;
            case LEVI_RS_IPROP_ID:
                *out = static_cast<double>(item->getId());
                return true;
            case LEVI_RS_IPROP_DAMAGE:
                *out = static_cast<double>(item->getDamageValue());
                return true;
            case LEVI_RS_IPROP_IS_NULL:
                *out = item->isNull() ? 1.0 : 0.0;
                return true;
            case LEVI_RS_IPROP_IS_BLOCK:
                *out = item->isBlock() ? 1.0 : 0.0;
                return true;
            case LEVI_RS_IPROP_IS_ENCHANTED:
                *out = item->isEnchanted() ? 1.0 : 0.0;
                return true;
            case LEVI_RS_IPROP_IS_ARMOR:
                *out = item->isArmorItem() ? 1.0 : 0.0;
                return true;
            case LEVI_RS_IPROP_IS_DAMAGEABLE:
                *out = item->isDamageableItem() ? 1.0 : 0.0;
                return true;
            case LEVI_RS_IPROP_IS_DAMAGED:
                *out = item->isDamaged() ? 1.0 : 0.0;
                return true;
            /* ── v5 additive: item gap fill ── */
            case LEVI_RS_IPROP_MAX_DAMAGE:
                *out = static_cast<double>(item->getMaxDamage());
                return true;
            case LEVI_RS_IPROP_IS_UNBREAKABLE:
                *out = item->isUnbreakable() ? 1.0 : 0.0;
                return true;
            case LEVI_RS_IPROP_HAS_DURABILITY:
                *out = item->hasDurability() ? 1.0 : 0.0;
                return true;
            case LEVI_RS_IPROP_IS_POTION:
                *out = item->isPotionItem() ? 1.0 : 0.0;
                return true;
            case LEVI_RS_IPROP_IS_THROWABLE:
                // isThrowable() is behind #ifdef LL_PLAT_C — unavailable on server.
                return false;
            case LEVI_RS_IPROP_IS_FIRE_RESISTANT:
                *out = item->isFireResistant() ? 1.0 : 0.0;
                return true;
            case LEVI_RS_IPROP_ATTACK_DAMAGE:
                *out = static_cast<double>(item->getAttackDamage());
                return true;
            case LEVI_RS_IPROP_REPAIR_COST:
                *out = static_cast<double>(item->getBaseRepairCost());
                return true;
            case LEVI_RS_IPROP_ENCHANT_VALUE:
                *out = static_cast<double>(item->getEnchantValue());
                return true;
            case LEVI_RS_IPROP_IS_STACKABLE:
                *out = item->isStackable() ? 1.0 : 0.0;
                return true;
            case LEVI_RS_IPROP_IS_MUSIC_DISC:
                *out = item->isMusicDiscItem() ? 1.0 : 0.0;
                return true;
            case LEVI_RS_IPROP_IS_OFFHAND:
                *out = item->isOffhandItem() ? 1.0 : 0.0;
                return true;
            case LEVI_RS_IPROP_USE_DURATION:
                *out = static_cast<double>(item->getMaxUseDuration());
                return true;
            case LEVI_RS_IPROP_IS_GLINT:
                *out = item->isGlint() ? 1.0 : 0.0;
                return true;
            case LEVI_RS_IPROP_IS_BUNDLE:
                // isBundle() is behind #ifdef LL_PLAT_C — unavailable on server.
                return false;
            case LEVI_RS_IPROP_HAS_USER_DATA:
                *out = item->hasUserData() ? 1.0 : 0.0;
                return true;
            case LEVI_RS_IPROP_HAS_CUSTOM_NAME:
                *out = item->hasCustomHoverName() ? 1.0 : 0.0;
                return true;
            default:
                return false;
            }
        LEVI_RS_API_GUARD_END
    }

    bool api_item_get_str(LeviRsStr itemSnbt, int32_t prop, void* ctx, LeviRsStrSink sink)
    {
        LEVI_RS_API_GUARD_BEGIN
            auto item = itemFromSnbt(std::string_view{itemSnbt});
            if (!item || !sink) return false;
            switch (prop)
            {
            case LEVI_RS_ISTR_TYPE_NAME:
                sink(ctx, item->getTypeName());
                return true;
            case LEVI_RS_ISTR_NAME:
                sink(ctx, item->getName());
                return true;
            case LEVI_RS_ISTR_CUSTOM_NAME:
                sink(ctx, item->getCustomName());
                return true;
            case LEVI_RS_ISTR_RAW_NAME_ID:
                sink(ctx, item->getRawNameId());
                return true;
            /* ── v5 additive ── */
            case LEVI_RS_ISTR_LORE:
                {
                    auto const& lore = item->getCustomLore();
                    std::string out = "[";
                    for (size_t i = 0; i < lore.size(); ++i)
                    {
                        if (i > 0) out += ",";
                        out += "\"" + snbtEscape(lore[i]) + "\"";
                    }
                    out += "]";
                    sink(ctx, out);
                    return true;
                }
            case LEVI_RS_ISTR_CAN_DESTROY:
                {
                    auto const& list = item->getCanDestroy();
                    std::string out = "[";
                    for (size_t i = 0; i < list.size(); ++i)
                    {
                        if (!list[i]) continue;
                        if (out.size() > 1) out += ",";
                        out += "\"" + snbtEscape(list[i]->getRawNameId()) + "\"";
                    }
                    out += "]";
                    sink(ctx, out);
                    return true;
                }
            case LEVI_RS_ISTR_CAN_PLACE_ON:
                {
                    auto const& list = item->getCanPlaceOn();
                    std::string out = "[";
                    for (size_t i = 0; i < list.size(); ++i)
                    {
                        if (!list[i]) continue;
                        if (out.size() > 1) out += ",";
                        out += "\"" + snbtEscape(list[i]->getRawNameId()) + "\"";
                    }
                    out += "]";
                    sink(ctx, out);
                    return true;
                }
            case LEVI_RS_ISTR_USER_DATA:
                {
                    auto* ud = item->getUserData();
                    if (!ud)
                    {
                        sink(ctx, "{}");
                        return true;
                    }
                    sink(ctx, ud->toSnbt(SnbtFormat::Minimize));
                    return true;
                }
            case LEVI_RS_ISTR_HOVER_NAME:
                // getHoverName() is behind #ifdef LL_PLAT_C — use getName() which
                // returns the same display string (custom name takes priority
                // inside getName() on the server side).
                sink(ctx, item->getName());
                return true;
            case LEVI_RS_ISTR_EFFECT_NAME:
                sink(ctx, item->getEffectName(false));
                return true;
            case LEVI_RS_ISTR_COLOR:
                {
                    auto color = item->getColor();
                    std::string snbt = "{r:" + snbtNum(color.r);
                    snbt += ",g:" + snbtNum(color.g);
                    snbt += ",b:" + snbtNum(color.b) + "}";
                    sink(ctx, snbt);
                    return true;
                }
            default:
                return false;
            }
        LEVI_RS_API_GUARD_END
    }

    bool api_item_transform(LeviRsStr itemSnbt, int32_t op, LeviRsStr sarg, double narg, void* ctx, LeviRsStrSink out)
    {
        LEVI_RS_API_GUARD_BEGIN
            auto item = itemFromSnbt(std::string_view{itemSnbt});
            if (!item || !out) return false;
            switch (op)
            {
            case LEVI_RS_IOP_SET_CUSTOM_NAME:
                item->setCustomName(::Bedrock::Safety::RedactableString{std::string{sarg}, std::nullopt});
                break;
            case LEVI_RS_IOP_SET_DAMAGE:
                item->setDamageValue(static_cast<short>(narg));
                break;
            case LEVI_RS_IOP_SET_COUNT:
                {
                    int count = static_cast<int>(narg);
                    if (count < 0 || count > 255) return false;
                    item->mCount = static_cast<unsigned char>(count);
                    break;
                }
            case LEVI_RS_IOP_SET_LORE:
                {
                    // sarg is an SNBT list wrapped for parsing: {lore:["l1","l2"]}.
                    auto tag = CompoundTag::fromSnbt(std::string_view{sarg});
                    if (!tag || !tag->contains("lore") || !tag->at("lore").is_array()) return false;
                    std::vector<std::string> lore;
                    for (auto const& p : tag->at("lore").get<ListTag>())
                    {
                        if (!p || p->getId() != Tag::Type::String) continue;
                        lore.emplace_back(static_cast<std::string const&>(static_cast<StringTag const&>(*p)));
                    }
                    item->setCustomLore(lore);
                    break;
                }
            /* ── v5 additive ── */
            case LEVI_RS_IOP_SET_UNBREAKABLE:
                item->setUnbreakable(narg != 0.0);
                break;
            case LEVI_RS_IOP_HURT_AND_BREAK:
                item->hurtAndBreak(static_cast<int>(narg), nullptr);
                break;
            case LEVI_RS_IOP_SET_REPAIR_COST:
                item->setRepairCost(static_cast<int>(narg));
                break;
            case LEVI_RS_IOP_ADD_ENCHANT:
                {
                    // sarg = "enchant_name:level" — apply via the item's enchantment
                    // storage. A full implementation needs EnchantUtils::applyEnchant;
                    // stub returns the item unchanged for now.
                    break;
                }
            case LEVI_RS_IOP_REMOVE_ENCHANTS:
                item->removeEnchants();
                break;
            case LEVI_RS_IOP_CLEAR_LORE:
                item->setCustomLore({});
                break;
            case LEVI_RS_IOP_RESET_NAME:
                item->resetHoverName();
                break;
            case LEVI_RS_IOP_SET_CAN_DESTROY:
                {
                    // sarg = SNBT list wrapped as {v:["minecraft:stone", …]} (same
                    // pattern as SET_LORE) — CompoundTag::fromSnbt parses compounds
                    // only, so a bare [..] list cannot be parsed directly.
                    auto tag = CompoundTag::fromSnbt(std::string_view{sarg});
                    if (!tag || !tag->contains("v") || !tag->at("v").is_array()) return false;
                    std::vector<std::string> list;
                    for (auto const& p : tag->at("v").get<ListTag>())
                    {
                        if (!p || p->getId() != Tag::Type::String) continue;
                        list.emplace_back(static_cast<std::string const&>(
                            static_cast<StringTag const&>(*p)));
                    }
                    item->setCanDestroy(list);
                    break;
                }
            case LEVI_RS_IOP_SET_CAN_PLACE_ON:
                {
                    auto tag = CompoundTag::fromSnbt(std::string_view{sarg});
                    if (!tag || !tag->contains("v") || !tag->at("v").is_array()) return false;
                    std::vector<std::string> list;
                    for (auto const& p : tag->at("v").get<ListTag>())
                    {
                        if (!p || p->getId() != Tag::Type::String) continue;
                        list.emplace_back(static_cast<std::string const&>(
                            static_cast<StringTag const&>(*p)));
                    }
                    item->setCanPlaceOn(list);
                    break;
                }
            default:
                return false;
            }
            out(ctx, itemToSnbt(*item));
            return true;
        LEVI_RS_API_GUARD_END
    }
} // namespace levi_rs::bridge
