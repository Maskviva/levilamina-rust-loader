#include <memory>
#include <string>

#include "ll/api/event/EventBus.h"
#include "ll/api/mod/ModManagerRegistry.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/mod/RegisterHelper.h"

// The debug /levirs command uses the command registrar (server-only).
#ifndef LEVI_RS_TARGET_CLIENT
#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/command/runtime/ParamKind.h"
#include "ll/api/command/runtime/RuntimeCommand.h"
#include "ll/api/command/runtime/RuntimeOverload.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#endif

#include "LeviRsAbi.h"
#include "RustModManager.h"

// MoreDimensions 自初始化需要。只在服务器构建里包含（客户端构建不定义
// LEVI_RS_FEATURE_MORE_DIMENSIONS，这段代码会被整体剔除）。
#ifdef LEVI_RS_FEATURE_MORE_DIMENSIONS
#include "more_dimensions/CustomDimensionManager.h"
#endif

namespace levi_rs
{
    class LoaderMod
    {
    public:
        static LoaderMod& getInstance()
        {
            static LoaderMod instance;
            return instance;
        }

        [[nodiscard]] ll::mod::NativeMod& getSelf() const { return *ll::mod::NativeMod::current(); }

        bool load()
        {
            auto& logger = getSelf().getLogger();
            if (!leviRsVerifyStrLayout())
            {
                logger.error(
                    "std::string_view 的内存布局跟预期的 {{pointer,size_t}} 不一致——"
                    "Rust 那边独立声明的 repr(C) 镜像结构会跟这里的真实布局对不上，"
                    "继续跑下去会导致跨语言传字符串时读到错位的指针/长度。拒绝加载。"
                );
                return false;
            }
            if (!ll::mod::ModManagerRegistry::getInstance().addManager(std::make_shared<RustModManager>()))
            {
                logger.error("failed to register the 'rust' mod manager");
                return false;
            }
            logger.info("levilamina-rust-loader ready (ABI v{})", LEVI_RS_ABI_VERSION);

            return true;
        }

        bool enable()
        {
#ifndef LEVI_RS_TARGET_CLIENT
            registerDebugCommand();
#endif
#ifdef LEVI_RS_FEATURE_MORE_DIMENSIONS
            // MoreDimensions 在 C++ 侧于加载期自初始化：建好所有 hook、读好维度
            // 配置，全程不依赖 Rust SDK 是否调用任何 md_* 接口。Rust 的
            // `more_dimensions` cargo feature 只是「入口」——它把这组 FFI 暴露给
            // Rust mod 用，而不是 C++ 部分的开关。服务器构建永远启用，客户端构建
            // 永远不包含。这里主动触发一次 getInstance()，确保即使没有 Rust mod
            // 调用维度 API，C++ 的钩子也始终在线。
            (void)more_dimensions::CustomDimensionManager::getInstance();
#endif
            return true;
        }

        bool disable() { return true; }

#ifndef LEVI_RS_TARGET_CLIENT
    private:
        void registerDebugCommand()
        {
            using namespace ll::command;
            auto& handle = CommandRegistrar::getServerInstance().getOrCreateCommand(
                "levirs",
                "levilamina-rs loader utilities",
                CommandPermissionLevel::Host
            );
            handle.runtimeOverload().optional("args", ParamKind::RawText).execute(
                [](CommandOrigin const&, CommandOutput& output, RuntimeCommand const& rt)
                {
                    std::string sub;
                    if (auto const& p = rt["args"]; p.hold(ParamKind::RawText))
                    {
                        sub = p.get<ParamKind::RawText>().mText;
                    }
                    if (sub == "events")
                    {
                        size_t n = 0;
                        for (auto&& [modName, id] : ll::event::EventBus::getInstance().events())
                        {
                            output.success(std::string{id.name} + "  (from " + std::string{modName} + ")");
                            n++;
                        }
                        output.success("total: " + std::to_string(n) + " event(s)");
                    }
                    else if (sub == "abi")
                    {
                        output.success("levilamina-rs ABI v" + std::to_string(LEVI_RS_ABI_VERSION));
                    }
                    else
                    {
                        output.success("usage: /levirs events | abi");
                    }
                }
            );
        }
#endif // !LEVI_RS_TARGET_CLIENT
    };
} // namespace levi_rs

LL_REGISTER_MOD(levi_rs::LoaderMod, levi_rs::LoaderMod::getInstance());
