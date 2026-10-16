-- Skyrim Companion: SKSE plugin that serves live player data to a second-monitor web page.

-- include CommonLibSSE-NG
includes("lib/commonlibsse-ng")

-- project constants
set_project("SkyrimCompanion")
set_version("0.1.0")
set_license("GPL-3.0")
set_languages("c++23")
set_warnings("allextra")

add_rules("mode.debug", "mode.releasedbg")

add_requires("nlohmann_json")

-- Tiny localhost web server. Kept in its own library so the Windows socket
-- headers never mix with the game library's headers.
target("companion-server")
    set_kind("static")
    set_arch("x64")
    add_files("server/**.cpp")
    add_headerfiles("server/**.h")
    add_includedirs("server", { public = true })
    add_syslinks("ws2_32", { public = true })
    add_cxxflags("cl::/EHsc", "cl::/utf-8")

-- The game plugin itself.
target("SkyrimCompanion")
    add_rules("commonlibsse-ng.plugin", {
        name = "SkyrimCompanion",
        author = "George",
        description = "Live map and stats on a second monitor"
    })

    add_deps("companion-server")
    add_packages("nlohmann_json")

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")
