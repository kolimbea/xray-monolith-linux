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
# __forceinline, MS builtins), `or`/`and` used as identifiers, and C++17-removed `register`.
add_library(xr_common INTERFACE)
target_compile_options(xr_common INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:-fms-extensions -fno-operator-names -Wno-register>
    -fno-strict-aliasing -msse4.2)
target_compile_definitions(xr_common INTERFACE NDEBUG _CRT_SECURE_NO_WARNINGS)
target_include_directories(xr_common INTERFACE
    ${XR_SRC}/Common/linux/include ${XR_SRC} ${CMAKE_SOURCE_DIR}/sdk/include ${XR_3RD})
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(WARNING "Only clang is tested so far; gcc lacks -fms-extensions semantics such as __super.")
endif()

add_subdirectory(src/xrCore)
