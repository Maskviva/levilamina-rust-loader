add_rules("mode.debug", "mode.release")

add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")

add_requires("levilamina 26.20.0", {configs = {target_type = "server"}})
add_requires("legacymoney 0.19.0", {configs = {target_type = "server"}})

add_requires("bedrockdata v26.20.5-server.4")
add_requires("prelink v0.7.1")
add_requires("levibuildscript")
add_requires("zlib 1.3.1")

if not has_config("vs_runtime") then
    set_runtimes("MD")
end

target("levilamina-rust-loader")
    on_load(function (target)
        target:add("rules", "@levibuildscript/linkrule")
        target:add("rules", "@levibuildscript/modpacker")
    end)
    add_cxflags("/EHa", "/utf-8", "/W4")
    add_defines("NOMINMAX", "UNICODE")
    add_files("src/**.cpp")
    add_includedirs("src")
    add_packages("levilamina", "legacymoney")
    add_ldflags("/DELAYLOAD:LegacyMoney.dll", {force = true})
    add_syslinks("delayimp")
    set_exceptions("none") -- /EHa
    set_kind("shared")
    set_languages("c++20")
    set_symbols("debug")