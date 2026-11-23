# Bundled LuaJIT 2.0.4 (Monolith-patched: xr_alloc, lj_allocprof), built the way
# src/3rd party/luajit-2/src/gen_luajit_artifacts.bat does on Windows, with ELF output.
set(LJ_SRC "${XR_3RD}/luajit-2/src")
set(LJ_GEN ${CMAKE_BINARY_DIR}/luajit-gen)
file(MAKE_DIRECTORY ${LJ_GEN} ${LJ_GEN}/jit)

add_executable(lj_minilua "${LJ_SRC}/host/minilua.c")
target_link_libraries(lj_minilua PRIVATE m)

set(LJ_DASC_FLAGS -D JIT -D FFI -D P64)
add_custom_command(OUTPUT ${LJ_GEN}/buildvm_arch.h
    COMMAND lj_minilua "${XR_3RD}/luajit-2/dynasm/dynasm.lua" -LN ${LJ_DASC_FLAGS}
            -o ${LJ_GEN}/buildvm_arch.h "${LJ_SRC}/vm_x86.dasc"
    DEPENDS lj_minilua "${LJ_SRC}/vm_x86.dasc" WORKING_DIRECTORY "${LJ_SRC}" VERBATIM)

add_executable(lj_buildvm "${LJ_SRC}/host/buildvm.c" "${LJ_SRC}/host/buildvm_asm.c"
    "${LJ_SRC}/host/buildvm_peobj.c" "${LJ_SRC}/host/buildvm_lib.c" "${LJ_SRC}/host/buildvm_fold.c"
    ${LJ_GEN}/buildvm_arch.h)
target_include_directories(lj_buildvm PRIVATE ${LJ_GEN} "${LJ_SRC}" "${XR_3RD}/luajit-2/dynasm")

set(LJ_LIB_C lib_base.c lib_math.c lib_bit.c lib_string.c lib_table.c lib_io.c lib_os.c
    lib_package.c lib_debug.c lib_jit.c lib_ffi.c)
set(LJ_HDRS)
foreach(mode bcdef ffdef libdef recdef)
    add_custom_command(OUTPUT ${LJ_GEN}/lj_${mode}.h
        COMMAND lj_buildvm -m ${mode} -o ${LJ_GEN}/lj_${mode}.h ${LJ_LIB_C}
        DEPENDS lj_buildvm WORKING_DIRECTORY "${LJ_SRC}" VERBATIM)
    list(APPEND LJ_HDRS ${LJ_GEN}/lj_${mode}.h)
endforeach()
add_custom_command(OUTPUT ${LJ_GEN}/lj_folddef.h
    COMMAND lj_buildvm -m folddef -o ${LJ_GEN}/lj_folddef.h lj_opt_fold.c
    DEPENDS lj_buildvm WORKING_DIRECTORY "${LJ_SRC}" VERBATIM)
add_custom_command(OUTPUT ${LJ_GEN}/jit/vmdef.lua
    COMMAND lj_buildvm -m vmdef -o ${LJ_GEN}/jit/vmdef.lua ${LJ_LIB_C}
    DEPENDS lj_buildvm WORKING_DIRECTORY "${LJ_SRC}" VERBATIM)
add_custom_command(OUTPUT ${LJ_GEN}/lj_vm.S
    COMMAND lj_buildvm -m elfasm -o ${LJ_GEN}/lj_vm.S
    DEPENDS lj_buildvm WORKING_DIRECTORY "${LJ_SRC}" VERBATIM)
list(APPEND LJ_HDRS ${LJ_GEN}/lj_folddef.h ${LJ_GEN}/jit/vmdef.lua)

execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/cmake/vcxproj_sources.py
                "${LJ_SRC}/luajit.vcxproj" ljamalg.c luajit.c
                OUTPUT_VARIABLE LJ_CORE)
string(REPLACE "\n" ";" LJ_CORE "${LJ_CORE}")
list(FILTER LJ_CORE EXCLUDE REGEX "^$")
enable_language(ASM)
add_library(luajit STATIC ${LJ_CORE} ${LJ_GEN}/lj_vm.S ${LJ_HDRS})
target_include_directories(luajit PRIVATE ${LJ_GEN} PUBLIC "${LJ_SRC}")
target_compile_options(luajit PRIVATE -fno-strict-aliasing -fomit-frame-pointer)
target_link_libraries(luajit PUBLIC m dl)
