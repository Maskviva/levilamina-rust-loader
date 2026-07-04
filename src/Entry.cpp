#include <memory>
#include <string>

#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/command/runtime/ParamKind.h"
#include "ll/api/command/runtime/RuntimeCommand.h"
#include "ll/api/command/runtime/RuntimeOverload.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/mod/ModManagerRegistry.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/mod/RegisterHelper.h"

#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"

#include "LeviRsAbi.h"
#include "RustModManager.h"

namespace levi_rs {

class LoaderMod {
public:
    static LoaderMod& getInstance() {
        static LoaderMod instance;
        return instance;
    }

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return *ll::mod::NativeMod::current(); }

    bool load() {
        auto& logger = getSelf().getLogger();
        if (!ll::mod::ModManagerRegistry::getInstance().addManager(std::make_shared<RustModManager>())) {
            logger.error("failed to register the 'rust' mod manager");
            return false;
        }
        logger.info("levilamina-rust-loader ready (ABI v{})", LEVI_RS_ABI_VERSION);
        logger.info(
            std::string{R"(rust mods: manifest {"type": "rust", "dependencies": [{"name": "levilamina-rust-loader"}]})"}
        );
        return true;
    }

    bool enable() {
        registerDebugCommand();
        return true;
    }

    bool disable() { return true; }

private:
    void registerDebugCommand() {
        using namespace ll::command;
        auto& handle = CommandRegistrar::getServerInstance().getOrCreateCommand(
            "levirs",
            "levilamina-rs loader utilities",
            CommandPermissionLevel::Host
        );
        handle.runtimeOverload().optional("args", ParamKind::RawText).execute(
            [](CommandOrigin const&, CommandOutput& output, RuntimeCommand const& rt) {
                std::string sub;
                if (auto const& p = rt["args"]; p.hold(ParamKind::RawText)) {
                    sub = p.get<ParamKind::RawText>().mText;
                }
                if (sub == "events") {
                    size_t n = 0;
                    for (auto&& [modName, id] : ll::event::EventBus::getInstance().events()) {
                        output.success(std::string{id.name} + "  (from " + std::string{modName} + ")");
                        n++;
                    }
                    output.success("total: " + std::to_string(n) + " event(s)");
                } else if (sub == "abi") {
                    output.success("levilamina-rs ABI v" + std::to_string(LEVI_RS_ABI_VERSION));
                } else {
                    output.success("usage: /levirs events | abi");
                }
            }
        );
    }
};

} // namespace levi_rs

LL_REGISTER_MOD(levi_rs::LoaderMod, levi_rs::LoaderMod::getInstance());
