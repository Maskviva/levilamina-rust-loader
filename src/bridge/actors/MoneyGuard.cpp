#include "bridge/MoneyGuard.h"

#include <atomic>

#include "ll/api/memory/Symbol.h"
#include "ll/api/mod/Mod.h"
#include "ll/api/mod/ModManagerRegistry.h"
#include "ll/api/mod/NativeMod.h"

namespace levi_rs::bridge
{
    namespace
    {
        // LegacyMoney's LeviLamina manifest name.
        constexpr std::string_view kMoneyModName = "LegacyMoney";

        // One stable exported symbol from LegacyMoney. It's declared
        // `extern "C"`, so on x64 MSVC the export name is undecorated —
        // exactly this string. If LLMoney_Get is present the rest of the
        // LLMoney_* family is too (they ship together).
        constexpr std::string_view kProbeSymbol = "LLMoney_Get";

        // -1 = not probed yet, 0 = symbol missing, 1 = symbol present.
        // The DLL that owns the symbol can't be swapped mid-process, so once
        // we've observed the symbol we never need to resolve it again.
        std::atomic<int> gSymbolState{-1};

        // Warn only once per process regardless of which check failed first.
        std::atomic_flag gWarned = ATOMIC_FLAG_INIT;

        bool symbolPresent()
        {
            int cached = gSymbolState.load(std::memory_order_acquire);
            if (cached >= 0)
            {
                return cached == 1;
            }
            // resolve() with disableErrorOutput=true returns nullptr on miss
            // and does NOT spam the log — we own the messaging here.
            void* addr = ll::memory::SymbolView{kProbeSymbol}.resolve(true);
            int result = addr != nullptr ? 1 : 0;
            // Benign race: two threads may both resolve; they compute the
            // same answer, last write wins.
            gSymbolState.store(result, std::memory_order_release);
            return result == 1;
        }

        bool modLoadedAndEnabled()
        {
            auto& registry = ll::mod::ModManagerRegistry::getInstance();
            if (!registry.hasMod(kMoneyModName))
            {
                return false;
            }
            auto mod = registry.getMod(kMoneyModName);
            return mod && mod->isEnabled();
        }

        void warnOnce(std::string_view reason)
        {
            if (gWarned.test_and_set(std::memory_order_relaxed))
            {
                return; // already warned
            }
            if (auto self = ll::mod::NativeMod::current())
            {
                self->getLogger().warn(
                    "找不到可用的 LLMoney 后端（{}）。money::* 接口本次全部空转："
                    "读取返回 0、写入返回失败。请检查是否安装并启用了 LegacyMoney"
                    "（模组名 \"{}\"）。",
                    reason,
                    kMoneyModName
                );
            }
        }
    } // namespace

    bool moneyBackendReady() noexcept
    {
        // Cheap check first (mod list + state), then the memoized symbol
        // probe. Either failing means "not ready".
        if (!modLoadedAndEnabled())
        {
            warnOnce("模组列表里没有已启用的 LegacyMoney");
            return false;
        }
        if (!symbolPresent())
        {
            warnOnce("LegacyMoney 已加载，但解析不到导出符号 LLMoney_Get（版本不匹配或 DLL 损坏？）");
            return false;
        }
        return true;
    }
} // namespace levi_rs::bridge
