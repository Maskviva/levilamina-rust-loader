// Client-only bridge functions (LEVI_RS_TARGET_CLIENT).
// All callbacks run on the CLIENT THREAD (KeyRegistry dispatches there).
// Key binding lifetime: KeyRegistry owns the KeyHandle; we hold a raw
// pointer to it inside a ClientKeyEntry. "Unregister" marks the entry
// dead (handlers become no-ops) and frees the entry.
#include "bridge/Api.h"
#include "bridge/Common.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "ll/api/input/KeyHandle.h"
#include "ll/api/input/KeyRegistry.h"
#include "ll/api/service/TargetedBedrock.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/deps/input/enums/FocusImpact.h"
#include "mc/world/actor/player/Player.h"

#include "RustMod.h"

namespace levi_rs::bridge
{
    namespace
    {
        struct ClientKeyEntry
        {
            std::string name;
            ll::input::KeyHandle* handle;
            LeviRsKeyCb down_cb;
            LeviRsKeyCb up_cb;
            void* user;
            std::shared_ptr<bool> alive;
            /** 只作身份用，绝不解引用——用来在它的 mod 卸载时摘掉这个绑定。 */
            RustMod* owner = nullptr;
        };

        /**
         * 所有还活着的绑定，好让卸载能够到那些 mod 忘了反注册的。
         *
         * KeyRegistry 的 handler 比 mod 的 dylib 活得久：它捕获的是裸的
         * down_cb/up_cb 函数指针，而且注册之后没有任何办法摘掉。`alive` 是让
         * 它们变成 no-op 的唯一手段，而在这张表之前，除了显式
         * client_unregister_key 之外没有任何东西会把它置 false。一个注册了热
         * 键然后被卸载的 mod，会留下一个仍然武装着、指向未映射内存的 handler。
         *
         * 仅客户端线程（KeyRegistry 在那里派发，注册也来自同一线程），无需加锁。
         */
        std::vector<ClientKeyEntry*>& liveEntries()
        {
            static std::vector<ClientKeyEntry*> entries;
            return entries;
        }

        /** 拆一个：让 handler 变 no-op，释放条目。 */
        void destroyEntry(ClientKeyEntry* entry)
        {
            *entry->alive = false;
            delete entry;
        }

        LeviRsFocusImpact toAbiFocus(::FocusImpact fi)
        {
            return static_cast<LeviRsFocusImpact>(static_cast<int>(fi));
        }
    } // namespace

    bool api_client_get_local_player(void* ctx, LeviRsStrSink sink)
    {
        LEVI_RS_API_GUARD_BEGIN
        if (!sink) return false;
        auto ci = ll::service::getClientInstance();
        if (!ci) return false;
        auto* player = ci->getLocalPlayer();
        if (!player) return false;
        sink(ctx, player->getRealName());
        return true;
        LEVI_RS_API_GUARD_END
    }

    bool api_client_is_in_level()
    {
        LEVI_RS_API_GUARD_BEGIN
        return ll::service::getMultiPlayerLevel() != nullptr;
        LEVI_RS_API_GUARD_END
    }

    bool api_client_get_screen_name(void* ctx, LeviRsStrSink sink)
    {
        LEVI_RS_API_GUARD_BEGIN
            // ClientInstance has no stable getScreenName() accessor in current LL headers.
            (void)
        ctx;
        (void)sink;
        return false;
        LEVI_RS_API_GUARD_END
    }

    LeviRsKeyHandle api_client_register_key(
        LeviRsModHandle modHandle,
        LeviRsStr name,
        int32_t const* keyCodes,
        int32_t keyCount,
        bool allowRemap,
        LeviRsKeyCb downCb,
        LeviRsKeyCb upCb,
        void* user
    )
    {
        LEVI_RS_API_GUARD_BEGIN
            auto* mod = asMod(modHandle);
        if (!mod || !name || keyCount <= 0) return nullptr;

        auto entry = std::make_unique<ClientKeyEntry>();
        entry->name = std::string{name};
        entry->down_cb = downCb;
        entry->up_cb = upCb;
        entry->user = user;
        entry->alive = std::make_shared<bool>(true);

        std::vector<int> keys(keyCodes, keyCodes + keyCount);
        auto modWeak = std::weak_ptr<ll::mod::Mod>(mod->shared_from_this());

        auto& handle = ll::input::KeyRegistry::getInstance().getOrCreateKey(
            entry->name, keys, allowRemap, modWeak
        );
        entry->handle = &handle;

        auto alive = entry->alive;
        auto downCbCapture = downCb;
        auto upCbCapture = upCb;
        auto userCapture = user;

        handle.registerButtonDownHandler(
            [alive, downCbCapture, userCapture](
            ::FocusImpact fi, ::IClientInstance&
        )
            {
                if (!*alive || !downCbCapture) return;
                downCbCapture(userCapture, /*Down=*/1, toAbiFocus(fi));
            }
        );
        handle.registerButtonUpHandler(
            [alive, upCbCapture, userCapture](
            ::FocusImpact fi, ::IClientInstance&
        )
            {
                if (!*alive || !upCbCapture) return;
                upCbCapture(userCapture, /*Up=*/0, toAbiFocus(fi));
            }
        );

        entry->owner = mod;
        auto* raw = entry.release();
        liveEntries().push_back(raw);
        return reinterpret_cast<LeviRsKeyHandle>(raw);
        LEVI_RS_API_GUARD_END
    }

    bool api_client_unregister_key(LeviRsKeyHandle handle)
    {
        LEVI_RS_API_GUARD_BEGIN
        if (!handle) return false;
        auto* entry = reinterpret_cast<ClientKeyEntry*>(handle);
        auto& live = liveEntries();
        auto it = std::find(live.begin(), live.end(), entry);
        // 不在表里说明已经拆过了（重复反注册，或者它的 mod 先卸载了）。
        // 原来这里是无条件 delete —— 重复反注册就是 double free。
        if (it == live.end()) return false;
        live.erase(it);
        destroyEntry(entry);
        return true;
        LEVI_RS_API_GUARD_END
    }

    void clientOnRustModGone(RustMod* mod)
    {
        if (!mod) return;
        auto& live = liveEntries();
        for (auto it = live.begin(); it != live.end();)
        {
            if ((*it)->owner != mod)
            {
                ++it;
                continue;
            }
            destroyEntry(*it);
            it = live.erase(it);
        }
    }

    bool api_client_get_key_codes(LeviRsKeyHandle handle, void* ctx, LeviRsStrSink sink)
    {
        LEVI_RS_API_GUARD_BEGIN
        if (!handle || !sink) return false;
        auto* entry = reinterpret_cast<ClientKeyEntry*>(handle);
        if (!entry->handle) return false;
        auto const& codes = entry->handle->getKeyCodes();
        std::string out = "[";
        for (size_t i = 0; i < codes.size(); ++i)
        {
            if (i) out += ',';
            out += snbtNum(codes[i]);
        }
        out += "]";
        sink(ctx, out);
        return true;
        LEVI_RS_API_GUARD_END
    }
} // namespace levi_rs::bridge
