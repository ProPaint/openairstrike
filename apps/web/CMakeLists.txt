# The web version (docs/web.md, docs/spec/issues/150): the engine compiled to WebAssembly with
# Emscripten, drawing through WebGL 2. A standalone CMake project, never part of the desktop
# build, Android or tools/ci.sh. Configure only through tools/web_build.sh (emcmake).
#
#   as3d_web: the whole game (apps/game's session, view, audio bridge, touch overlay, flow
#             and windowed loop) with the page's entry point web_main.cpp. The page itself
#             is in site/.
#
# The engine modules are used unchanged: their pkg-config lookups (sdl2, egl, glesv2) resolve
# to the stub .pc files in pkgconfig/, which turn into Emscripten's -sUSE_SDL=2 and WebGL 2
# flags. The platform module's headless EGL backend is swapped for its null stand-in.
cmake_minimum_required(VERSION 3.22)
project(as3d_web LANGUAGES C CXX)

if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "apps/web builds only with Emscripten: run tools/web_build.sh")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra -Wno-unused-parameter)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
# No fused multiply-add (docs/spec/issues/180-online-coop.md 7.1; wasm has no FMA instruction
# without relaxed-simd, but the option keeps the promise explicit, as in the root CMakeLists.txt).
add_compile_options(-ffp-contract=off)

get_filename_component(AS3D_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_compile_definitions(AS3D_REPO_ROOT="${AS3D_REPO_ROOT}")

# Emscripten's toolchain file points pkg-config at its sysroot (again on every try_compile);
# a wrapper pins it to the stubs.
set(PKG_CONFIG_EXECUTABLE "${CMAKE_CURRENT_LIST_DIR}/pkgconfig/pkg-config-web" CACHE FILEPATH "" FORCE)
set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH OFF)

add_subdirectory(${AS3D_REPO_ROOT}/engine ${CMAKE_BINARY_DIR}/engine)
get_property(AS3D_MODULES GLOBAL PROPERTY AS3D_MODULES)

# No headless EGL backend in a browser: build the platform module's null stand-in instead.
set_source_files_properties(${AS3D_REPO_ROOT}/engine/src/platform/sdl/egl_headless.cpp
  DIRECTORY ${AS3D_REPO_ROOT}/engine PROPERTIES HEADER_FILE_ONLY TRUE)
set_source_files_properties(${AS3D_REPO_ROOT}/engine/src/platform/sdl/android_stub.cpp
  DIRECTORY ${AS3D_REPO_ROOT}/engine PROPERTIES HEADER_FILE_ONLY FALSE)

# libopenmpt throws and catches C++ exceptions: native WebAssembly exception handling.
if(TARGET openmpt_static)
  target_compile_options(openmpt_static PRIVATE -fwasm-exceptions)
endif()

set(AS3D_WEB_LINK
  -sUSE_SDL=2
  -sUSE_ZLIB=1
  -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB
  -sSTACK_SIZE=4MB
  -sFORCE_FILESYSTEM=1
  -sEXIT_RUNTIME=0
  -sENVIRONMENT=web
  -fwasm-exceptions
  -lidbfs.js
  # The paks' reference entry lists (tools/pak_entries/: names only, no data), for the importer
  # of as3d_unpack: a pak of another build and a copy without pak0.apk (issue 170 section 2).
  "--embed-file=${AS3D_REPO_ROOT}/tools/pak_entries@/pak_entries"
  # HEAPU32: datachannel-wasm's rtcCreatePeerConnection reads the STUN list through
  # Module["HEAPU32"]; without it every session with a STUN server threw "Cannot read properties
  # of undefined (reading 'BYTES_PER_ELEMENT')" while gathering (the walks run with --stun none).
  "-sEXPORTED_RUNTIME_METHODS=['callMain','FS','HEAPU8','HEAPU32','addRunDependency','removeRunDependency','ccall']"
)
if(AS3D_WEB_FULL_ES3)
  list(APPEND AS3D_WEB_LINK -sFULL_ES3=1)
endif()

set(AS3D_GAME_DIR ${AS3D_REPO_ROOT}/apps/game)
add_executable(as3d_web web_main.cpp
  ${AS3D_GAME_DIR}/game_session.cpp ${AS3D_GAME_DIR}/game_view.cpp ${AS3D_GAME_DIR}/audio_bridge.cpp
  ${AS3D_GAME_DIR}/game_flow.cpp ${AS3D_GAME_DIR}/ui_script.cpp ${AS3D_GAME_DIR}/game_loop.cpp
  ${AS3D_GAME_DIR}/touch_overlay.cpp ${AS3D_GAME_DIR}/game_stack.cpp ${AS3D_GAME_DIR}/launcher_screen.cpp)
target_include_directories(as3d_web PRIVATE ${AS3D_GAME_DIR})
# Online co-op (docs/spec/issues/180-online-coop.md 8.4, 12): AS3D_WITH_NET and the net backend
# come from engine/CMakeLists.txt (ON, rtc). NetPlay here; T3 adds datachannel-wasm (its
# add_subdirectory and --js-library link flag) below this line.
if(AS3D_WITH_NET)
  target_sources(as3d_web PRIVATE ${AS3D_GAME_DIR}/net_play.cpp)
  # datachannel-wasm's JS library (wasm/js/webrtc.js) calls _malloc and _free without declaring
  # them as dependencies: without the exports every SDP, candidate and message callback threw
  # "ReferenceError: _free is not defined" (found by the final test's online walks).
  list(APPEND AS3D_WEB_LINK "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free']")
endif()
target_link_libraries(as3d_web PRIVATE ${AS3D_MODULES} ${AS3D_MODULES})
target_link_options(as3d_web PRIVATE ${AS3D_WEB_LINK})
set_target_properties(as3d_web PROPERTIES SUFFIX ".js")

# as3d_music: the 3DS music of "Prepare files for 3DS" (docs/web.md "Files for the 3DS"), a
# module of its own that the page loads only when the player asks for those files, in a Web
# Worker (site/music_worker.js): the pak reader, libopenmpt, the shared render of audio_tool
# render-music (engine/src/audio/music_prerender.cpp) and the Ogg Vorbis encoder (libvorbis,
# engine/src/audio/vorbisenc/). Not the audio module itself: that one brings SDL's output.
set(AS3D_VORBISENC_REQUIRED ON)
include(${AS3D_REPO_ROOT}/engine/src/audio/vorbisenc/vorbisenc.cmake)
add_executable(as3d_music music_main.cpp
  ${AS3D_REPO_ROOT}/engine/src/audio/music_prerender.cpp
  ${AS3D_REPO_ROOT}/engine/src/audio/music_openmpt.cpp)
# libopenmpt's source root (the C API header), among openmpt_static's include directories.
get_target_property(_as3d_openmpt_dirs openmpt_static INCLUDE_DIRECTORIES)
foreach(_d ${_as3d_openmpt_dirs})
  if(EXISTS "${_d}/libopenmpt/libopenmpt.h")
    set(_as3d_openmpt_inc "${_d}")
  endif()
endforeach()
target_include_directories(as3d_music PRIVATE ${AS3D_REPO_ROOT}/engine/include ${AS3D_REPO_ROOT}/engine/src/audio
  ${_as3d_openmpt_inc})
target_compile_options(as3d_music PRIVATE -fno-exceptions)
target_link_libraries(as3d_music PRIVATE as3d_vfs as3d_core openmpt_static as3d_vorbisenc)
target_link_options(as3d_music PRIVATE
  -sENVIRONMENT=worker
  -sMODULARIZE=1 -sEXPORT_NAME=AS3DMusic
  --no-entry
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=32MB
  -sSTACK_SIZE=1MB
  -sFORCE_FILESYSTEM=1
  -lworkerfs.js
  -fwasm-exceptions
  "-sEXPORTED_RUNTIME_METHODS=['FS','ccall']")
set_target_properties(as3d_music PROPERTIES SUFFIX ".js")
