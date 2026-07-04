-- levilamina-rust-loader — LeviLamina mod that teaches the server to load Rust mods.
-- Based on the official LeviLamina mod template (levilamina-mod-template).
-- Build:  xmake f -m release -y   &&   xmake
-- Update the "levilamina" version below to match your target server version.

add_rules("mode.debug", "mode.release")

add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")

-- !! Pin this to the LeviLamina version running on your server.
add_requires("levilamina", {configs = {target_type = "server"}})
add_requires("bedrockdata")
add_requires("prelink")
add_requires("levibuildscript")

if not has_config("vs_runtime") then
    set_runtimes("MD")
end

target("levilamina-rust-loader")
    add_rules("@levibuildscript/linkrule")
    add_rules("@levibuildscript/modpacker")
    add_cxflags("/EHa", "/utf-8", "/W4")
    add_defines("NOMINMAX", "UNICODE")
    add_files("src/**.cpp")
    add_includedirs("src")
    add_packages("levilamina")
    set_exceptions("none") -- /EHa
    set_kind("shared")
    set_languages("c++20")
    set_symbols("debug")
