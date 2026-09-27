# Cross-compile toolchain: Windows ARM64 (aarch64-w64-mingw32) from an x86_64
# Windows host using the MSYS2 ucrt64 clang + the clangarm64 sysroot fetched by
# tools/fetch-sysroot.ps1.
#
# Usage:
#   cmake -S . -B build-winarm64 -G Ninja ^
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-aarch64.cmake ^
#     -DCMAKE_BUILD_TYPE=Release

get_filename_component(SHIP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(SYSROOT "${SHIP_ROOT}/tools/sysroot-aarch64/clangarm64")

if(NOT EXISTS "${SYSROOT}/include")
    message(FATAL_ERROR
        "ARM64 sysroot not found at:\n  ${SYSROOT}\n"
        "Run: powershell -ExecutionPolicy Bypass -File tools/fetch-sysroot.ps1")
endif()

find_program(SHIP_CLANGXX
    NAMES clang++.exe clang++
    PATHS "C:/msys64/ucrt64/bin" "C:/msys64/clang64/bin"
)
if(NOT SHIP_CLANGXX)
    message(FATAL_ERROR
        "clang++.exe not found. Install it with:\n"
        "  pacman -S mingw-w64-ucrt-x86_64-clang mingw-w64-ucrt-x86_64-lld")
endif()
get_filename_component(SHIP_CLANG_BIN "${SHIP_CLANGXX}" DIRECTORY)

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR ARM64)

set(CMAKE_C_COMPILER   "${SHIP_CLANGXX}")
set(CMAKE_CXX_COMPILER "${SHIP_CLANGXX}")
set(CMAKE_ASM_COMPILER "${SHIP_CLANGXX}")

set(CMAKE_FIND_ROOT_PATH "${SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

list(APPEND CMAKE_PREFIX_PATH "${SYSROOT}")
list(REMOVE_DUPLICATES CMAKE_PREFIX_PATH)

set(SHIP_TARGET_FLAGS
    "-target aarch64-w64-mingw32 --sysroot=\"${SYSROOT}\" -resource-dir=\"${SYSROOT}/lib/clang/22\""
)
set(SHIP_LINK_FLAGS
    "${SHIP_TARGET_FLAGS} --rtlib=compiler-rt -fuse-ld=lld -L\"${SYSROOT}/lib\""
)

set(CMAKE_C_FLAGS_INIT   "${SHIP_TARGET_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${SHIP_TARGET_FLAGS} -stdlib=libc++")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "${SHIP_LINK_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${SHIP_LINK_FLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${SHIP_LINK_FLAGS}")

find_program(SHIP_LD_LLD NAMES ld.lld.exe ld.lld PATHS "${SHIP_CLANG_BIN}" NO_DEFAULT_PATH)
find_program(SHIP_AR     NAMES llvm-ar.exe ar.exe    PATHS "${SHIP_CLANG_BIN}" C:/msys64/ucrt64/bin)
if(SHIP_LD_LLD)
    set(CMAKE_LINKER "${SHIP_LD_LLD}")
endif()
if(SHIP_AR)
    set(CMAKE_AR "${SHIP_AR}")
endif()
