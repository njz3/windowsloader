cmake_minimum_required(VERSION 3.21)

# Honor CMAKE_MSVC_RUNTIME_LIBRARY (static CRT, required for a DLL injected into games)
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(windowsloader LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "windowsloader runs Windows games natively: build it for Windows (or run it under Wine).")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The injected DLL must match the game's bitness: the x86 preset builds the 32-bit pair,
# the x64 preset the 64-bit pair. Both land in bin/<config> so each launcher can hand
# a game of the other bitness to its sibling.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(WL_BITS 64)
    set(WL_LAUNCHER_NAME windowsloader64)
else()
    set(WL_BITS 32)
    set(WL_LAUNCHER_NAME windowsloader)
endif()
set(WL_HOOK_NAME windowsloader${WL_BITS})
set(WL_OUTPUT_DIR ${PROJECT_SOURCE_DIR}/bin/$<CONFIG>)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set(WL_C_FLAGS /W3 /utf-8 /wd4100)
    set(WL_C_DEFINES _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0601 WINVER=0x0601)
endif()

# ------------------------------------------------------------------------------
# MinHook (static), copied from linuxloader's src/minhook
# ------------------------------------------------------------------------------
file(GLOB_RECURSE MINHOOK_SRC src/minhook/src/*.c)
add_library(minhook STATIC ${MINHOOK_SRC})
target_include_directories(minhook PUBLIC src/minhook/include)

# ------------------------------------------------------------------------------
# d3d8to9 (static), crosire's (BSD 2-clause, src/d3d8to9/LICENSE.md): Direct3D 8 played on
# Direct3D 9, for the Direct3D 8 games (src/hook/d3d8hook.cpp). Direct3D 8 is 32-bit only.
# ------------------------------------------------------------------------------
if(WL_BITS EQUAL 32)
    file(GLOB D3D8TO9_SRC src/d3d8to9/*.cpp)
    add_library(d3d8to9 STATIC ${D3D8TO9_SRC})
    target_compile_definitions(d3d8to9 PRIVATE D3D8TO9NOLOG NOMINMAX)
    if(MSVC)
        target_compile_options(d3d8to9 PRIVATE /W1 /utf-8)
    endif()
    set(WL_D3D8TO9 d3d8to9)
endif()

# ------------------------------------------------------------------------------
# windowsloader.exe / windowsloader64.exe: starts the game suspended and injects the DLL
# ------------------------------------------------------------------------------
add_executable(wl_launcher src/launcher/launcher.cpp)
set_target_properties(wl_launcher PROPERTIES
    OUTPUT_NAME ${WL_LAUNCHER_NAME}
    RUNTIME_OUTPUT_DIRECTORY ${WL_OUTPUT_DIR})
target_include_directories(wl_launcher PRIVATE src/common)
target_compile_definitions(wl_launcher PRIVATE ${WL_C_DEFINES} UNICODE _UNICODE)
target_compile_options(wl_launcher PRIVATE ${WL_C_FLAGS})

# ------------------------------------------------------------------------------
# windowsloader32.dll / windowsloader64.dll: runs inside the game process
# ------------------------------------------------------------------------------
add_library(wl_hook SHARED
    src/hook/dllmain.cpp
    src/hook/wlhook.def
    src/hook/log.cpp
    src/hook/hooks.cpp
    src/hook/crash.cpp
    src/hook/files.cpp
    src/hook/devices.cpp
    src/hook/system.cpp
    src/hook/netenv.cpp
    src/hook/window.cpp
    src/hook/d3d9hook.cpp
    src/hook/dinputhook.cpp
    src/hook/vdinput.cpp
    src/hook/d3d11hook.cpp
    src/hook/glhook.cpp
    src/hook/vdev.cpp
    src/hook/vhid.cpp
    src/hook/inputs.cpp
    src/hook/agent.cpp
    src/hook/game.cpp
    src/hook/controls.cpp
    src/hook/ioports.cpp
    src/hook/ffb.cpp
    src/hook/pause.cpp
    src/hook/menu.cpp
    src/hook/display.cpp
    src/hook/d3d8hook.cpp
    src/hook/settings.cpp
    src/hook/games/segarally3.cpp
    src/hook/games/daytona.cpp
    src/hook/games/fordracing.cpp
    src/hook/games/ringedge.cpp
    src/hook/games/taito.cpp
    src/hook/games/konami.cpp
    src/hook/games/triotech.cpp
    src/hook/games/namco.cpp
    src/hook/games/unis.cpp
    src/hook/games/alls.cpp
    src/hook/games/stormracer.cpp
    src/hook/games/cardrw.cpp
    src/hook/games/codemasters.cpp
    src/hook/pcp.cpp
    src/hook/jvs.cpp
    src/hook/driveboard.cpp
    src/hook/segashell.cpp
    src/imgui/imgui.cpp
    src/imgui/imgui_draw.cpp
    src/imgui/imgui_tables.cpp
    src/imgui/imgui_widgets.cpp
    src/imgui/backends/imgui_impl_dx10.cpp
    src/imgui/backends/imgui_impl_dx11.cpp
    src/imgui/backends/imgui_impl_dx9.cpp
    src/imgui/backends/imgui_impl_win32.cpp)
set_target_properties(wl_hook PROPERTIES
    OUTPUT_NAME ${WL_HOOK_NAME}
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY ${WL_OUTPUT_DIR})
target_include_directories(wl_hook PRIVATE src/common src/hook src/imgui)
target_compile_definitions(wl_hook PRIVATE ${WL_C_DEFINES})
target_compile_options(wl_hook PRIVATE ${WL_C_FLAGS})
# SDL3 (vcpkg, static): the player's controllers, and later the force feedback
find_package(SDL3 CONFIG REQUIRED)
if(WL_D3D8TO9)
    target_compile_definitions(wl_hook PRIVATE WL_D3D8TO9)
endif()
target_link_libraries(wl_hook PRIVATE minhook ${WL_D3D8TO9} SDL3::SDL3-static dxguid user32 advapi32 gdi32 winmm ole32 oleaut32 imm32 version setupapi hid shell32 uuid ws2_32 dxgi dwmapi)
