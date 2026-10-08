cmake_minimum_required(VERSION 3.22)
project(as3d LANGUAGES C CXX)

# Owned by the orchestrator. Modules add sources by dropping files into their
# own directory; they never need to edit this file.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# gnu++17 on the 3DS: newlib hides fseeko/ftello without _GNU_SOURCE (the spike used gnu++17).
if(NINTENDO_3DS)
  set(CMAKE_CXX_EXTENSIONS ON)
else()
  set(CMAKE_CXX_EXTENSIONS OFF)
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(AS3D_SANITIZE "Build with ASan and UBSan" OFF)
option(AS3D_BUILD_TESTS "Build unit and golden tests" ON)
if(NINTENDO_3DS)
  set(AS3D_BUILD_TESTS OFF)
  # Music (docs/3ds-port.md M2.4): libopenmpt is built from the source tree that
  # tools/3ds_build.sh mounts and passes as AS3D_3DS_OPENMPT_SRC (fetched once, outside the
  # build, by tools/fetch_third_party.sh). Without it the stub decoder: no music.
  set(AS3D_3DS_OPENMPT_SRC "" CACHE PATH "libopenmpt source tree for the 3DS build (empty: no music)")
  if(AS3D_3DS_OPENMPT_SRC AND EXISTS "${AS3D_3DS_OPENMPT_SRC}/libopenmpt/libopenmpt.h")
    set(AS3D_WITH_OPENMPT ON CACHE BOOL "" FORCE)
  else()
    if(AS3D_3DS_OPENMPT_SRC)
      message(WARNING "3DS: no libopenmpt source at '${AS3D_3DS_OPENMPT_SRC}': music decoder stub (no music)")
    else()
      message(WARNING "3DS: AS3D_3DS_OPENMPT_SRC not set: music decoder stub (no music)")
    endif()
    set(AS3D_WITH_OPENMPT OFF CACHE BOOL "" FORCE)
  endif()
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_compile_options(-Wall -Wextra -Wno-unused-parameter)
# RTTI is off everywhere, tests included, so that interface vtables agree
# between translation units.
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
if(AS3D_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# Root of the repository. Use this, never CMAKE_SOURCE_DIR, for paths inside the repository:
# the Android build adds this directory from android/app/src/main/cpp, where CMAKE_SOURCE_DIR
# is that directory. Tests also use it to locate the (gitignored) game data.
set(AS3D_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}")
add_compile_definitions(AS3D_REPO_ROOT="${AS3D_REPO_ROOT}")

add_subdirectory(engine)
if(NINTENDO_3DS)
  # The 3DS build is the one app below; the desktop apps, tests and tools need SDL/GL/Python.
  add_subdirectory(apps/3ds)
elseif(NOT ANDROID)
  add_subdirectory(apps)
endif()
