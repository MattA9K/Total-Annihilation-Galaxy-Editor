# Cross build for 64-bit Windows with MinGW-w64 from Linux or macOS, used by the
# "windows" preset. Install the x86_64-w64-mingw32 compilers first (on Debian and
# Ubuntu: apt install mingw-w64, then pick the posix thread model with
# update-alternatives). Set MINGW_PREFIX to use a differently named toolchain,
# for example -DMINGW_PREFIX=x86_64-w64-mingw32ucrt for Debian's UCRT compilers
# (apt install g++-mingw-w64-ucrt64), which release builds use.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if(NOT DEFINED MINGW_PREFIX)
    set(MINGW_PREFIX x86_64-w64-mingw32)
endif()

# Prefer the posix thread model compilers when the distribution ships both, since
# std::thread and std::mutex need it on older MinGW-w64 runtimes.
find_program(CMAKE_C_COMPILER NAMES ${MINGW_PREFIX}-gcc-posix ${MINGW_PREFIX}-gcc REQUIRED)
find_program(CMAKE_CXX_COMPILER NAMES ${MINGW_PREFIX}-g++-posix ${MINGW_PREFIX}-g++ REQUIRED)
find_program(CMAKE_RC_COMPILER NAMES ${MINGW_PREFIX}-windres windres REQUIRED)

# Look for libraries and headers in the MinGW sysroot only (it sits beside the
# compiler's bin folder, as /usr/x86_64-w64-mingw32 or Homebrew's equivalent),
# but run build tools from the host.
get_filename_component(MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
get_filename_component(MINGW_SYSROOT "${MINGW_BIN_DIR}/../${MINGW_PREFIX}" ABSOLUTE)
set(CMAKE_FIND_ROOT_PATH "${MINGW_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
