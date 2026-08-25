/**
 * bridge/Money.cpp — money API entry points, backed by the OPTIONAL LLMoney
 * (LegacyMoney) plugin.
 *
 * LegacyMoney.dll is delay-loaded, so this translation unit compiles and the
 * loader starts even when LegacyMoney isn't installed. Every entry point is
 * gated by moneyBackendReady() (mod-list + symbol dual check, see
 * MoneyGuard.h). When the backend is missing/disabled we return a safe
 * default instead of calling an unresolved `LLMoney_*` thunk — which would
 * otherwise raise a delay-load structured exception and take BDS down.
 */
#include "LLMoney.h"
#include "bridge/Api.h"
#include "BridgeApi.h"
#include "bridge/Common.h"
#include "bridge/MoneyGuard.h"

#include "RustMod.h"

namespace levi_rs::bridge
{
    long long api_get_money(LeviRsStr xuid)
    {
        if (!moneyBackendReady()) return 0;
        return LLMoney_Get(std::string{xuid});
    }

    bool api_set_money(LeviRsStr xuid, long long money)
    {
        if (!moneyBackendReady()) return false;
        return LLMoney_Set(std::string{xuid}, money);
    }

    bool api_add_money(LeviRsStr xuid, long long money)
    {
        if (!moneyBackendReady()) return false;
        return LLMoney_Add(std::string{xuid}, money);
    }

    bool api_reduce_money(LeviRsStr xuid, long long money)
    {
        if (!moneyBackendReady()) return false;
        return LLMoney_Reduce(std::string{xuid}, money);
    }

    bool api_trans_money(LeviRsStr from, LeviRsStr to, long long val, LeviRsStr note)
    {
        if (!moneyBackendReady()) return false;
        return LLMoney_Trans(std::string{from}, std::string{to}, val, std::string{note});
    }

    void api_money_get_hist(LeviRsStr xuid, int timediff, void* ctx, LeviRsStrSink sink)
    {
        // Backend absent → no records. Don't invoke the sink at all so the
        // Rust side sees an empty history (matching "nothing found").
        if (!moneyBackendReady()) return;
        sink(ctx, LLMoney_GetHist(std::string{xuid}, timediff));
    }

    void api_money_clear_hist(int difftime)
    {
        if (!moneyBackendReady()) return;
        LLMoney_ClearHist(difftime);
    }

    /* ── money 事件监听器 ────────────────────────────────────────────────
     *
     * LegacyMoney 的实现（它自己的 src/Event.cpp）是：
     *
     *     void LLMoney_ListenBeforeEvent(cb) { beforeCallbacks.push_back(cb); }
     *
     * 只 append，整个 LegacyMoney API 里没有任何反注册。两个推论：
     *
     *   1. loader 只能装**一个常驻蹦床**、自己扇出。原代码每次注册都往
     *      LegacyMoney 再 push 一个蹦床，注册两次就是每笔交易派发两遍。
     *   2. 这两个槽位早于 mod-scoped 约定，只收一个裸函数指针，没有 mod 句柄
     *      也没有 user 上下文。loader 因此不知道回调属于谁，卸载时清不掉——
     *      指针活过 FreeLibrary，下一笔交易跳进未映射内存。归属靠
     *      addressOwnedBy() 从函数地址反查模块来恢复。
     *
     * 遗留形状还带来一个改不了的限制：每种只能有一个监听器，第二个 mod 注册
     * 会静默顶掉第一个。签名不能变（ABI 只能追加），要修得追加一对带 mod 和
     * user 的新槽位。
     */
    namespace
    {
        LLMoneyCallback g_before = nullptr;
        LLMoneyCallback g_after = nullptr;
        bool g_beforeHooked = false;
        bool g_afterHooked = false;
    } // namespace

    void api_money_listen_before_event(LLMoneyCallback callback)
    {
        // 无条件存下：后端可能稍后才出现，而蹦床是在派发时读 g_before 的，
        // 不是在安装时。
        g_before = callback;
        if (g_beforeHooked || !moneyBackendReady()) return;
        g_beforeHooked = true;
        LLMoney_ListenBeforeEvent([](::LLMoneyEvent t, std::string f, std::string to, long long v)
        {
            return g_before ? g_before(static_cast<LLMoneyEvent>(t), f, to, v) : true;
        });
    }

    void api_money_listen_after_event(LLMoneyCallback callback)
    {
        g_after = callback;
        if (g_afterHooked || !moneyBackendReady()) return;
        g_afterHooked = true;
        LLMoney_ListenAfterEvent([](::LLMoneyEvent t, std::string f, std::string to, long long v)
        {
            return g_after ? g_after(static_cast<LLMoneyEvent>(t), f, to, v) : true;
        });
    }

    void moneyOnRustModGone(RustMod* mod)
    {
        if (!mod) return;
        void const* base = mod->lib.handle();
        if (addressOwnedBy(base, reinterpret_cast<void const*>(g_before))) g_before = nullptr;
        if (addressOwnedBy(base, reinterpret_cast<void const*>(g_after))) g_after = nullptr;
        // 蹦床留着不动：LegacyMoney 没有反注册。回调为空时它返回 true
        // （= 不取消），这是正确的中立答案。
    }

    void api_money_ranking(unsigned short num, void* ctx, LeviRsStrSink sink)
    {
        if (!moneyBackendReady()) return;
        for (auto const& [x, m] : LLMoney_Ranking(num))
        {
            sink(ctx, x + ":" + snbtNum(m));
        }
    }
}