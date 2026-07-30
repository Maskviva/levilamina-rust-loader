/**
 * bridge/hooks/DecisionThrottle.h — a per-(player, place) decision cache for
 * hook events that fire on **passive** game behaviour rather than on a player
 * action.
 *
 * # Why this exists
 *
 * Most hooks in this directory fire once per deliberate act: a click, a drop, a
 * mount. Two do not:
 *
 *   PressurePlateEvent.cpp — `entityInside` runs every tick for every entity
 *                            overlapping the block
 *   PushEntityEvent.cpp    — collision push runs every tick for every pair of
 *                            overlapping entities
 *
 * Each dispatch means: build an SNBT string, cross the FFI boundary, parse it
 * in Rust, hit the plot database, cross back. At 20 Hz per player per block,
 * one AFK player standing on a plate would cost more than the rest of the
 * plugin combined, and a farm with a dozen plates would be a denial of service
 * against the very server that installed the protection. Throttling here is
 * not a micro-optimisation; without it the protection is unshippable.
 *
 * # Why both outcomes are cached
 *
 * Caching only denials is the tempting half-measure and it is the wrong one: a
 * player walking around inside their *own* plot generates exactly the same call
 * volume, and every one of those calls would be an allowed decision paid for at
 * full price. The common case must be cheap too.
 *
 * # Why the key is the XUID
 *
 * `Actor*` is the obvious key and it is unsafe: actor pointers get recycled, so
 * a recycled pointer inside the TTL window hands one player another player's
 * decision. In the direction that matters that is a wrong *allow* — a griefer
 * inheriting a resident's permissions. A string hash per call is cheap next to
 * that.
 *
 * # Why the position is part of the key
 *
 * The cache must never carry a decision across a plot border. Including the
 * place means moving one block invalidates immediately, so the worst a stale
 * entry can do is repeat a decision that was correct for the same player at the
 * same spot a few ticks ago.
 *
 * Server thread only (like every hook here), so no locking.
 */
#pragma once

#include <chrono>
#include <string>
#include <unordered_map>

namespace levi_rs::bridge
{
    /** 250 ms = 5 ticks. Long enough to matter, short enough to feel instant. */
    inline constexpr long long kDecisionTtlMs = 250;

    struct ThrottledDecision
    {
        int       x = 0, y = 0, z = 0;
        int       dim = 0;
        bool      cancelled = false;
        long long atMs = 0;
    };

    inline long long throttleNowMs()
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }

    /**
     * Look up a cached decision for `key` at (x,y,z,dim). Returns true and sets
     * `out` on a hit; false means the caller must dispatch and then call
     * `throttleStore`.
     *
     * `cache` is supplied by the caller so each hook keeps its own map — a
     * shared one would let a pressure-plate decision answer a push question at
     * the same coordinates.
     */
    inline bool throttleLookup(
        std::unordered_map<std::string, ThrottledDecision>& cache,
        std::string const&                                  key,
        int                                                 x,
        int                                                 y,
        int                                                 z,
        int                                                 dim,
        long long                                           now,
        bool&                                               out)
    {
        auto it = cache.find(key);
        if (it == cache.end()) return false;
        auto const& c = it->second;
        if (c.x != x || c.y != y || c.z != z || c.dim != dim) return false;
        if (now - c.atMs >= kDecisionTtlMs) return false;
        out = c.cancelled;
        return true;
    }

    inline void throttleStore(
        std::unordered_map<std::string, ThrottledDecision>& cache,
        std::string const&                                  key,
        int                                                 x,
        int                                                 y,
        int                                                 z,
        int                                                 dim,
        long long                                           now,
        bool                                                cancelled)
    {
        // Bounded growth: entries are only added on a miss, and the whole map is
        // dropped once it outgrows any plausible player count. Simpler than
        // per-entry expiry, and a cleared cache costs at most one extra dispatch
        // per player.
        if (cache.size() > 512) cache.clear();
        cache[key] = ThrottledDecision{x, y, z, dim, cancelled, now};
    }
} // namespace levi_rs::bridge
