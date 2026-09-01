#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "ll/api/event/ListenerBase.h"
#include "ll/api/mod/Manifest.h"
#include "ll/api/mod/Mod.h"
#include "ll/api/utils/SystemUtils.h"

#include "LeviRsAbi.h"

namespace levi_rs
{
    inline constexpr std::string_view RustModManagerName = "rust";

    /**
     * A mod of type "rust": a plain Rust cdylib loaded through the LeviRsAbi
     * function-table contract instead of the C++ native mod ABI.
     */
    class RustMod : public ll::mod::Mod, public std::enable_shared_from_this<RustMod>
    {
    public:
        explicit RustMod(ll::mod::Manifest manifest) : Mod(std::move(manifest))
        {
        }

        ll::sys_utils::DynamicLibrary lib;
        LeviRsModVTable vtable{};
        /** Keeps DynamicListeners alive; cleared on unload. */
        /**
         * 保持 DynamicListener 存活；卸载时清空。
         *
         * 用进程内单调 id 索引，**不是**监听器的地址。地址是最直觉的句柄，也
         * 是不安全的那个：退订会释放监听器，下一次订阅完全可能落在同一块内存
         * 上，于是一个 mod 忘了丢弃的过期句柄会匹配上另一条订阅 —— 悄无声息
         * 地退掉别人的监听器。id 永不复用，过期句柄只会匹配失败。
         */
        struct ListenerSlot
        {
            std::uint64_t id;
            std::shared_ptr<ll::event::ListenerBase> listener;
        };

        std::vector<ListenerSlot> listeners;
        /** Muted when disabled so already-registered commands become no-ops. */
        bool commandsMuted = false;
    };
} // namespace levi_rs
