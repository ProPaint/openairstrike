# apps/android_boot: WP-18 bring-up program (native SDL_main entry point).
#
# The Android APK does NOT build through this file: android/app compiles
# these same sources directly into its "main" shared library (see
# android/app/src/main/cpp/CMakeLists.txt), because Android needs a JNI
# shared library, not a standalone executable, and because apps/CMakeLists.txt
# is skipped entirely by the root tree when ANDROID is set.
#
# This file covers the desktop build. apps/CMakeLists.txt (owned by the
# orchestrator) only add_subdirectory()s "game", "viewer" and "rcsl_tool", so
# this directory is not wired into the normal `apps/` build yet. Configure it
# standalone:
#   cmake -S apps/android_boot -B build-boot && cmake --build build-boot
# (One line would need to change in apps/CMakeLists.txt to fold it in: add
# "android_boot" to the `foreach(app game viewer rcsl_tool)` list.)
cmake_minimum_required(VERSION 3.22)

# Detect whether we're add_subdirectory()'d from a parent tree that already
# defines as3d_core (e.g. if apps/CMakeLists.txt one day adds us), or being
# configured standalone and need to pull the engine in ourselves.
get_directory_property(AS3D_ANDROID_BOOT_HAS_PARENT PARENT_DIRECTORY)

if(NOT AS3D_ANDROID_BOOT_HAS_PARENT)
    project(as3d_android_boot LANGUAGES C CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE RelWithDebInfo)
    endif()
    get_filename_component(AS3D_ANDROID_BOOT_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
    add_compile_definitions(AS3D_REPO_ROOT="${AS3D_ANDROID_BOOT_REPO_ROOT}")
    add_subdirectory(${AS3D_ANDROID_BOOT_REPO_ROOT}/engine ${CMAKE_BINARY_DIR}/engine)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(EGL REQUIRED IMPORTED_TARGET egl)
pkg_check_modules(GLESV2 REQUIRED IMPORTED_TARGET glesv2)

add_executable(android_boot
    src/main.cpp
    src/sdl_stream.cpp
)
target_link_libraries(android_boot PRIVATE
    as3d_core
    as3d_vfs
    as3d_audio
    PkgConfig::SDL2
    PkgConfig::EGL
    PkgConfig::GLESV2
)
