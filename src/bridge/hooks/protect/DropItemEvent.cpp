/**
 * bridge/hooks/DropItemEvent.cpp — "PlayerDropItemEvent": a player is about to
 * throw an item out of their inventory, and it **can be cancelled**.
 *
 * # Why this hook exists
 *
 * `event::names::PLAYER_DROP_ITEM` claimed to be "verified against the pinned
 * LeviLamina headers". It was not: there is no `PlayerDropItemEvent` anywhere
 * under `ll/api/event/player/`. The only thing with that name is the *vanilla*
 * `mc/world/events/PlayerDropItemEvent.h`, a `PlayerGameplayEvent<void>`
 * variant — not on the LL event bus, not subscribable, not cancellable.
 *
 * So `subscribe_event("PlayerDropItemEvent")` fell through to `resolveEventId`,
 * found nothing, logged "unknown or ambiguous event id", and returned null.
 *
 * # Two hooks, because there are two ways to drop
 *
 * This is the part that is easy to get wrong, and LegacyScriptEngine gets it
 * right (`lse/events/PlayerEvents.cpp`, `DropItemHook1` / `DropItemHook2`):
 *
 *   1. `Player::drop` — the Q key / "drop held item". Returns bool; returning
 *      false without calling origin refuses the drop.
 *
 *   2. `ComplexInventoryTransaction::handle` — dragging a stack out of the
 *      **open inventory screen**. This path never touches `Player::drop`, so a
 *      protection built on hook 1 alone blocks Q and silently allows the
 *      inventory UI. Cancelling means returning `NoError` without calling
 *      origin: the transaction is reported as handled and nothing moves.
 *
 * The filter on hook 2 (NormalTransaction, exactly one action sourced from
 * `ContainerInventory`) is LSE's, and it is what distinguishes "player threw
 * one stack on the ground" from every other inventory shuffle that flows
 * through the same virtual.
 *
 * # Payload
 *
 * ```text
 * {eventId, x, y, z, dim, item, randomly, viaInventoryUi, _player:{name,xuid,uuid}}
 * ```
 *
 * `x/y/z` is the player's position as flat integers — the shape every hook
 * event in this directory uses, because LL's reflection serialises `Vec3` as a
 * JSON *array* and consumers expecting `{x,y,z}` read nothing from it.
 */
#include "bridge/Common.h"
#include "bridge/hooks/HookEvents.h"

#include <string>

#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/ContainerID.h"
#include "mc/world/actor/player/Player.h"
// PlayerInventory.h is required, not optional: `Player.h` only *forward-declares*
// PlayerInventory, and `ll::TypedStorage` has a specialisation that collapses
// `unique_ptr<T>` to the bare `unique_ptr<T>` (Alias.h) — so `player.mInventory`
// is a real unique_ptr and `.get()` needs the pointee complete. Without this
// include MSVC reports C2027 on the incomplete type plus a cascading C2039
// naming `unique_ptr` as the thing missing the member, which reads like a wrong
// number of dereferences and is not.
#include "mc/world/actor/player/PlayerInventory.h"
#include "mc/world/inventory/transaction/ComplexInventoryTransaction.h"
#include "mc/world/inventory/transaction/InventoryAction.h"
#include "mc/world/inventory/transaction/InventorySource.h"
#include "mc/world/inventory/transaction/InventorySourceType.h"
#include "mc/world/inventory/transaction/InventoryTransaction.h"
#include "mc/world/inventory/transaction/InventoryTransactionError.h"
#include "mc/world/item/ItemStack.h"

namespace levi_rs::bridge
{
    namespace
    {
        HookEventDef& dropItemDef(); // fwd

        /** Item type name, or "" if the stack can't be read. */
        std::string safeTypeName(::ItemStack const& item)
        {
            try
            {
                return item.isNull() ? std::string{} : item.getTypeName();
            }
            catch (...)
            {
                return {};
            }
        }

        /** Shared payload builder — both hooks report the same event shape. */
        std::string buildSnbt(Player& p, std::string const& itemName, bool randomly, bool viaUi)
        {
            auto const& pos = p.getPosition();
            return "{\"eventId\":\"PlayerDropItemEvent\""
                   ",\"x\":" + std::to_string(static_cast<int>(pos.x))
                 + ",\"y\":" + std::to_string(static_cast<int>(pos.y))
                 + ",\"z\":" + std::to_string(static_cast<int>(pos.z))
                 + ",\"dim\":" + std::to_string(static_cast<int>(p.getDimensionId()))
                 + ",\"randomly\":" + (randomly ? "1" : "0")
                 + ",\"viaInventoryUi\":" + (viaUi ? "1" : "0")
                 + ",\"item\":\"" + snbtEscape(itemName)
                 + "\",\"_player\":{\"name\":\"" + snbtEscape(p.getRealName())
                 + "\",\"xuid\":\"" + snbtEscape(p.getXuid())
                 + "\",\"uuid\":\"" + snbtEscape(p.getUuid().asString()) + "\"}}";
        }

        /** Hook 1: Q key / drop held item. */
        LL_TYPE_INSTANCE_HOOK(
            PlayerDropItemHook,
            ll::memory::HookPriority::Normal,
            Player,
            &Player::$drop,
            bool,
            ::ItemStack const& item,
            bool randomly)
        {
            auto& def = dropItemDef();
            if (!def.live())
            {
                static bool warned = false;
                if (!warned)
                {
                    warned = true;
                    bridgeLogger().warn(
                        "[DropItemEvent] PlayerDropItemHook 已触发，但没有任何订阅者"
                        "（def.live()==false）。说明原生 detour 已装上，但 Rust 侧没成功订阅"
                        " 'PlayerDropItemEvent'。检查启动日志里是否有“订阅事件 PlayerDropItemEvent 失败”。");
                }
                return origin(item, randomly);
            }

            if (dispatchHookEventCancellable(def, buildSnbt(*this, safeTypeName(item), randomly, false)))
            {
                // false == "the drop did not happen". The stack is still in the
                // container, so there is nothing to restore.
                return false;
            }
            return origin(item, randomly);
        }

        /** Hook 2: dragging a stack out of the open inventory screen. */
        LL_TYPE_INSTANCE_HOOK(
            InventoryUiDropHook,
            ll::memory::HookPriority::Normal,
            ComplexInventoryTransaction,
            &ComplexInventoryTransaction::$handle,
            ::InventoryTransactionError,
            ::Player& player,
            bool isSenderAuthority)
        {
            auto& def = dropItemDef();
            if (!def.live() || mType != ComplexInventoryTransaction::Type::NormalTransaction)
            {
                if (!def.live())
                {
                    static bool warned = false;
                    if (!warned)
                    {
                        warned = true;
                        bridgeLogger().warn(
                            "[DropItemEvent] InventoryUiDropHook 已触发，但没有订阅者"
                            "（def.live()==false）。说明原生 detour 已装上，但 Rust 侧没成功订阅"
                            " 'PlayerDropItemEvent'。");
                    }
                }
                return origin(player, isSenderAuthority);
            }

            // Filter lifted from LSE: a single action sourced from the player's
            // own inventory is the "threw a stack on the ground" shape. Every
            // other transaction (crafting, container moves, creative fills)
            // either has a different source or more than one action.
            ::InventorySource source{
                ::InventorySourceType::ContainerInventory,
                ::ContainerID::Inventory,
                ::InventorySource::InventorySourceFlags::NoFlag};

            auto const& actions = mTransaction->getActions(source);
            if (actions.size() != 1)
            {
                return origin(player, isSenderAuthority);
            }

            std::string itemName;
            try
            {
                itemName = safeTypeName(player.mInventory.get()->getItem(actions[0].mSlot, ::ContainerID::Inventory));
            }
            catch (...)
            {
                itemName.clear();
            }

            if (dispatchHookEventCancellable(def, buildSnbt(player, itemName, false, true)))
            {
                // NoError, not an error code: the client is told the
                // transaction was handled, so it does not retry or desync. The
                // items simply never left the inventory.
                return ::InventoryTransactionError::NoError;
            }
            return origin(player, isSenderAuthority);
        }

        HookEventDef gDef{
            "PlayerDropItemEvent",
            []
            {
                // `hook()` returns the ll::memory::hookEx status: 0 == success,
                // non-zero == failure (symbol not found / patch refused). The
                // original code swallowed this — a failed install looked identical
                // to "working", and the drop-protection silently did nothing.
                // Surface it so a version mismatch is diagnosable from the log.
                int r1 = PlayerDropItemHook::hook();
                int r2 = InventoryUiDropHook::hook();
                auto& log = bridgeLogger();
                log.info(
                    "[DropItemEvent] 安装 detour：PlayerDropItemHook={} (code={})，"
                    "InventoryUiDropHook={} (code={})",
                    r1 == 0 ? "成功" : "失败", r1,
                    r2 == 0 ? "成功" : "失败", r2
                );
                if (r1 != 0 || r2 != 0)
                {
                    log.error(
                        "[DropItemEvent] 原生 detour 安装失败（非 0 状态码）。最常见原因是"
                        "本 loader 链接的 BDS/LeviLamina 版本与服务器实际运行的版本不一致，"
                        "导致 Player::$drop 或 ComplexInventoryTransaction::$handle 的符号地址"
                        "解析错误。结果：丢弃物品保护完全不生效（物品照常掉落，且不触发任何"
                        "拦截日志）。请用服务器实际运行的 LeviLamina/BDS 版本重新编译本 loader。");
                }
            }};
        HookEventDef& dropItemDef() { return gDef; }

        HookEventRegistrar gReg{gDef};
    } // namespace
} // namespace levi_rs::bridge
