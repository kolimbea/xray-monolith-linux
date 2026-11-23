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
set(XR_3RD "${XR_SRC}/3rd party")

# Flags every engine module shares: MSVC extensions the code base relies on (__super, __declspec,
# __forceinline, MS builtins), `or`/`and` used as identifiers, C++17-removed `register`, and
# MSVC-style template parsing (names in dependent bases resolved at instantiation), without which
# hundreds of thousands of lines would need `this->`/`typename` edits.
add_library(xr_common INTERFACE)
target_compile_options(xr_common INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:-fms-extensions -fno-operator-names -Wno-register -fdelayed-template-parsing>
    -fno-strict-aliasing -msse4.2)
target_compile_definitions(xr_common INTERFACE NDEBUG _CRT_SECURE_NO_WARNINGS)
target_include_directories(xr_common INTERFACE
    ${XR_SRC}/Common/linux/include ${XR_SRC} ${CMAKE_SOURCE_DIR}/sdk/include ${XR_3RD})
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
# guids.cpp: DirectSound GUIDs; NotificationClient.cpp: Windows audio endpoint notifications
# (OpenAL Soft follows the default device itself).
xr_module(xrSound ${XR_SRC}/xrSound/xrSound.vcxproj EXCLUDE guids.cpp NotificationClient.cpp)
target_link_libraries(xrSound PUBLIC PkgConfig::XR_AUDIO xrCDB)

xr_module(ode "${XR_3RD}/ode/contrib/msvc7/ode_default/default.vcxproj")
target_compile_definitions(ode PUBLIC dSINGLE PRIVATE dNODEBUG)
target_compile_options(ode PRIVATE -include ${XR_SRC}/Common/linux/ode_prelude.h)
target_include_directories(ode PUBLIC "${XR_3RD}/ode/include" PRIVATE "${XR_3RD}/ode/ode/src")
xr_module(xrPhysics ${XR_SRC}/xrPhysics/xrPhysics.vcxproj)
target_link_libraries(xrPhysics PUBLIC ode xrCDB)
