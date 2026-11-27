# Linux build of X-Ray Monolith (xray-linux-port). The Windows build stays on the .vcxproj files.
cmake_minimum_required(VERSION 3.25)
project(xray-monolith-linux LANGUAGES C CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "This CMake tree is for Linux only; use engine-vs2022.sln on Windows.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(PkgConfig REQUIRED)
find_package(SDL3 REQUIRED CONFIG)
find_package(TBB REQUIRED CONFIG)
find_package(Iconv REQUIRED)

set(XR_SRC ${CMAKE_SOURCE_DIR}/src)

# dxvk-native: D3D11/DXGI over Vulkan, the temporary render path (see docs/PLAN.md). Its headers
# also supply the Win32 base types for the whole engine (windows_base.h). Unpacked by
# tools/fetch_deps.sh into build/deps; override with -DDXVK_NATIVE_DIR=...
set(DXVK_NATIVE_DIR "${CMAKE_SOURCE_DIR}/../build/deps/dxvk-native-3.1.1/usr" CACHE PATH "dxvk-native prefix")
if(NOT EXISTS "${DXVK_NATIVE_DIR}/include/dxvk/windows_base.h")
    message(FATAL_ERROR "dxvk-native not found in ${DXVK_NATIVE_DIR} (run tools/fetch_deps.sh)")
endif()
set(XR_3RD "${XR_SRC}/3rd party")
# The stage-1c renderer (R4 on dxvk-native's D3D11, shader compiler on vkd3d-shader) and its `xray`
# executable. Superseded by xray_vk; kept buildable for comparisons. Needs dxvk-native's libraries and
# libvkd3d-shader. (dxvk-native's headers are always needed: they provide the Win32 base types.)
option(XR_BUILD_DXVK_RENDERER "Build the dxvk-native renderer path (xrRender_R4, xray)" OFF)
# ThinLTO for release/player builds: measured 2026-11-27, GAMMA Bar 720p, A/B twice: 30.05/29.05 ->
# 28.72/27.52 ms/frame (~4.5 %). Needs clang's LLVMgold plugin for GNU ld/ar (bfd-plugins) or lld.
option(XR_THINLTO "Build with -flto=thin" OFF)
if(XR_THINLTO)
    add_compile_options(-flto=thin)
    add_link_options(-flto=thin)
endif()

# Flags every engine module shares: MSVC extensions the code base relies on (__super, __declspec,
# __forceinline, MS builtins), `or`/`and` used as identifiers, C++17-removed `register`, and
# MSVC-style template parsing (names in dependent bases resolved at instantiation), without which
# hundreds of thousands of lines would need `this->`/`typename` edits.
add_library(xr_common INTERFACE)
target_compile_options(xr_common INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:-fms-extensions -fno-operator-names -Wno-register -fdelayed-template-parsing>
    -fno-strict-aliasing -msse4.2
    # X-Ray tests `this` against null in places (ShaderElement::equal, ...); MSVC keeps those checks,
    # clang drops them unless told not to assume pointers (and `this`) are non-null.
    -fno-delete-null-pointer-checks)
target_compile_definitions(xr_common INTERFACE NDEBUG _CRT_SECURE_NO_WARNINGS)
target_include_directories(xr_common INTERFACE
    ${XR_SRC}/Common/linux/include ${XR_SRC} ${CMAKE_SOURCE_DIR}/sdk/include ${XR_3RD}
    ${DXVK_NATIVE_DIR}/include/dxvk)
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(WARNING "Only clang is tested so far; gcc lacks -fms-extensions semantics such as __super.")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# xr_module(<target> <vcxproj> [EXCLUDE substr...] [EXTRA files...])
# Static library (all-in-one build) from the Release|x64 source list of a Visual Studio project.
function(xr_module target vcxproj)
    cmake_parse_arguments(XM "" "" "EXCLUDE;EXTRA" ${ARGN})
    execute_process(
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/cmake/vcxproj_sources.py ${vcxproj} ${XM_EXCLUDE}
        OUTPUT_VARIABLE _src RESULT_VARIABLE _rc ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "${target}: ${_err}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${vcxproj})
    string(REPLACE "\n" ";" _src "${_src}")
    list(FILTER _src EXCLUDE REGEX "^$")
    add_library(${target} STATIC ${_src} ${XM_EXTRA})
    execute_process(
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/cmake/vcxproj_sources.py --defines ${vcxproj}
        OUTPUT_VARIABLE _defs)
    string(REPLACE "\n" ";" _defs "${_defs}")
    list(FILTER _defs EXCLUDE REGEX "^$")
    target_compile_definitions(${target} PRIVATE ${_defs})
    get_filename_component(_dir ${vcxproj} DIRECTORY)
    target_include_directories(${target} PRIVATE ${_dir} ${XR_SRC}/xrCore ${XR_SRC}/xrEngine)
    target_link_libraries(${target} PUBLIC xr_common xrCore)
endfunction()

add_subdirectory(src/xrCore)
xr_module(xrCDB ${XR_SRC}/xrCDB/xrCDB.vcxproj)
xr_module(xrXMLParser ${XR_SRC}/xrXMLParser/xrXMLParser.vcxproj)
xr_module(xrCPU_Pipe ${XR_SRC}/xrCPU_Pipe/xrCPU_Pipe.vcxproj)
xr_module(xrParticles ${XR_SRC}/xrParticles/xrParticles.vcxproj)

pkg_check_modules(XR_AUDIO REQUIRED IMPORTED_TARGET openal vorbisfile vorbis ogg)
pkg_check_modules(XR_MEDIA REQUIRED IMPORTED_TARGET libjpeg theoradec theora icu-uc)
# guids.cpp: DirectSound GUIDs; NotificationClient.cpp: Windows audio endpoint notifications
# (OpenAL Soft follows the default device itself).
xr_module(xrSound ${XR_SRC}/xrSound/xrSound.vcxproj EXCLUDE guids.cpp NotificationClient.cpp)
target_link_libraries(xrSound PUBLIC PkgConfig::XR_AUDIO xrCDB)

include(cmake/luajit.cmake)

xr_module(luabind "${XR_3RD}/luabind/luabind.vcxproj")
target_include_directories(luabind PUBLIC "${XR_3RD}/luabind" PRIVATE "${XR_3RD}/luabind/luabind/src")
target_link_libraries(luabind PUBLIC luajit)
# luasocket: the POSIX backend replaces the Winsock one.
xr_module(lua_extensions "${XR_3RD}/lua-extensions/lua_extensions.vcxproj" EXCLUDE wsocket.c
    EXTRA "${XR_3RD}/lua-extensions/luasocket/usocket.c")
target_link_libraries(lua_extensions PUBLIC luajit)

xr_module(ode "${XR_3RD}/ode/contrib/msvc7/ode_default/default.vcxproj")
target_compile_definitions(ode PUBLIC dSINGLE PRIVATE dNODEBUG)
target_compile_options(ode PRIVATE -include ${XR_SRC}/Common/linux/ode_prelude.h)
target_include_directories(ode PUBLIC "${XR_3RD}/ode/include" PRIVATE "${XR_3RD}/ode/ode/src")
xr_module(xrPhysics ${XR_SRC}/xrPhysics/xrPhysics.vcxproj)
target_link_libraries(xrPhysics PUBLIC ode xrCDB)

# --- Stage 1c: engine ---
# imgui core only; the D3D backends are replaced by the renderer later.
xr_module(xrNetServer ${XR_SRC}/xrNetServer/xrNetServer.vcxproj)
target_compile_definitions(xrNetServer PRIVATE XRNETSERVER_EXPORTS)

xr_module(imgui "${XR_3RD}/imgui/imgui.vcxproj" EXCLUDE backends/)
target_include_directories(imgui PUBLIC "${XR_3RD}/imgui")

set(LX ${XR_SRC}/Common/linux)
# Windows-only parts and their Linux replacements:
#   Text_Console*  dedicated-server console window (DEDICATED_SERVER builds only)
#   tntQAVI        Video-for-Windows AVI textures (renderer stage; no replacement yet)
#   MonitorList    -> Common/linux/MonitorList_linux.cpp (SDL displays)
xr_module(xrEngine ${XR_SRC}/xrEngine/xrEngine.vcxproj
    EXCLUDE Text_Console.cpp Text_Console_WndProc.cpp tntQAVI.cpp MonitorList.cpp
    EXTRA ${LX}/MonitorList_linux.cpp ${LX}/dinput_sdl.cpp ${LX}/win32_window.cpp ${LX}/discord_stub.cpp)
target_compile_definitions(xrEngine PRIVATE ENGINE_BUILD STATIC_RENDERER_R4 AL_LIBTYPE_STATIC)
target_include_directories(xrEngine PRIVATE "${XR_3RD}/discord")
target_link_libraries(xrEngine PUBLIC imgui luabind lua_extensions xrCDB xrSound xrParticles xrXMLParser)

# --- Stage 1c: game ---
# lua_studio.cpp: Lua Studio debugger backend, only used when USE_LUA_STUDIO (non-MASTER_GOLD builds)
xr_module(xrGame ${XR_SRC}/xrGame/xrGame.vcxproj EXCLUDE lua_studio.cpp)
target_include_directories(xrGame BEFORE PRIVATE ${XR_SRC}/Common/linux/pch/xrGame)
target_include_directories(xrGame PRIVATE ${XR_SRC}/xrServerEntities "${XR_3RD}/luajit-2/src" "${XR_3RD}/luabind"
    "${XR_3RD}/cximage")
target_link_libraries(xrGame PUBLIC xrEngine xrPhysics xrNetServer luabind lua_extensions)

if(XR_BUILD_DXVK_RENDERER)
# --- Stage 1c: renderer (temporary path: D3D11 on dxvk-native) ---
xr_module(xrRender_R4 ${XR_SRC}/Layers/xrRenderPC_R4/xrRender_R4.vcxproj EXCLUDE ai_script_lua_debug.cpp ai_script_lua_extension.cpp)
target_compile_definitions(xrRender_R4 PRIVATE USE_DX11)
# D3DX9/D3DX11 functions and the D3D shader compiler API (Windows-only libraries). The compiler is
# built on vkd3d-shader's C API: vkd3d-utils uses ms_abi on x86-64 Linux, unlike dxvk-native.
target_sources(xrRender_R4 PRIVATE ${XR_SRC}/Common/linux/d3dx9_linux.cpp ${XR_SRC}/Common/linux/d3dx11_tex_linux.cpp
    ${XR_SRC}/Common/linux/dds_codec.cpp ${XR_SRC}/Common/linux/d3dcompiler_linux.cpp)
pkg_check_modules(XR_VKD3D_SHADER REQUIRED IMPORTED_TARGET libvkd3d-shader)
target_link_libraries(xrRender_R4 PUBLIC PkgConfig::XR_VKD3D_SHADER PkgConfig::XR_MEDIA)
endif()
# The DirectX SDK's D3DX10/D3DX11 headers include each other with varying letter case. Expose them
# under lower-case names in the build tree (symlinks), so every spelling resolves on Linux.
set(XR_DXSDK_CI ${CMAKE_BINARY_DIR}/dxsdk_ci)
file(MAKE_DIRECTORY ${XR_DXSDK_CI})
# D3DX9 too: the SDK headers are made to coexist with D3DX10/11 (the mingw ones in dxvk are not).
file(GLOB _d3dx_hdrs ${CMAKE_SOURCE_DIR}/sdk/include/dxsdk/D3DX1*.h ${CMAKE_SOURCE_DIR}/sdk/include/dxsdk/d3dx1*.h
     ${CMAKE_SOURCE_DIR}/sdk/include/dxsdk/d3dx9*.h ${CMAKE_SOURCE_DIR}/sdk/include/dxsdk/d3dx9*.inl)
foreach(h ${_d3dx_hdrs})
    get_filename_component(n ${h} NAME)
    string(TOLOWER ${n} ln)
    file(CREATE_LINK ${h} ${XR_DXSDK_CI}/${ln} SYMBOLIC)
    if(NOT n STREQUAL ln)
        file(CREATE_LINK ${h} ${XR_DXSDK_CI}/${n} SYMBOLIC) # the original spelling too
    endif()
    # also the mixed spellings used by the sources (D3Dx11core.h, D3DX10Core.h, D3DX10Tex.h ...)
    foreach(v D3Dx11core.h D3DX10Core.h D3Dx10core.h D3DX10Tex.h)
        string(TOLOWER ${v} lv)
        if(lv STREQUAL ln)
            file(CREATE_LINK ${h} ${XR_DXSDK_CI}/${v} SYMBOLIC)
        endif()
    endforeach()
endforeach()
if(XR_BUILD_DXVK_RENDERER)
    target_include_directories(xrRender_R4 BEFORE PRIVATE ${XR_DXSDK_CI})
    target_include_directories(xrRender_R4 PRIVATE ${XR_SRC}/Layers/xrRender ${XR_SRC}/Layers/xrRenderDX10 "${XR_3RD}/imgui")
    target_link_directories(xrRender_R4 PUBLIC ${DXVK_NATIVE_DIR}/lib)
    target_link_libraries(xrRender_R4 PUBLIC xrEngine dxvk_d3d11 dxvk_dxgi)
endif()

# --- Stage 2: own Vulkan renderer (ADR 0001). Same R4 pipeline + shared layer sources, compiled with
# USE_VK (and USE_DX11 for the R3/R4 pipeline logic). all of xrRenderDX10 is reused; the D3D11/DXGI
# objects themselves are implemented by Layers/xrRenderVK (ADR 0001 refinement). Not linked against dxvk.
xr_module(xrRender_VK ${XR_SRC}/Layers/xrRenderPC_R4/xrRender_R4.vcxproj
    EXCLUDE ai_script_lua_debug.cpp ai_script_lua_extension.cpp
    EXTRA ${XR_SRC}/Common/linux/d3dx9_linux.cpp ${XR_SRC}/Common/linux/d3dx11_tex_linux.cpp)
target_compile_definitions(xrRender_VK PRIVATE USE_DX11 USE_VK)
find_package(Vulkan REQUIRED)
# Shader toolchain of the Vulkan renderer (tools/fetch_deps.sh puts both into build/deps):
# DXC 1.9.2609 (libdxcompiler.so, in-process HLSL -> SPIR-V) and SPIRV-Reflect vulkan-sdk-1.4.363.0.
set(DXC_DIR "${CMAKE_SOURCE_DIR}/../build/deps/dxc-1.9.2609" CACHE PATH "DXC release directory")
set(SPIRV_REFLECT_DIR "${CMAKE_SOURCE_DIR}/../build/deps/spirv-reflect-1.4.363.0" CACHE PATH "SPIRV-Reflect source")
if(NOT EXISTS "${DXC_DIR}/lib/libdxcompiler.so" OR NOT EXISTS "${SPIRV_REFLECT_DIR}/spirv_reflect.c")
    message(FATAL_ERROR "DXC or SPIRV-Reflect missing in build/deps (run tools/fetch_deps.sh)")
endif()
add_library(dxcompiler SHARED IMPORTED)
set_target_properties(dxcompiler PROPERTIES IMPORTED_LOCATION "${DXC_DIR}/lib/libdxcompiler.so"
    INTERFACE_INCLUDE_DIRECTORIES "${DXC_DIR}/include")
add_library(spirv_reflect STATIC ${SPIRV_REFLECT_DIR}/spirv_reflect.c)
target_include_directories(spirv_reflect SYSTEM PUBLIC ${SPIRV_REFLECT_DIR})
# DXC wrapper + HLSL compat rules: std types only, deliberately without the engine's include paths
# (DXC's COM headers and the engine's Win32 stand-ins cannot share a translation unit).
add_library(xrVK_shaderc STATIC ${XR_SRC}/Layers/xrRenderVK/vkDxc.cpp ${XR_SRC}/Layers/xrRenderVK/vkHlslRewrite.cpp)
target_include_directories(xrVK_shaderc PUBLIC ${XR_SRC}/Layers/xrRenderVK)
target_link_libraries(xrVK_shaderc PRIVATE dxcompiler)

# AMD FidelityFX Super Resolution 1.0 (MIT): the headers are compiled in twice — as HLSL text the
# present pass compiles with DXC at run time (generated fsr1_hlsl.h), and with A_CPU for the constants.
set(FSR1_DIR "${CMAKE_SOURCE_DIR}/../build/deps/fsr1-a21ffb8" CACHE PATH "FidelityFX FSR 1 source")
if(NOT EXISTS "${FSR1_DIR}/ffx-fsr/ffx_fsr1.h")
    message(FATAL_ERROR "FidelityFX FSR 1 missing in ${FSR1_DIR} (run tools/fetch_deps.sh)")
endif()
file(READ "${FSR1_DIR}/ffx-fsr/ffx_a.h" _ffx_a)
file(READ "${FSR1_DIR}/ffx-fsr/ffx_fsr1.h" _ffx_fsr1)
file(WRITE "${CMAKE_BINARY_DIR}/generated/fsr1_hlsl.h"
    "// generated by CMakeLists.txt from ${FSR1_DIR} (MIT, see its license.txt)\n#pragma once\n"
    "static const char *const kFfxAHeader = R\"ffx(${_ffx_a})ffx\";\n"
    "static const char *const kFfxFsr1Header = R\"ffx(${_ffx_fsr1})ffx\";\n")

# xrVK_core: the Vulkan device layer, independent of the engine (tests/vk_smoke.cpp links it alone).
set(XR_VK_CORE_SRC vkCore.cpp vkFormat.cpp vkObjects.cpp vkDevice.cpp vkDXGI.cpp vkShader.cpp vkReflection.cpp vkShaderObjects.cpp vkHeap.cpp vkBuffer.cpp vkTexture.cpp vkViews.cpp vkStates.cpp vkContext.cpp vkQuery.cpp vkPresent.cpp vkFsr.cpp)
list(TRANSFORM XR_VK_CORE_SRC PREPEND ${XR_SRC}/Layers/xrRenderVK/)
add_library(xrVK_core STATIC ${XR_VK_CORE_SRC} ${XR_SRC}/Common/linux/dds_codec.cpp)
target_include_directories(xrVK_core BEFORE PUBLIC ${XR_SRC}/Layers/xrRenderVK/include ${XR_SRC}/Layers/xrRenderVK)
target_link_libraries(xrVK_core PUBLIC xr_common Vulkan::Vulkan SDL3::SDL3 PRIVATE xrVK_shaderc spirv_reflect dxcompiler)
target_include_directories(xrVK_core PRIVATE ${CMAKE_BINARY_DIR}/generated)
set_source_files_properties(${XR_SRC}/Layers/xrRenderVK/vkFsr.cpp PROPERTIES INCLUDE_DIRECTORIES "${FSR1_DIR}/ffx-fsr")
file(GLOB XR_VK_SRC CONFIGURE_DEPENDS ${XR_SRC}/Layers/xrRenderVK/*.cpp)
list(REMOVE_ITEM XR_VK_SRC ${XR_VK_CORE_SRC} ${XR_SRC}/Layers/xrRenderVK/vkDxc.cpp ${XR_SRC}/Layers/xrRenderVK/vkHlslRewrite.cpp)
target_sources(xrRender_VK PRIVATE ${XR_VK_SRC})
target_link_libraries(xrRender_VK PUBLIC xrVK_core)
# stand-in d3d11.h/dxgi*.h/d3dcompiler.h first: they lead to xrRenderVK/vkCommonTypes.h
target_include_directories(xrRender_VK BEFORE PRIVATE ${XR_SRC}/Layers/xrRenderVK/include ${XR_SRC}/Layers/xrRenderVK)
target_include_directories(xrRender_VK PRIVATE ${XR_SRC}/Layers/xrRender ${XR_SRC}/Layers/xrRenderDX10 ${XR_DXSDK_CI}
    "${XR_3RD}/imgui")
target_link_libraries(xrRender_VK PUBLIC xrEngine Vulkan::Vulkan)

# --- libraries the executable needs besides the engine modules ---
xr_module(xrAPI ${XR_SRC}/Layers/xrAPI/xrAPI.vcxproj)
target_include_directories(xrAPI PRIVATE ${XR_SRC}/Layers/xrRender ${XR_SRC}/Layers/xrRenderDX10 ${XR_DXSDK_CI})
target_compile_definitions(xrAPI PRIVATE USE_DX11)
# crypto: only the engine wrappers; the bundled OpenSSL 1.0 sources are replaced by the system OpenSSL.
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
add_library(crypto STATIC "${XR_3RD}/crypto/crypto.cpp" "${XR_3RD}/crypto/xr_dsa.cpp" "${XR_3RD}/crypto/xr_sha.cpp")
target_compile_definitions(crypto PRIVATE CRYPTO_BUILD)
target_compile_options(crypto PRIVATE -Wno-deprecated-declarations) # DSA_*/SHA_* are deprecated in OpenSSL 3
target_link_libraries(crypto PUBLIC xr_common xrCore OpenSSL::Crypto)
xr_module(cximage "${XR_3RD}/cximage/cximage.vcxproj")
target_compile_options(cximage PRIVATE -include ${XR_SRC}/Common/linux/cximage_prelude.h)
target_link_libraries(cximage PUBLIC PkgConfig::XR_MEDIA)
# The imgui D3D11 backend is compiled into each renderer against that renderer's D3D11 types.
set(_xr_renderers xrRender_VK)
if(XR_BUILD_DXVK_RENDERER)
    list(APPEND _xr_renderers xrRender_R4)
endif()
foreach(r ${_xr_renderers})
    target_sources(${r} PRIVATE "${XR_3RD}/imgui/backends/imgui_impl_dx11.cpp")
    target_include_directories(${r} PRIVATE "${XR_3RD}/imgui/backends")
endforeach()

if(XR_BUILD_DXVK_RENDERER)
# --- the game executable (all-in-one, like Monolith on Windows) ---
add_executable(xray ${XR_SRC}/Common/linux/exe_main_stub.cpp ${XR_SRC}/Common/linux/reshade_stub.cpp)
# Static engine libraries reference each other in cycles; resolve them as one group.
target_link_libraries(xray PRIVATE
    -Wl,--start-group
    xrGame xrRender_R4 xrEngine xrNetServer xrPhysics ode xrSound xrParticles xrCPU_Pipe xrCDB xrXMLParser
    imgui luabind lua_extensions luajit xrAPI crypto cximage
    -Wl,--end-group
    gif dxvk_d3d11 dxvk_dxgi
    xrCore)
target_link_options(xray PRIVATE -rdynamic)  # symbol names in xrDebug backtraces
set_target_properties(xray PROPERTIES BUILD_RPATH "${DXVK_NATIVE_DIR}/lib;$<TARGET_FILE_DIR:xrCore>")
endif()

# The same game linked with the Vulkan renderer instead of R4-on-dxvk (stage 2, ADR 0001).
add_executable(xray_vk ${XR_SRC}/Common/linux/exe_main_stub.cpp ${XR_SRC}/Common/linux/reshade_stub.cpp)
target_link_libraries(xray_vk PRIVATE
    -Wl,--start-group
    xrGame xrRender_VK xrEngine xrNetServer xrPhysics ode xrSound xrParticles xrCPU_Pipe xrCDB xrXMLParser
    imgui luabind lua_extensions luajit xrAPI crypto cximage
    -Wl,--end-group
    gif)
target_link_options(xray_vk PRIVATE -rdynamic -Wl,-u,xrvk_engine_log_hook) # backend log -> engine log (vkBase.cpp)
set_target_properties(xray_vk PROPERTIES BUILD_RPATH "$<TARGET_FILE_DIR:xrCore>")

# Milestone M2 test of the Vulkan device layer (tests/vk_smoke.cpp).
add_executable(vk_smoke ${CMAKE_SOURCE_DIR}/../tests/vk_smoke.cpp)
target_link_libraries(vk_smoke PRIVATE xrVK_core)
# Milestone M4 test of the draw path (tests/vk_draw.cpp).
add_executable(vk_draw ${CMAKE_SOURCE_DIR}/../tests/vk_draw.cpp)
target_link_libraries(vk_draw PRIVATE xrVK_core)
# Host heap unit test (tests/vk_heap.cpp).
add_executable(vk_heap ${CMAKE_SOURCE_DIR}/../tests/vk_heap.cpp)
target_link_libraries(vk_heap PRIVATE xrVK_core)
# Milestone M3 check: every shader of an exported tree through D3DCompile/D3DReflect (tests/vk_shaders.cpp).
add_executable(vk_shaders ${CMAKE_SOURCE_DIR}/../tests/vk_shaders.cpp)
target_link_libraries(vk_shaders PRIVATE xrVK_core)
# Reflection dump against the Vulkan backend (tests/reflect_dump.cpp; R4 counterpart: tests/reflect_dump_r4.sh).
add_executable(reflect_dump_vk ${CMAKE_SOURCE_DIR}/../tests/reflect_dump.cpp)
target_link_libraries(reflect_dump_vk PRIVATE xrVK_core)
# Unit test of the HLSL compat rules (tests/vk_hlsl_rewrite.cpp).
add_executable(vk_hlsl_rewrite ${CMAKE_SOURCE_DIR}/../tests/vk_hlsl_rewrite.cpp)
target_link_libraries(vk_hlsl_rewrite PRIVATE xrVK_shaderc)
