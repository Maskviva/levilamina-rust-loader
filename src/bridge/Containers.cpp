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
#include "mc/world/item/ItemStack.h"

namespace levi_rs::bridge
{
    namespace
    {
    } // namespace

    bool api_container_size(LeviRsContainerRef ref, int32_t* out)
    {
        Container* c = resolveContainer(ref);
        if (!c || !out) return false;
        *out = c->getContainerSize();
        return true;
    }

    bool api_container_get_item(LeviRsContainerRef ref, int32_t slot, void* ctx, LeviRsStrSink sink)
    {
        Container* c = resolveContainer(ref);
        if (!c || !sink) return false;
        if (slot < 0 || slot >= c->getContainerSize()) return false;
        sink(ctx, itemToSnbt(c->getItem(slot)));
        return true;
    }

    bool api_container_set_item(LeviRsContainerRef ref, int32_t slot, LeviRsStr itemSnbt)
    {
        Container* c = resolveContainer(ref);
        if (!c) return false;
        if (slot < 0 || slot >= c->getContainerSize()) return false;
        auto opt = itemFromSnbt(std::string_view{itemSnbt});
        if (!opt) return false;
        ItemStack item = std::move(*opt);
        c->setItem(slot, item);
        return true;
    }

    bool api_container_add_item(LeviRsContainerRef ref, LeviRsStr itemSnbt)
    {
        Container* c = resolveContainer(ref);
        if (!c) return false;
        auto opt = itemFromSnbt(std::string_view{itemSnbt});
        if (!opt) return false;
        ItemStack item = std::move(*opt);
        if (item.isNull()) return false;
        return c->addItem(item);
    }

    bool api_container_remove_item(LeviRsContainerRef ref, int32_t slot, int32_t count)
    {
        Container* c = resolveContainer(ref);
        if (!c) return false;
        if (slot < 0 || slot >= c->getContainerSize() || count <= 0) return false;
        c->removeItem(slot, count);
        return true;
    }

    bool api_container_clear(LeviRsContainerRef ref)
    {
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
    }
} // namespace levi_rs::bridge
