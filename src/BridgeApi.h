#pragma once

#include "LeviRsAbi.h"

namespace levi_rs
{
    class RustMod;

    /** The singleton function table handed to every Rust mod. */
    const LeviRsApi* getBridgeApi();

    namespace detail
    {
        /** Release everything owned on behalf of a mod being unloaded:
         * command bindings (nulled), pending form tickets (cleared), and any
         * KvDb handles left open (force-closed with a warning). */
        void onRustModGone(RustMod* mod);

        /**
         * 该 mod 提供的车道里，有没有哪条此刻正停在调用中；返回车道名，没有则
         * nullptr。**卸载前必须查。**
         *
         * 声明放在这里而不是 bridge/Api.h，是为了跟着 onRustModGone 走：
         * RustModManager.cpp 只包含 BridgeApi.h，桥接层的内部头对它不可见。
         *
         * 为什么需要它：车道的存活标志只能挡住「调用之前提供方已经走了」，挡不
         * 住检查与调用之间那个窗口。全部服务器线程调用挡住了**并发**卸载，挡不
         * 住**重入**卸载 —— 提供方的表项自己触发了一次命令派发，那条命令把提供
         * 方卸了，于是 FreeLibrary 发生在一个仍然停在提供方代码里的栈帧下面。
         */
        char const* laneBusyName(RustMod* mod);
    } // namespace detail
} // namespace levi_rs
