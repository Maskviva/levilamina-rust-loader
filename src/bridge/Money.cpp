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
#include "Api.h"
#include "BridgeApi.h"
#include "bridge/MoneyGuard.h"

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

    static LLMoneyCallback g_before = nullptr;
    static LLMoneyCallback g_after = nullptr;

    void api_money_listen_before_event(LLMoneyCallback callback)
    {
        // Registration is a no-op without the backend: there's no event
        // source to hook, and LLMoney_ListenBeforeEvent itself is a
        // delay-loaded symbol. Stash the callback anyway so a later
        // (re)registration path stays consistent, but skip the FFI call.
        g_before = callback;
        if (!moneyBackendReady()) return;
        LLMoney_ListenBeforeEvent([](::LLMoneyEvent t, std::string f, std::string to, long long v)
        {
            return g_before ? g_before(static_cast<LLMoneyEvent>(t), f, to, v) : true;
        });
    }

    void api_money_listen_after_event(LLMoneyCallback callback)
    {
        g_after = callback;
        if (!moneyBackendReady()) return;
        LLMoney_ListenAfterEvent([](::LLMoneyEvent t, std::string f, std::string to, long long v)
        {
            return g_after ? g_after(static_cast<LLMoneyEvent>(t), f, to, v) : true;
        });
    }

    void api_money_ranking(unsigned short num, void* ctx, LeviRsStrSink sink)
    {
        if (!moneyBackendReady()) return;
        for (auto const& [x, m] : LLMoney_Ranking(num))
        {
            sink(ctx, x + ":" + std::to_string(m));
        }
    }
}