# Each module is a static library built from the .cpp files directly in src/<m>/ (the
# neutral part) plus those in src/<m>/<backend>/, where <backend> is AS3D_<M>_BACKEND for
# the modules that have backends (render, ui, audio, platform). Modules without a backend
# variable (core, vfs, ...) build only their own directory. Override with -DAS3D_<M>_BACKEND=.
if(NINTENDO_3DS)
  set(_as3d_backend_defaults pica pica ndsp ctr)
else()
  set(_as3d_backend_defaults gles gles sdl sdl)
endif()
list(GET _as3d_backend_defaults 0 _as3d_d_render)
list(GET _as3d_backend_defaults 1 _as3d_d_ui)
list(GET _as3d_backend_defaults 2 _as3d_d_audio)
list(GET _as3d_backend_defaults 3 _as3d_d_platform)
set(AS3D_RENDER_BACKEND "${_as3d_d_render}" CACHE STRING "render backend directory (gles, pica)")
set(AS3D_UI_BACKEND "${_as3d_d_ui}" CACHE STRING "ui backend directory (gles, pica)")
set(AS3D_AUDIO_BACKEND "${_as3d_d_audio}" CACHE STRING "audio backend directory (sdl, ndsp)")
set(AS3D_PLATFORM_BACKEND "${_as3d_d_platform}" CACHE STRING "platform backend directory (sdl, ctr)")

# No fused multiply-add in any engine module (docs/spec/issues/180-online-coop.md 7.1): the
# simulation must compute the same bits on x86-64, arm64, wasm and the 3DS. Here too (not only in
# the root CMakeLists.txt) because the web project and the standalone apps/sim_tool add this
# directory directly; directory scope, so it reaches every module whatever CMAKE_CXX_FLAGS say
# (it comes after them on the command line).
add_compile_options(-ffp-contract=off)

# Online two-player co-op (docs/spec/issues/180-online-coop.md 12). Here rather than in the root
# CMakeLists.txt because every platform build passes through this file: the desktop and Android
# (the root adds it), the web (apps/web adds the engine directly) and the 3DS. AS3D_WITH_NET is
# ON by default off the 3DS and forced OFF there; AS3D_NET_BACKEND is the net module's backend
# directory (rtc, null), forced to null without AS3D_WITH_NET. The neutral net sources are
# built either way (engine/src/net/module.cmake).
if(NINTENDO_3DS)
  set(AS3D_WITH_NET OFF CACHE BOOL "Online co-op (WebRTC data channels)" FORCE)
else()
  option(AS3D_WITH_NET "Online co-op (WebRTC data channels)" ON)
endif()
set(AS3D_NET_BACKEND "rtc" CACHE STRING "net backend directory (rtc, null)")
if(NOT AS3D_WITH_NET)
  set(AS3D_NET_BACKEND "null" CACHE STRING "net backend directory (rtc, null)" FORCE)
endif()
# The build id two online peers must share (section 3 "Versioning"): `git describe --tags --always
# --dirty` of the repository at configure time, "unknown" without git. Recomputed at every
# configure (an internal cache entry, so apps can read it too); only engine/src/net/net_common.cpp
# is compiled with it.
set(_as3d_build_id "unknown")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${AS3D_REPO_ROOT}/.git")
  # The commit alone (12 hex digits), not `git describe`: a release built from its tag and the
  # Pages site built from main at the same commit must give the same id, or their players
  # refuse each other online ("different version"). "-dirty" when the tree has changes.
  execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
                  WORKING_DIRECTORY "${AS3D_REPO_ROOT}"
                  OUTPUT_VARIABLE _as3d_git_describe OUTPUT_STRIP_TRAILING_WHITESPACE
                  RESULT_VARIABLE _as3d_git_result ERROR_QUIET)
  if(_as3d_git_result EQUAL 0 AND NOT _as3d_git_describe STREQUAL "")
    execute_process(COMMAND "${GIT_EXECUTABLE}" diff --quiet HEAD
                    WORKING_DIRECTORY "${AS3D_REPO_ROOT}" RESULT_VARIABLE _as3d_git_dirty ERROR_QUIET)
    if(NOT _as3d_git_dirty EQUAL 0)
      string(APPEND _as3d_git_describe "-dirty")
    endif()
    set(_as3d_build_id "${_as3d_git_describe}")
  endif()
endif()
set(AS3D_BUILD_ID "${_as3d_build_id}" CACHE INTERNAL "build id of the online Hello check")
message(STATUS "as3d: online co-op ${AS3D_WITH_NET} (net backend ${AS3D_NET_BACKEND}), build id ${AS3D_BUILD_ID}")

function(as3d_module name)
  string(TOUPPER ${name} NAME_UP)
  set(srcs_dirs ${CMAKE_CURRENT_SOURCE_DIR}/src/${name})
  set(globs ${CMAKE_CURRENT_SOURCE_DIR}/src/${name}/*.cpp)
  if(DEFINED AS3D_${NAME_UP}_BACKEND)
    list(APPEND globs ${CMAKE_CURRENT_SOURCE_DIR}/src/${name}/${AS3D_${NAME_UP}_BACKEND}/*.cpp)
  endif()
  file(GLOB srcs CONFIGURE_DEPENDS ${globs})
  if(srcs)
    add_library(as3d_${name} STATIC ${srcs})
    target_include_directories(as3d_${name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
                                            PRIVATE ${srcs_dirs})
    target_compile_options(as3d_${name} PRIVATE -fno-exceptions)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/${name}/module.cmake)
      include(${CMAKE_CURRENT_SOURCE_DIR}/src/${name}/module.cmake)
    endif()
    set_property(GLOBAL APPEND PROPERTY AS3D_MODULES as3d_${name})
  endif()
endfunction()

# Order matters only for readability; dependencies are declared in each
# module's optional module.cmake via target_link_libraries.
foreach(m core net vfs formats script game audio input ui render platform)
  as3d_module(${m})
endforeach()
