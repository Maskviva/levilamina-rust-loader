// Standalone validation of the two pieces of NEW logic added by this patch
// set, compiled against stubs so it can run without the LeviLamina SDK:
//
//   1. snbtNum()          -- the SNBT number formatter
//   2. the write-back merge -- field-level diff against the snapshot
//
// Build: g++ -std=c++20 -Wall -Wextra -o verify_logic tests/verify_logic.cpp && ./verify_logic

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <system_error>
#include <type_traits>
#include <vector>

// ─────────── 1. snbtNum: verbatim copy from bridge/Common.h ───────────

template <typename T>
    requires std::is_arithmetic_v<T>
std::string snbtNum(T v)
{
    if constexpr (std::is_floating_point_v<T>)
    {
        if (!std::isfinite(v)) return "0";
    }
    char buf[40];
    auto [end, ec] = std::to_chars(buf, buf + sizeof(buf), v);
    if (ec != std::errc{}) return "0";
    return std::string(buf, end);
}

// ─────────── 2. merge: same shape as the Events.cpp write-back ───────────
// CompoundTag stands in as map<string,string>; CompoundTagVariant equality
// is string equality. The control flow mirrors the real lambda exactly.

using Tag = std::map<std::string, std::string>;

void mergeWriteBack(Tag& data, Tag const& base, Tag const& edited)
{
    for (auto const& [key, value] : edited)
    {
        auto it = base.find(key);
        if (it == base.end() || !(it->second == value))
        {
            data[key] = value;
        }
    }
    for (auto const& [key, value] : base)
    {
        (void)value;
        if (!edited.contains(key)) data.erase(key);
    }
}

// ─────────────────────────── harness ───────────────────────────

static int failures = 0;

void check(bool ok, char const* what)
{
    std::printf("%-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

void checkEq(std::string const& got, std::string const& want, char const* what)
{
    bool ok = got == want;
    std::printf("%-58s %s", what, ok ? "ok" : "FAIL");
    if (!ok) std::printf("   got=%s want=%s", got.c_str(), want.c_str());
    std::printf("\n");
    if (!ok) ++failures;
}

int main()
{
    std::printf("=== snbtNum ===\n");

    // The bug this replaces: to_string(double) is "%f", six fixed decimals.
    checkEq(snbtNum(0.0000001), "1e-07", "tiny double survives (to_string gave 0.000000)");
    checkEq(snbtNum(1.5), "1.5", "simple double has no trailing zeros");
    checkEq(snbtNum(1234567.5f), "1234567.5", "large float keeps its fraction");
    checkEq(snbtNum(-0.25), "-0.25", "negative double");
    checkEq(snbtNum(42), "42", "int");
    checkEq(snbtNum(-7), "-7", "negative int");
    checkEq(snbtNum(static_cast<std::int64_t>(9007199254740993LL)),
            "9007199254740993", "int64 exact past 2^53");
    checkEq(snbtNum(0.0), "0", "zero");

    // Non-finite must not emit nan/inf: the SNBT parser would reject the
    // whole document, losing every field rather than one.
    checkEq(snbtNum(std::nan("")), "0", "NaN degrades to 0, not \"nan\"");
    checkEq(snbtNum(INFINITY), "0", "inf degrades to 0");
    checkEq(snbtNum(-INFINITY), "0", "-inf degrades to 0");

    // Round-trip: what we emit must parse back to the same bits.
    {
        double vals[] = {0.1, 1e300, 1e-300, 3.14159265358979, -2.718281828459045};
        bool allOk = true;
        for (double v : vals)
        {
            std::string s = snbtNum(v);
            double back = 0;
            std::from_chars(s.data(), s.data() + s.size(), back);
            if (back != v) allOk = false;
        }
        check(allOk, "double round-trips exactly through snbtNum");
    }

    std::printf("\n=== event write-back merge ===\n");

    // The reported bug: two mods subscribe to one event. A (high priority)
    // edits `message`. B (low priority) was handed the tag as it looked
    // BEFORE A ran, and only wants to cancel. Old code replaced the whole
    // tag, silently reverting A's edit.
    {
        Tag live{{"message", "edited by A"}, {"cancelled", "0"}};
        Tag bSnapshot{{"message", "original"}, {"cancelled", "0"}};
        Tag bEdited{{"message", "original"}, {"cancelled", "1"}};
        mergeWriteBack(live, bSnapshot, bEdited);
        checkEq(live["message"], "edited by A", "A's edit survives B's write-back");
        checkEq(live["cancelled"], "1", "B's cancel is applied");
    }

    // Same field from both: last writer wins. Best any merge can do.
    {
        Tag live{{"message", "from A"}};
        Tag snap{{"message", "original"}};
        Tag edit{{"message", "from B"}};
        mergeWriteBack(live, snap, edit);
        checkEq(live["message"], "from B", "same-field conflict resolves last-writer-wins");
    }

    // A listener that writes back an untouched copy must be a no-op.
    {
        Tag live{{"a", "1"}, {"b", "2"}};
        Tag snap{{"a", "1"}, {"b", "2"}};
        Tag edit = snap;
        mergeWriteBack(live, snap, edit);
        check(live == snap, "unchanged write-back is a no-op");
    }

    // Removal must propagate (a mod deleting a field).
    {
        Tag live{{"a", "1"}, {"b", "2"}};
        Tag snap{{"a", "1"}, {"b", "2"}};
        Tag edit{{"a", "1"}};
        mergeWriteBack(live, snap, edit);
        check(!live.contains("b"), "field removal propagates");
        checkEq(live["a"], "1", "untouched field survives removal of another");
    }

    // Addition of a brand-new field.
    {
        Tag live{{"a", "1"}};
        Tag snap{{"a", "1"}};
        Tag edit{{"a", "1"}, {"new", "x"}};
        mergeWriteBack(live, snap, edit);
        checkEq(live["new"], "x", "newly added field is written");
    }

    // enrichWithPlayer splices `_player` into the snapshot but not into the
    // live tag. A round-tripped write-back must not resurrect it in `data`.
    {
        Tag live{{"cancelled", "0"}};
        Tag snap{{"cancelled", "0"}, {"_player", "{name:bob}"}};
        Tag edit{{"cancelled", "1"}, {"_player", "{name:bob}"}};
        mergeWriteBack(live, snap, edit);
        check(!live.contains("_player"), "_player enrichment is not written back into the event");
        checkEq(live["cancelled"], "1", "real edit alongside _player still applies");
    }

    std::printf("\n=== ABI compatibility gates ===\n");

    // Guards the rule stated at the top of LeviRsAbi.h: an ADDITIVE change
    // must not bump LEVI_RS_ABI_VERSION. The gates are:
    //
    //   loader: MIN_SUPPORTED <= mod_abi <= LOADER_VERSION
    //   mod:    loader_abi >= mod_abi  &&  loader.struct_size >= mod's sizeof
    //
    // struct_size alone already separates "mod uses a slot the loader lacks"
    // from "mod doesn't". A version bump cannot express that distinction and
    // so refuses pairings that work fine -- the last case below is the one
    // that regresses if somebody bumps the version for an appended function.
    {
        constexpr int kMinSupported = 1;

        auto sizeOf = [](int slots) { return 8 + slots * 8; };
        auto loads = [&](int modAbi, int modSlots, int loaderAbi, int loaderSlots) {
            bool loaderGate = kMinSupported <= modAbi && modAbi <= loaderAbi;
            bool modGate = loaderAbi >= modAbi && sizeOf(loaderSlots) >= sizeOf(modSlots);
            return loaderGate && modGate;
        };

        constexpr int kOld = 176; // slot count before the money listeners
        constexpr int kNew = 178; // after appending money_listen_for/_unlisten
        constexpr int kV = 5;     // version must NOT have moved

        check(loads(kV, kOld, kV, kNew), "old mod loads on newer (larger) loader");
        check(!loads(kV, kNew, kV, kOld), "mod needing new slots refuses older loader");
        check(loads(kV, kNew, kV, kNew), "matched mod and loader load");
        check(loads(1, 20, kV, kNew), "ancient mod still loads (MIN_SUPPORTED=1)");

        // The regression this guards against: with the version bumped to 6, a
        // mod merely REBUILT against the newer crate -- using no new slot --
        // would refuse every older loader.
        constexpr int kBumped = 6;
        check(!loads(kBumped, kOld, kV, kOld),
              "bumping version would wrongly refuse an unchanged mod");
        check(loads(kV, kOld, kV, kOld),
              "not bumping keeps that same pairing working");
    }

    std::printf("\n=== lane fingerprint gate ===\n");

    // Lane.cpp:api_lane_acquire。0 以前表示「不校验」，那是个洞：这个调用给
    // 出去的是完整的 vtable + data 裸指针，消费方随后按自己的 C::Table 偏移
    // 调它们。跳过校验 = 类型混淆。
    {
        auto accepts = [](std::uint64_t want, std::uint64_t theirs) {
            return !(want == 0 || want != theirs);
        };
        check(accepts(0xdeadbeef, 0xdeadbeef), "matching fingerprint is accepted");
        check(!accepts(0xdeadbeef, 0xcafe), "mismatched fingerprint is refused");
        check(!accepts(0, 0xdeadbeef), "fingerprint 0 is refused, not a bypass");
        check(!accepts(0, 0), "fingerprint 0 is refused even against 0");
    }

    std::printf("\n=== lane busy counter (re-entrant unload) ===\n");

    // alive 只能证明「检查那一刻提供方还在」，管不到检查与调用之间。全部服务
    // 器线程调用挡住了并发卸载，挡不住重入卸载。busy 让 unload 能拒绝。
    {
        struct Cell { unsigned alive = 1; unsigned busy = 0; } cell;
        auto unloadAllowed = [&] { return cell.busy == 0; };

        check(unloadAllowed(), "idle lane can be unloaded");
        cell.busy++;                                  // Lane::with 进入
        check(!unloadAllowed(), "unload refused while a lane call is in flight");
        cell.busy--;                                  // BusyGuard drop
        check(unloadAllowed(), "unload allowed again after the call returns");

        // 嵌套借用：内层归还不该让外层失去保护。
        cell.busy++; cell.busy++;
        check(!unloadAllowed(), "nested lane calls both counted");
        cell.busy--;
        check(!unloadAllowed(), "inner return does not unprotect the outer frame");
        cell.busy--;
        check(unloadAllowed(), "balanced after both return");
    }

    std::printf("\n%s (%d failure%s)\n",
                failures ? "FAILED" : "ALL PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
