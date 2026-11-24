# Source list mirrors xrCore.vcxproj (Release|x64), minus Windows-only files.
file(GLOB XRCORE_SRC CONFIGURE_DEPENDS *.cpp)
list(REMOVE_ITEM XRCORE_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/xrDebug.cpp                  # excluded on Windows too
    ${CMAKE_CURRENT_SOURCE_DIR}/xrDebugNew.cpp               # replaced by Common/linux/xrDebug_linux.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/LocatorAPI_Notifications.cpp # excluded on Windows; unused
    ${CMAKE_CURRENT_SOURCE_DIR}/FileSystem_borland.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/xrMemory_subst_borland.cpp)

# Make sure the glob did not pick up anything the Visual Studio project does not build.
file(READ xrCore.vcxproj _vcx)
foreach(f ${XRCORE_SRC})
    get_filename_component(n ${f} NAME)
    string(FIND "${_vcx}" "Include=\"${n}\"" _hit)
    if(_hit EQUAL -1)
        message(FATAL_ERROR "xrCore: ${n} is not part of xrCore.vcxproj")
    endif()
endforeach()

set(LINUX_DIR ${XR_SRC}/Common/linux)
add_library(xrCore SHARED ${XRCORE_SRC}
    ${LINUX_DIR}/io_compat.cpp ${LINUX_DIR}/path_resolve.cpp ${LINUX_DIR}/win32_file.cpp ${LINUX_DIR}/win32_system.cpp
    ${LINUX_DIR}/win32_text.cpp ${LINUX_DIR}/xrDebug_linux.cpp)
target_compile_definitions(xrCore PRIVATE XRCORE_EXPORTS PURE_ALLOC)
target_include_directories(xrCore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(xrCore PUBLIC xr_common SDL3::SDL3 TBB::tbb Iconv::Iconv PRIVATE pthread)
