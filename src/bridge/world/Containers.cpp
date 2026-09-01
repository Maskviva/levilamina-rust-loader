/**
 * bridge/Containers.cpp — container access (ABI v5 §E).
 *
 * Container handles are "owner + which" references resolved on every call
 * (decision #10: everything goes through the Container virtual interface, so
 * chests / player inventories / ender chests share one code path).
 */
#include "bridge/Api.h"
#include "bridge/Common.h"

#include <string>

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/Container.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"

namespace levi_rs::bridge
{
    namespace
    {
    } // namespace

    bool api_container_size(LeviRsContainerRef ref, int32_t* out)
    {
        LEVI_RS_API_GUARD_BEGIN
            Container* c = resolveContainer(ref);
            if (!c || !out) return false;
            *out = c->getContainerSize();
            return true;
        LEVI_RS_API_GUARD_END
    }

    bool api_container_get_item(LeviRsContainerRef ref, int32_t slot, void* ctx, LeviRsStrSink sink)
    {
        LEVI_RS_API_GUARD_BEGIN
            Container* c = resolveContainer(ref);
            if (!c || !sink) return false;
            if (slot < 0 || slot >= c->getContainerSize()) return false;
            sink(ctx, itemToSnbt(c->getItem(slot)));
            return true;
        LEVI_RS_API_GUARD_END
    }

    bool api_container_set_item(LeviRsContainerRef ref, int32_t slot, LeviRsStr itemSnbt)
    {
        LEVI_RS_API_GUARD_BEGIN
            Container* c = resolveContainer(ref);
            if (!c) return false;
            if (slot < 0 || slot >= c->getContainerSize()) return false;
            auto opt = itemFromSnbt(std::string_view{itemSnbt});
            if (!opt) return false;
            ItemStack item = std::move(*opt);
            c->setItem(slot, item);
            return true;
        LEVI_RS_API_GUARD_END
    }

    bool api_container_add_item(LeviRsContainerRef ref, LeviRsStr itemSnbt)
    {
        LEVI_RS_API_GUARD_BEGIN
            Container* c = resolveContainer(ref);
            if (!c) return false;
            auto opt = itemFromSnbt(std::string_view{itemSnbt});
            if (!opt) return false;
            ItemStack item = std::move(*opt);
            if (item.isNull()) return false;
            return c->addItem(item);
        LEVI_RS_API_GUARD_END
    }

    bool api_container_remove_item(LeviRsContainerRef ref, int32_t slot, int32_t count)
    {
        LEVI_RS_API_GUARD_BEGIN
            Container* c = resolveContainer(ref);
            if (!c) return false;
            if (slot < 0 || slot >= c->getContainerSize() || count <= 0) return false;
            c->removeItem(slot, count);
            return true;
        LEVI_RS_API_GUARD_END
    }

    bool api_container_clear(LeviRsContainerRef ref)
    {
        LEVI_RS_API_GUARD_BEGIN
            Container* c = resolveContainer(ref);
            if (!c) return false;
            // Clear slot by slot through the virtual interface — removeAllItems /
            // clearContent availability varies across engine drops; this doesn't.
            int size = c->getContainerSize();
            for (int i = 0; i < size; ++i)
            {
                c->setItem(i, ItemStack::EMPTY_ITEM());
            }
            return true;
        LEVI_RS_API_GUARD_END
    }

    bool api_container_refresh(LeviRsContainerRef ref)
    {
        LEVI_RS_API_GUARD_BEGIN
            // Block containers have no single owner to resend to. Their viewers
            // are kept in sync by the engine's own container-transaction path, so
            // there is nothing useful to do here — say so rather than pretending.
            if (ref.which == 4) return false;

            Player* p = resolvePlayer(ref.player);
            if (!p) return false;

            // Player::sendInventory rebuilds and sends InventoryContentPacket for
            // the player's containers. `false` = do not also re-select the hotbar
            // slot; forcing a slot change on every refresh would yank the item the
            // player is holding, which is worse than the stale render we are here
            // to fix.
            p->sendInventory(false);
            return true;
        LEVI_RS_API_GUARD_END
    }
} // namespace levi_rs::bridge
