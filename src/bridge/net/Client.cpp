// Client-only bridge functions (LEVI_RS_TARGET_CLIENT).
// All callbacks run on the CLIENT THREAD (KeyRegistry dispatches there).
// Key binding lifetime: KeyRegistry owns the KeyHandle; we hold a raw
// pointer to it inside a ClientKeyEntry. "Unregister" marks the entry
// dead (handlers become no-ops) and frees the entry.
#include "bridge/Api.h"
#include "bridge/Common.h"

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
        };

        LeviRsFocusImpact toAbiFocus(::FocusImpact fi)
        {
            return static_cast<LeviRsFocusImpact>(static_cast<int>(fi));
        }
    } // namespace

    bool api_client_get_local_player(void* ctx, LeviRsStrSink sink)
    {
        if (!sink) return false;
        auto ci = ll::service::getClientInstance();
        if (!ci) return false;
        auto* player = ci->getLocalPlayer();
        if (!player) return false;
        sink(ctx, player->getRealName());
        return true;
    }

    bool api_client_is_in_level()
    {
        return ll::service::getMultiPlayerLevel() != nullptr;
    }

    bool api_client_get_screen_name(void* ctx, LeviRsStrSink sink)
    {
        // ClientInstance has no stable getScreenName() accessor in current LL headers.
        (void)ctx;
        (void)sink;
        return false;
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

        return reinterpret_cast<LeviRsKeyHandle>(entry.release());
    }

    bool api_client_unregister_key(LeviRsKeyHandle handle)
    {
        if (!handle) return false;
        auto* entry = reinterpret_cast<ClientKeyEntry*>(handle);
        *entry->alive = false;
        delete entry;
        return true;
    }

    bool api_client_get_key_codes(LeviRsKeyHandle handle, void* ctx, LeviRsStrSink sink)
    {
        if (!handle || !sink) return false;
        auto* entry = reinterpret_cast<ClientKeyEntry*>(handle);
        if (!entry->handle) return false;
        auto const& codes = entry->handle->getKeyCodes();
        std::string out = "[";
        for (size_t i = 0; i < codes.size(); ++i)
        {
            if (i) out += ',';
            out += std::to_string(codes[i]);
        }
        out += "]";
        sink(ctx, out);
        return true;
    }
} // namespace levi_rs::bridge
