/**
 * bridge/Items.cpp — item value objects (ABI v4 §E).
 *
 * Items cross the boundary as ItemStack::save SNBT. Every call rebuilds a
 * transient ItemStack (ItemStack::fromTag), queries or mutates it, and —
 * for transforms — serializes it right back. Zero cross-boundary ownership.
 */
#include "bridge/Api.h"
#include "bridge/Common.h"

#include <string>
#include <vector>

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/safety/RedactableString.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/SaveContext.h"
#include "mc/world/item/SaveContextFactory.h"

namespace levi_rs::bridge
{
    namespace
    {
        /** Rebuild a transient ItemStack from item SNBT; nullopt on parse failure. */
        std::optional<ItemStack> rebuild(LeviRsStr itemSnbt)
        {
            auto tag = CompoundTag::fromSnbt(std::string_view{itemSnbt});
            if (!tag) return std::nullopt;
            return ItemStack::fromTag(*tag);
        }

        /** Serialize an ItemStack back to SNBT via the clone save context. */
        std::string serialize(ItemStack const& item)
        {
            auto ctx = SaveContextFactory::createCloneSaveContext();
            auto tag = item.save(*ctx);
            if (!tag) return "{}";
            return tag->toSnbt(SnbtFormat::Minimize);
        }
    } // namespace

    bool api_item_get_num(LeviRsStr itemSnbt, int32_t prop, double* out)
    {
        auto item = rebuild(itemSnbt);
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
        default:
            return false;
        }
    }

    bool api_item_get_str(LeviRsStr itemSnbt, int32_t prop, void* ctx, LeviRsStrSink sink)
    {
        auto item = rebuild(itemSnbt);
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
        default:
            return false;
        }
    }

    bool api_item_transform(LeviRsStr itemSnbt, int32_t op, LeviRsStr sarg, double narg, void* ctx, LeviRsStrSink out)
    {
        auto item = rebuild(itemSnbt);
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
        default:
            return false;
        }
        out(ctx, serialize(*item));
        return true;
    }
} // namespace levi_rs::bridge
