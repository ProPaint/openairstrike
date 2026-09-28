cmake_minimum_required(VERSION 3.22)
project(as3d LANGUAGES C CXX)

# Owned by the orchestrator. Modules add sources by dropping files into their
# own directory; they never need to edit this file.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(AS3D_SANITIZE "Build with ASan and UBSan" OFF)
option(AS3D_BUILD_TESTS "Build unit and golden tests" ON)

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

# Root of the repository, used by tests to locate the (gitignored) game data.
add_compile_definitions(AS3D_REPO_ROOT="${CMAKE_SOURCE_DIR}")

add_subdirectory(engine)
if(NOT ANDROID)
  add_subdirectory(apps)
endif()
