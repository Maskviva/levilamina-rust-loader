/**
 * bridge/Common.h — shared internals for the levi_rs bridge.
 *
 * Everything here enforces the two project-wide disciplines:
 *   1. readiness guard first (levelReady),
 *   2. handles are identifiers, re-resolved on every call (resolvePlayer /
 *      resolveActor / resolveContainer) — never cached native pointers.
 */
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "LeviRsAbi.h"

class Actor;
class BlockSource;
class CompoundTag;
class CompoundTagVariant;
class Container;
class ItemStack;
class Level;
class Player;

namespace levi_rs
{
    class RustMod;

    RustMod* asMod(LeviRsModHandle h);

    namespace bridge
    {
        /**
         * Safely extract a double from an NBT variant, bypassing the C++20
         * std::integral constraint that causes ByteTag/IntTag -> double
         * static_casts to silently return 0.
         */
        double nbtToDouble(CompoundTagVariant const& val, double def = 0.0);

        /** Level pointer if the world is usable, nullptr otherwise. */
        Level* levelReady();

        /** BlockSource of a dimension, nullptr if the dimension isn't loaded. */
        BlockSource* blockSourceOf(int32_t dim);

        /**
         * Re-resolve a player selector against the live player list.
         *   kind 0 (name): exact Player::getRealName() match first, then a second
         *                  pass on Actor::getNameTag() (display name).
         *   kind 1 (xuid) / kind 2 (uuid): exact match.
         * Returns nullptr when nobody matches — the caller reports failure.
         */
        Player* resolvePlayer(LeviRsPlayerSel sel);

        /** Re-resolve an ActorUniqueID against the live actor list (players included). */
        Actor* resolveActor(LeviRsActorId id);

        /** Resolve a container reference ("owner + which"). nullptr on any failure. */
        Container* resolveContainer(LeviRsContainerRef ref);

        /** SNBT-escape a string for embedding in hand-built SNBT ("..\"..\\.."). */
        std::string snbtEscape(std::string_view s);

        /**
         * Item (de)serialization across the FFI boundary — items always cross as
         * `ItemStack::save` SNBT. `itemToSnbt` produces it (empty item → "{}");
         * `itemFromSnbt` rebuilds a transient `ItemStack`, returning nullopt on
         * malformed input. Shared so Items/Containers/Players agree byte-for-byte.
         */
        std::string itemToSnbt(ItemStack const& item);
        std::optional<ItemStack> itemFromSnbt(std::string_view snbt);

        /**
         * The player-identity enrichment used by the event path: if `data` embeds a
         * live Player pointer stub, splice a `_player` {name,xuid,uuid} field into a
         * copy and serialize that; otherwise serialize `data` as-is.
         */
        std::string enrichWithPlayer(CompoundTag const& data);

        /** Run a command as server console, discarding output. True on ≥1 success. */
        bool runConsoleCommand(std::string const& cmd);

        /**
         * Vanilla dimension name ("overworld" / "nether" / "the_end") for a
         * dimension id, used to build `/execute in <dim> run …` commands.
         * Out-of-range ids fall back to "overworld".
         */
        char const* dimensionName(int dim);

        /** Serialize a player's identity + position line: {name,xuid,uuid,dim,x,y,z}. */
        std::string playerSummarySnbt(Player& p);
    } // namespace bridge
} // namespace levi_rs
