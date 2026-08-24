/**
 * ModControl.cpp — runtime load / unload / reload of rust mods, and the /llr
 * command that drives it.
 *
 * Design constraints that shaped this file:
 *
 *  1. LeviLamina has NO public runtime mod-loading API. ModManagerRegistry's
 *     loadMod/unloadMod/enableMod/disableMod are private (friended only to
 *     ModRegistrar and Mod). What we can reach is the protected ModManager
 *     interface we inherit, wrapped by RustModManager::controlLoad/controlUnload.
 *     So a mod brought up here lives in our manager's table but NOT in
 *     LeviLamina's own dependency graph — hence the manifest walking below.
 *
 *  2. No dll copying. `reload` is a state reset (re-run levi_rs_main, re-read
 *     config), not a code hot-swap. Swapping in a freshly built dll is the
 *     `unload` → rebuild → `load` path, and even that is at the mercy of
 *     Windows' FreeLibrary refcounting — see checkImageSwapped().
 *
 *  3. `reload` is gated on `"reload_safe": true`, `unload` refuses when
 *     something depends on the target unless --cascade is given.
 */
#include "ModControl.h"

#include <algorithm>
#include <deque>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ll/api/Config.h" // pulls nlohmann/json.hpp
#include "ll/api/io/FileUtils.h"
#include "ll/api/mod/Mod.h"
#include "ll/api/mod/ModManagerRegistry.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/reflection/Deserialization.h"
#include "ll/api/utils/StringUtils.h"

#ifndef LEVI_RS_TARGET_CLIENT
#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/command/runtime/ParamKind.h"
#include "ll/api/command/runtime/RuntimeCommand.h"
#include "ll/api/command/runtime/RuntimeOverload.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#endif

#include "RustMod.h"
#include "RustModManager.h"

namespace levi_rs::mod_control
{
    namespace fs = std::filesystem;

    namespace
    {
        ll::io::Logger& log()
        {
            static auto* logger = &ll::mod::NativeMod::current()->getLogger();
            return *logger;
        }

        fs::path manifestPathOf(std::string_view name)
        {
            return ll::mod::getModsRoot() / ll::string_utils::sv2u8sv(name) / u8"manifest.json";
        }

        /** Parse a manifest.json into a Candidate. `err` is set on failure. */
        Candidate parseCandidate(fs::path const& file, std::string const& dirName)
        {
            Candidate c;
            c.name = dirName;

            auto text = ll::file_utils::readFile(file);
            if (!text)
            {
                c.problem = "manifest.json 读不出来";
                return c;
            }
            nlohmann::json j;
            try
            {
                // (text, cb, allow_exceptions, ignore_comments) — same call
                // shape LeviLamina's own config loader uses.
                j = nlohmann::json::parse(*text, nullptr, true, true);
            }
            catch (std::exception const& e)
            {
                c.problem = std::string{"manifest.json 不是合法 JSON: "} + e.what();
                return c;
            }
            if (!j.is_object())
            {
                c.problem = "manifest.json 顶层不是对象";
                return c;
            }
            if (!j.contains("type") || !j["type"].is_string() || j["type"].get<std::string>() != "rust")
            {
                c.problem = "not-rust"; // sentinel: filtered out silently, not an error to report
                return c;
            }
            if (j.contains("name") && j["name"].is_string())
            {
                c.name = j["name"].get<std::string>();
            }
            if (j.contains("entry") && j["entry"].is_string())
            {
                c.entry = j["entry"].get<std::string>();
            }
            else
            {
                c.problem = "manifest.json 缺少 entry";
                return c;
            }
            if (j.contains("version") && j["version"].is_string())
            {
                c.version = j["version"].get<std::string>();
            }
            // reload_safe: a plain top-level bool. LeviLamina's deserializer
            // iterates ll::mod::Manifest's members and looks each one up in the
            // JSON — it never iterates the JSON's own keys — so this extra field
            // is invisible to LeviLamina and cannot break normal startup loading.
            if (j.contains("reload_safe"))
            {
                if (j["reload_safe"].is_boolean())
                {
                    c.reloadSafe = j["reload_safe"].get<bool>();
                }
                else
                {
                    c.problem = "reload_safe 必须是 true/false";
                }
            }
            if (j.contains("dependencies") && j["dependencies"].is_array())
            {
                for (auto const& d : j["dependencies"])
                {
                    if (d.is_object() && d.contains("name") && d["name"].is_string())
                    {
                        c.dependencies.push_back(d["name"].get<std::string>());
                    }
                    else if (d.is_string())
                    {
                        c.dependencies.push_back(d.get<std::string>());
                    }
                }
            }
            // The directory name is what getModsRoot()/<dir>/entry resolves
            // against, so a manifest whose "name" disagrees with its folder
            // cannot be loaded by path at all. Catch it here rather than as a
            // confusing "file not found" later.
            if (c.name != dirName)
            {
                c.problem = "manifest 里的 name (\"" + c.name + "\") 和目录名 (\"" + dirName + "\") 不一致";
            }
            return c;
        }

        /** Every loaded mod's name -> its declared dependencies. */
        std::unordered_map<std::string, std::vector<std::string>> loadedDependencyMap()
        {
            std::unordered_map<std::string, std::vector<std::string>> out;

            auto absorb = [&out](ll::mod::Mod& mod)
            {
                auto const& mf = mod.getManifest();
                auto& deps = out[mod.getName()];
                if (mf.dependencies)
                {
                    for (auto const& d : *mf.dependencies)
                    {
                        deps.push_back(d.name);
                    }
                }
            };

            // The registry covers everything LeviLamina loaded at startup,
            // native C++ mods included — which matters, because a C++ mod can
            // depend on a rust mod just as easily as another rust mod can.
            for (auto& mod : ll::mod::ModManagerRegistry::getInstance().mods())
            {
                absorb(mod);
            }
            // Union with our own table: a mod brought up by /llr load may not
            // be in the registry's view, and we must not lose it here.
            if (auto* mgr = RustModManager::instance())
            {
                for (auto& mod : mgr->mods())
                {
                    absorb(mod);
                }
            }
            return out;
        }
    } // namespace

    std::vector<Candidate> rescan()
    {
        std::vector<Candidate> found;
        std::error_code ec;
        auto root = ll::mod::getModsRoot();
        if (!fs::is_directory(root, ec)) return found;
        for (auto const& entry : fs::directory_iterator(root, ec))
        {
            if (ec) break;
            if (!entry.is_directory(ec)) continue;
            auto file = entry.path() / u8"manifest.json";
            if (!fs::is_regular_file(file, ec)) continue;

            auto dirName = ll::string_utils::u8str2str(entry.path().filename().u8string());
            auto c = parseCandidate(file, dirName);
            if (c.problem == "not-rust") continue; // other managers' business
            found.push_back(std::move(c));
        }
        std::sort(found.begin(), found.end(),
                  [](Candidate const& a, Candidate const& b) { return a.name < b.name; });
        return found;
    }

    ll::Expected<Candidate> readCandidate(std::string_view name)
    {
        // Deliberately bypasses the cache: `/llr load` must see the manifest as
        // it is on disk right now, not as it was at the last `/llr list`.
        auto file = manifestPathOf(name);
        std::error_code ec;
        if (!fs::is_regular_file(file, ec))
        {
            return ll::makeStringError(
                "找不到 mods/" + std::string(name) + "/manifest.json"
            );
        }
        auto c = parseCandidate(file, std::string(name));
        if (c.problem == "not-rust")
        {
            return ll::makeStringError("'" + std::string(name) + "' 不是 rust 类型的 mod");
        }
        if (!c.problem.empty())
        {
            return ll::makeStringError("'" + std::string(name) + "': " + c.problem);
        }
        return c;
    }

    ll::Expected<ll::mod::Manifest> readManifest(std::string_view name)
    {
        auto file = manifestPathOf(name);
        auto text = ll::file_utils::readFile(file);
        if (!text)
        {
            return ll::makeStringError("读不出 mods/" + std::string(name) + "/manifest.json");
        }
        nlohmann::json j;
        try
        {
            j = nlohmann::json::parse(*text, nullptr, true, true);
        }
        catch (std::exception const& e)
        {
            return ll::makeStringError(std::string{"manifest.json 解析失败: "} + e.what());
        }
        ll::mod::Manifest manifest;
        // Reuse LeviLamina's own reflection deserializer so a hot-loaded mod
        // gets byte-identical manifest semantics to a startup-loaded one.
        if (auto e = ll::reflection::deserialize<ll::mod::Manifest>(manifest, j); !e)
        {
            return ll::forwardError(e.error());
        }
        return manifest;
    }

    bool isRustMod(std::string_view name)
    {
        auto* mgr = RustModManager::instance();
        return mgr && mgr->hasMod(name);
    }

    bool isLoadedAnywhere(std::string_view name)
    {
        if (isRustMod(name)) return true;
        return ll::mod::ModManagerRegistry::getInstance().hasMod(name);
    }

    std::vector<std::string> dependentsOf(std::string_view name)
    {
        auto deps = loadedDependencyMap();
        const std::string target{name};

        // reverse edges: dependency -> those depending on it
        std::unordered_map<std::string, std::vector<std::string>> dependedBy;
        for (auto const& [mod, list] : deps)
        {
            for (auto const& d : list)
            {
                dependedBy[d].push_back(mod);
            }
        }

        // transitive closure of everything that (indirectly) needs `target`
        std::unordered_set<std::string> affected;
        std::deque<std::string> queue{target};
        while (!queue.empty())
        {
            auto cur = queue.front();
            queue.pop_front();
            auto it = dependedBy.find(cur);
            if (it == dependedBy.end()) continue;
            for (auto const& dep : it->second)
            {
                if (dep == target) continue;
                if (affected.insert(dep).second) queue.push_back(dep);
            }
        }

        // Order them so a mod always comes before anything it depends on —
        // that is the order they must be unloaded in. Kahn's algorithm over
        // the induced subgraph; a plain BFS depth would get diamonds wrong.
        std::unordered_map<std::string, int> indeg;
        for (auto const& m : affected) indeg[m] = 0;
        for (auto const& m : affected)
        {
            for (auto const& d : deps[m])
            {
                if (affected.count(d)) indeg[d]++;
            }
        }
        std::vector<std::string> ready;
        for (auto const& [m, n] : indeg)
        {
            if (n == 0) ready.push_back(m);
        }
        std::sort(ready.begin(), ready.end()); // deterministic output

        std::vector<std::string> ordered;
        while (!ready.empty())
        {
            auto cur = ready.back();
            ready.pop_back();
            ordered.push_back(cur);
            for (auto const& d : deps[cur])
            {
                if (!affected.count(d)) continue;
                if (--indeg[d] == 0) ready.push_back(d);
            }
        }
        // A dependency cycle would leave nodes unemitted. Append them rather
        // than dropping them silently; the caller still needs to know.
        for (auto const& m : affected)
        {
            if (std::find(ordered.begin(), ordered.end(), m) == ordered.end())
            {
                ordered.push_back(m);
            }
        }
        return ordered;
    }

#ifndef LEVI_RS_TARGET_CLIENT
    namespace
    {
        /**
         * Base address each mod had when we last unloaded it.
         *
         * The development workflow you asked for is `unload` → rebuild →
         * `load`, which spans two commands, so the reload-path check alone
         * would never see it. Stashing the address here lets `/llr load`
         * answer the only question that matters after a rebuild: did the new
         * dll actually get mapped, or is Windows still handing back the old
         * image?
         */
        std::unordered_map<std::string, void const*> gLastUnloadBase;

        /**
         * Windows FreeLibrary is refcounted, not a hard unmap. If a TLS
         * destructor never ran, a COM object is still live, or the CRT retained
         * a pointer into the image, the dll stays mapped and the next
         * LoadLibrary hands back the SAME base address — meaning the freshly
         * built dll on disk was NOT picked up and the server is still running
         * the old code, silently.
         *
         * Comparing the base address across an unload/load cycle is the only
         * cheap way to catch that, and it is precisely the failure mode of the
         * "rebuild and /llr load" development workflow.
         */
        void checkImageSwapped(CommandOutput& output, std::string const& name, void const* before)
        {
            auto* mgr = RustModManager::instance();
            if (!mgr || before == nullptr) return;
            void const* after = mgr->moduleBase(name);
            if (after != nullptr && after == before)
            {
                output.error(
                    "⚠ '" + name + "' 重新加载后 dll 基址没变 —— Windows 没有真正卸载这个映像"
                    "（FreeLibrary 是引用计数的）。如果你刚重新编译过，服务器现在跑的**还是旧代码**。"
                    "常见原因：mod 自己 spawn 的线程没 join、TLS 析构没跑完、还有 handle 没关。"
                    "确认新代码生效的唯一可靠办法是重启服务器。"
                );
            }
        }

        struct ParsedArgs
        {
            std::string sub;
            std::string name;
            bool cascade = false;
            bool unknownFlag = false;
            std::string unknownFlagText;
        };

        ParsedArgs parseArgs(std::string const& raw)
        {
            ParsedArgs a;
            std::vector<std::string> tokens;
            std::string cur;
            for (char ch : raw)
            {
                if (ch == ' ' || ch == '\t')
                {
                    if (!cur.empty()) tokens.push_back(std::exchange(cur, {}));
                }
                else
                {
                    cur += ch;
                }
            }
            if (!cur.empty()) tokens.push_back(std::move(cur));

            for (auto const& t : tokens)
            {
                if (t.rfind("--", 0) == 0)
                {
                    if (t == "--cascade")
                    {
                        a.cascade = true;
                    }
                    else
                    {
                        a.unknownFlag = true;
                        a.unknownFlagText = t;
                    }
                }
                else if (a.sub.empty())
                {
                    a.sub = t;
                }
                else if (a.name.empty())
                {
                    a.name = t;
                }
            }
            return a;
        }

        void cmdList(CommandOutput& output)
        {
            // Explicitly a DISK RESCAN, not a cache dump: this is the command
            // that makes a newly added / newly built mod directory visible.
            auto found = rescan();
            auto* mgr = RustModManager::instance();

            output.success("── rust mods (已重新扫描 mods/ 目录) ──");
            if (found.empty())
            {
                output.success("(没有找到任何 \"type\": \"rust\" 的 mod)");
                return;
            }
            size_t loaded = 0;
            for (auto const& c : found)
            {
                const bool isLoaded = mgr && mgr->hasMod(c.name);
                if (isLoaded) loaded++;

                std::string line = c.name;
                if (!c.version.empty()) line += " v" + c.version;
                line += isLoaded ? "  [已加载" : "  [未加载";
                if (isLoaded)
                {
                    auto mod = mgr->getMod(c.name);
                    line += (mod && mod->isEnabled()) ? "/已启用" : "/已禁用";
                }
                line += "]";
                line += c.reloadSafe ? "  [reload_safe]" : "  [不可 reload]";
                if (!c.problem.empty()) line += "  ⚠ " + c.problem;
                output.success(line);
            }
            output.success(
                "共 " + std::to_string(found.size()) + " 个，已加载 " + std::to_string(loaded) + " 个"
            );
        }

        void cmdLoad(CommandOutput& output, std::string const& name)
        {
            auto* mgr = RustModManager::instance();
            if (!mgr)
            {
                output.error("rust mod manager 还没就绪");
                return;
            }
            if (mgr->hasMod(name))
            {
                output.error("'" + name + "' 已经加载了。要换代码请先 /llr unload " + name);
                return;
            }
            if (isLoadedAnywhere(name))
            {
                output.error("'" + name + "' 已被别的 mod manager 加载，这里不接管");
                return;
            }

            auto cand = readCandidate(name);
            if (!cand)
            {
                output.error(cand.error().message());
                return;
            }
            // Refuse rather than load something whose own dependencies are
            // missing — it would fail later, from inside the mod, much less
            // legibly than it does here.
            std::vector<std::string> missing;
            for (auto const& d : cand->dependencies)
            {
                if (!isLoadedAnywhere(d)) missing.push_back(d);
            }
            if (!missing.empty())
            {
                std::string list;
                for (auto const& m : missing)
                {
                    if (!list.empty()) list += ", ";
                    list += m;
                }
                output.error("'" + name + "' 的依赖还没加载: " + list);
                return;
            }

            auto manifest = readManifest(name);
            if (!manifest)
            {
                output.error(manifest.error().message());
                return;
            }
            if (auto e = mgr->controlLoad(std::move(*manifest)); !e)
            {
                output.error("加载 '" + name + "' 失败: " + e.error().message());
                return;
            }
            output.success("已加载并启用 '" + name + "'");
            // If this name was unloaded earlier in the session, the base
            // address tells us whether the rebuilt dll really replaced the old
            // image, or whether Windows just handed the old one back.
            if (auto it = gLastUnloadBase.find(name); it != gLastUnloadBase.end())
            {
                checkImageSwapped(output, name, it->second);
                gLastUnloadBase.erase(it);
            }
            if (!cand->reloadSafe)
            {
                output.success(
                    "提示：'" + name + "' 的 manifest 没写 \"reload_safe\": true，"
                    "所以 /llr reload 对它不开放，只能 unload + load"
                );
            }
        }

        /** Shared unload path. Returns false if nothing was done. */
        bool doUnload(CommandOutput& output, std::string const& name, bool cascade,
                      std::vector<std::string>* unloadedOut)
        {
            auto* mgr = RustModManager::instance();
            if (!mgr || !mgr->hasMod(name))
            {
                output.error("'" + name + "' 没有被加载（rust mod）");
                return false;
            }

            auto dependents = dependentsOf(name);
            if (!dependents.empty())
            {
                std::string list;
                for (auto const& d : dependents)
                {
                    if (!list.empty()) list += ", ";
                    list += d;
                    if (!isRustMod(d)) list += "(C++)";
                }
                if (!cascade)
                {
                    // Default is refuse: unloading out from under a dependent
                    // leaves it holding function pointers into a freed dylib,
                    // and it will not find out until it next calls in.
                    output.error(
                        "拒绝卸载 '" + name + "'：还有 mod 依赖它 —— " + list
                        + "。确认要一起卸载就加 --cascade。"
                    );
                    return false;
                }
                // Cascade can only take down mods we own. A native C++ mod is
                // another manager's business and we have no way to unload it.
                std::vector<std::string> foreign;
                for (auto const& d : dependents)
                {
                    if (!isRustMod(d)) foreign.push_back(d);
                }
                if (!foreign.empty())
                {
                    std::string flist;
                    for (auto const& f : foreign)
                    {
                        if (!flist.empty()) flist += ", ";
                        flist += f;
                    }
                    output.error(
                        "--cascade 也做不了：依赖方里有非 rust 的 mod（" + flist
                        + "），本 loader 无权卸载它们。请先手动处理，或重启服务器。"
                    );
                    return false;
                }
            }

            // Dependents first, then the target: dependentsOf() already returns
            // them in an order where a mod precedes everything it depends on.
            std::vector<std::string> order = dependents;
            order.push_back(name);

            for (auto const& m : order)
            {
                if (!mgr->hasMod(m)) continue;
                gLastUnloadBase[m] = mgr->moduleBase(m);
                if (auto e = mgr->controlUnload(m); !e)
                {
                    output.error("卸载 '" + m + "' 失败: " + e.error().message());
                    output.error("已经卸载的部分不会自动恢复，请检查服务器状态。");
                    return false;
                }
                output.success("已卸载 '" + m + "'");
                if (unloadedOut) unloadedOut->push_back(m);
            }
            return true;
        }

        void cmdUnload(CommandOutput& output, std::string const& name, bool cascade)
        {
            if (doUnload(output, name, cascade, nullptr))
            {
                output.success(
                    "提示：要换成新编译的 dll，现在把文件替换掉，然后 /llr list 刷新列表，"
                    "再 /llr load " + name
                );
            }
        }

        void cmdReload(CommandOutput& output, std::string const& name, bool cascade)
        {
            auto* mgr = RustModManager::instance();
            if (!mgr || !mgr->hasMod(name))
            {
                output.error("'" + name + "' 没有被加载（rust mod）");
                return;
            }

            auto cand = readCandidate(name);
            if (!cand)
            {
                output.error(cand.error().message());
                return;
            }
            // The gate you asked for: reload is only for mods that have
            // declared they can survive it. Everything else gets the cold path.
            if (!cand->reloadSafe)
            {
                output.error(
                    "'" + name + "' 没有在 manifest.json 里声明 \"reload_safe\": true，不允许 reload。"
                );
                output.error(
                    "reload 会重跑 levi_rs_main，mod 必须自己保证：on_unload 里 join 掉所有线程、"
                    "关掉所有 handle、取消所有定时器（schedule_cancel），全局状态可重入。"
                    "确认做到了再加这个字段。在那之前请用 /llr unload " + name + " 然后 /llr load " + name + "。"
                );
                return;
            }

            // Cascade reloads its dependents as well, so they undergo exactly
            // the same reload the gate above exists to guard. Check them too
            // rather than reloading an unmarked mod through the back door.
            if (cascade)
            {
                for (auto const& dep : dependentsOf(name))
                {
                    if (!isRustMod(dep)) continue; // reported later by doUnload
                    auto dc = readCandidate(dep);
                    if (!dc || !dc->reloadSafe)
                    {
                        output.error(
                            "级联里的 '" + dep + "' 没有声明 \"reload_safe\": true，"
                            "不能跟着一起 reload。请改用 /llr unload " + name + " --cascade 再逐个 load。"
                        );
                        return;
                    }
                }
            }

            std::vector<std::string> unloaded;
            if (!doUnload(output, name, cascade, &unloaded)) return;

            // Bring everything back in the reverse of the order it went down.
            std::reverse(unloaded.begin(), unloaded.end());
            bool allOk = true;
            for (auto const& m : unloaded)
            {
                auto manifest = readManifest(m);
                if (!manifest)
                {
                    output.error("重新加载 '" + m + "' 失败: " + manifest.error().message());
                    allOk = false;
                    break;
                }
                if (auto e = mgr->controlLoad(std::move(*manifest)); !e)
                {
                    output.error("重新加载 '" + m + "' 失败: " + e.error().message());
                    output.error("'" + m + "' 现在处于未加载状态，修好后用 /llr load " + m + " 拉起来。");
                    allOk = false;
                    break;
                }
                // Reload is a state reset by design, so an unchanged base
                // address here is the EXPECTED outcome, not a symptom —
                // running checkImageSwapped on this path would warn every
                // single time. The check belongs only on the unload → rebuild
                // → load path, where a stable base really does mean the new
                // dll was not picked up. Drop the stashed address so a later
                // /llr load of the same mod doesn't compare against it.
                gLastUnloadBase.erase(m);
                output.success("已重新加载 '" + m + "'");
            }
            if (allOk)
            {
                output.success(
                    "'" + name + "' reload 完成（状态已重置、配置已重读；dll 代码没有更换）"
                );
            }
        }
    } // namespace

    void registerCommand()
    {
        using namespace ll::command;
        auto& handle = CommandRegistrar::getServerInstance().getOrCreateCommand(
            "llr",
            "levilamina-rs rust mod 控制：list / load / unload / reload",
            CommandPermissionLevel::Host
        );
        handle.runtimeOverload().optional("args", ParamKind::RawText).execute(
            [](CommandOrigin const&, CommandOutput& output, RuntimeCommand const& rt)
            {
                std::string raw;
                if (auto const& p = rt["args"]; p.hold(ParamKind::RawText))
                {
                    raw = p.get<ParamKind::RawText>().mText;
                }
                auto args = parseArgs(raw);

                if (args.unknownFlag)
                {
                    output.error("不认识的参数 " + args.unknownFlagText + "（只支持 --cascade）");
                    return;
                }

                auto needName = [&output, &args](char const* verb)
                {
                    if (args.name.empty())
                    {
                        output.error(std::string{"用法: /llr "} + verb + " <mod_name>");
                        return false;
                    }
                    return true;
                };

                if (args.sub == "list" || args.sub.empty())
                {
                    cmdList(output);
                    if (args.sub.empty())
                    {
                        output.success(
                            "用法: /llr list | load <n> | unload <n> [--cascade] | reload <n> [--cascade]"
                        );
                    }
                }
                else if (args.sub == "load")
                {
                    if (needName("load")) cmdLoad(output, args.name);
                }
                else if (args.sub == "unload")
                {
                    if (needName("unload")) cmdUnload(output, args.name, args.cascade);
                }
                else if (args.sub == "reload")
                {
                    if (needName("reload")) cmdReload(output, args.name, args.cascade);
                }
                else
                {
                    output.error(
                        "未知子命令 '" + args.sub
                        + "'。用法: /llr list | load <n> | unload <n> [--cascade] | reload <n> [--cascade]"
                    );
                }
            }
        );
        log().info("/llr 已注册（list / load / unload / reload）");
    }
#else
    void registerCommand() {}
#endif // !LEVI_RS_TARGET_CLIENT
} // namespace levi_rs::mod_control
