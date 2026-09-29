# Android-specific CMake glue: builds SDL2 from source, the repository's engine/ tree, and
# the game (apps/game) into the "main" shared library that SDLActivity loads.
#
# This file is not part of the root CMake tree (the root CMakeLists.txt skips apps/ when
# ANDROID is set), so it adds apps/game itself; apps/game/CMakeLists.txt builds the shared
# game library as3d_game_shell (also linked by the desktop executable) and, on Android,
# the "main" library around apps/game/android_main.cpp.
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

# SDL2's own CMake project produces the SDL2 (shared) and SDL2main (static, contains the
# JNI SDL_android_main entry point) targets we need.
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
add_subdirectory(${AS3D_SDL2_SOURCE_DIR} ${CMAKE_BINARY_DIR}/sdl2)

# The repository's engine/ tree. The root CMakeLists.txt skips apps/ because ANDROID is set
# by the NDK toolchain file, and tests are not built for the device.
set(AS3D_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${AS3D_REPO_ROOT} ${CMAKE_BINARY_DIR}/as3d_repo)

# The game: as3d_game_shell and the "main" library.
add_subdirectory(${AS3D_REPO_ROOT}/apps/game ${CMAKE_BINARY_DIR}/as3d_game_app)
