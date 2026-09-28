# Android-specific CMake glue for WP-18.
#
# This file is intentionally NOT part of the root CMake tree (root
# CMakeLists.txt is owned by the orchestrator and skips apps/ when ANDROID is
# set). It builds SDL2 from source, pulls in the repository's engine/ tree for
# as3d_core, and compiles apps/android_boot's sources directly into the
# "main" shared library that SDLActivity.getLibraries() loads.
cmake_minimum_required(VERSION 3.22)
project(as3d_android_glue LANGUAGES C CXX)

if(NOT DEFINED AS3D_SDL2_SOURCE_DIR)
  message(FATAL_ERROR "AS3D_SDL2_SOURCE_DIR not set (run tools/fetch_third_party.sh and let "
                       "tools/android_build.sh / app/build.gradle pass it through)")
endif()
if(NOT DEFINED AS3D_REPO_ROOT)
  message(FATAL_ERROR "AS3D_REPO_ROOT not set")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# SDL2's own CMake project produces the SDL2 (shared) and SDL2main (static,
# contains the JNI SDL_android_main entry point) targets we need.
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
add_subdirectory(${AS3D_SDL2_SOURCE_DIR} ${CMAKE_BINARY_DIR}/sdl2)

# The repository's engine/ tree (as3d_core etc). Root CMakeLists.txt skips
# apps/ automatically because ANDROID is set by the NDK toolchain file, so
# this only builds engine modules, not the desktop apps.
add_subdirectory(${AS3D_REPO_ROOT} ${CMAKE_BINARY_DIR}/as3d_repo)

add_library(main SHARED
    ${AS3D_REPO_ROOT}/apps/android_boot/src/main.cpp
    ${AS3D_REPO_ROOT}/apps/android_boot/src/pak_boot.cpp
)

target_include_directories(main PRIVATE
    ${AS3D_REPO_ROOT}/apps/android_boot/src
)

target_link_libraries(main PRIVATE
    as3d_core
    SDL2
    SDL2main
    GLESv3
    EGL
    log
    android
)
