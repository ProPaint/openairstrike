# Web feasibility spike (docs/web-spike.md): the engine compiled to WebAssembly with
# Emscripten, drawing through WebGL 2. A standalone CMake project, never part of the desktop
# build, Android or tools/ci.sh. Configure only through tools/web_build.sh (emcmake).
#
#   as3d_web_level: one level at a slowly moving scroll position (the view of
#                   `as3d_viewer level 1 --scroll 500`), Stage A.
#   as3d_web_game:  the real game loop (apps/game) straight into a mission, Stage B.
#
# The engine modules are used unchanged: their pkg-config lookups (sdl2, egl, glesv2) resolve
# to the stub .pc files in pkgconfig/, which turn into Emscripten's -sUSE_SDL=2 and WebGL 2
# flags. The platform module's headless EGL backend is swapped for its null stand-in.
cmake_minimum_required(VERSION 3.22)
project(as3d_web_spike LANGUAGES C CXX)

if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "apps/web_spike builds only with Emscripten: run tools/web_build.sh")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra -Wno-unused-parameter)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)

get_filename_component(AS3D_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_compile_definitions(AS3D_REPO_ROOT="${AS3D_REPO_ROOT}")

# Emscripten's toolchain file points pkg-config at its sysroot (again on every try_compile);
# a wrapper pins it to the stubs.
set(PKG_CONFIG_EXECUTABLE "${CMAKE_CURRENT_LIST_DIR}/pkgconfig/pkg-config-web" CACHE FILEPATH "" FORCE)
set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH OFF)

add_subdirectory(${AS3D_REPO_ROOT}/engine ${CMAKE_BINARY_DIR}/engine)
get_property(AS3D_MODULES GLOBAL PROPERTY AS3D_MODULES)

# No headless EGL backend in a browser: build the platform module's null stand-in instead.
set_source_files_properties(${AS3D_REPO_ROOT}/engine/src/platform/egl_headless.cpp
  DIRECTORY ${AS3D_REPO_ROOT}/engine PROPERTIES HEADER_FILE_ONLY TRUE)
set_source_files_properties(${AS3D_REPO_ROOT}/engine/src/platform/android_stub.cpp
  DIRECTORY ${AS3D_REPO_ROOT}/engine PROPERTIES HEADER_FILE_ONLY FALSE)

# libopenmpt throws and catches C++ exceptions: native WebAssembly exception handling.
if(TARGET openmpt_static)
  target_compile_options(openmpt_static PRIVATE -fwasm-exceptions)
endif()

set(AS3D_WEB_LINK
  -sUSE_SDL=2
  -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB
  -sSTACK_SIZE=4MB
  -sFORCE_FILESYSTEM=1
  -sEXIT_RUNTIME=0
  -sENVIRONMENT=web
  -fwasm-exceptions
  "-sEXPORTED_RUNTIME_METHODS=['callMain']"
)
if(AS3D_WEB_FULL_ES3)
  list(APPEND AS3D_WEB_LINK -sFULL_ES3=1)
endif()

add_executable(as3d_web_level level_main.cpp ${AS3D_REPO_ROOT}/apps/viewer/level_render.cpp)
target_include_directories(as3d_web_level PRIVATE ${AS3D_REPO_ROOT}/apps/viewer)
target_link_libraries(as3d_web_level PRIVATE ${AS3D_MODULES} ${AS3D_MODULES})
target_link_options(as3d_web_level PRIVATE ${AS3D_WEB_LINK})
set_target_properties(as3d_web_level PROPERTIES SUFFIX ".js")

set(AS3D_GAME_DIR ${AS3D_REPO_ROOT}/apps/game)
add_executable(as3d_web_game game_main.cpp
  ${AS3D_GAME_DIR}/game_session.cpp ${AS3D_GAME_DIR}/game_view.cpp ${AS3D_GAME_DIR}/audio_bridge.cpp
  ${AS3D_GAME_DIR}/game_flow.cpp ${AS3D_GAME_DIR}/ui_script.cpp ${AS3D_GAME_DIR}/game_loop.cpp
  ${AS3D_GAME_DIR}/touch_overlay.cpp)
target_include_directories(as3d_web_game PRIVATE ${AS3D_GAME_DIR})
target_link_libraries(as3d_web_game PRIVATE ${AS3D_MODULES} ${AS3D_MODULES})
target_link_options(as3d_web_game PRIVATE ${AS3D_WEB_LINK})
set_target_properties(as3d_web_game PROPERTIES SUFFIX ".js")
