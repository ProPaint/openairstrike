# as3d_audio: sound effects + tracker music. See docs/audio.md.
#
# Depends on as3d_core, as3d_vfs, SDL2 (the output device on both desktop and
# Android) and libopenmpt (tracker/MO3 decoding), the last of which is built
# from source here rather than found on the system (no dev headers are
# installed on the reference host, and Android needs it built from source
# regardless).

target_link_libraries(as3d_audio PUBLIC as3d_core as3d_vfs)

if(NOT DEFINED AS3D_REPO_ROOT)
  set(AS3D_REPO_ROOT "${CMAKE_SOURCE_DIR}")
endif()

if(DEFINED ENV{AS3D_DATA_ROOT} AND NOT "$ENV{AS3D_DATA_ROOT}" STREQUAL "")
  set(AS3D_AUDIO_DATA_ROOT "$ENV{AS3D_DATA_ROOT}")
else()
  set(AS3D_AUDIO_DATA_ROOT "${AS3D_REPO_ROOT}")
endif()

# ---- SDL2 (output device) ----
if(ANDROID)
  # android/app/src/main/cpp/CMakeLists.txt add_subdirectory()s SDL2's own
  # CMake project before pulling in the engine tree, so the SDL2 target
  # already exists by the time this file runs.
  target_link_libraries(as3d_audio PUBLIC SDL2)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(AS3D_AUDIO_SDL2 REQUIRED IMPORTED_TARGET sdl2)
  target_link_libraries(as3d_audio PUBLIC PkgConfig::AS3D_AUDIO_SDL2)
endif()

# ---- libopenmpt (tracker/MO3 decoding), built from source ----
set(AS3D_OPENMPT_VERSION "0.8.9")
set(AS3D_OPENMPT_SRC "${AS3D_AUDIO_DATA_ROOT}/third_party_local/libopenmpt-${AS3D_OPENMPT_VERSION}+release")

if(NOT EXISTS "${AS3D_OPENMPT_SRC}/libopenmpt/libopenmpt.h")
  message(STATUS "as3d_audio: fetching libopenmpt ${AS3D_OPENMPT_VERSION} source via tools/fetch_third_party.sh")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "AS3D_DATA_ROOT=${AS3D_AUDIO_DATA_ROOT}"
            "${AS3D_REPO_ROOT}/tools/fetch_third_party.sh"
    RESULT_VARIABLE AS3D_AUDIO_FETCH_RESULT
  )
  if(NOT AS3D_AUDIO_FETCH_RESULT EQUAL 0)
    message(FATAL_ERROR "as3d_audio: tools/fetch_third_party.sh failed (exit code ${AS3D_AUDIO_FETCH_RESULT})")
  endif()
  if(NOT EXISTS "${AS3D_OPENMPT_SRC}/libopenmpt/libopenmpt.h")
    message(FATAL_ERROR "as3d_audio: libopenmpt source still missing at ${AS3D_OPENMPT_SRC} after fetching")
  endif()
endif()

# This file list is the exact source set libopenmpt's own `make` build
# compiles for `NO_ZLIB=1 NO_MPG123=1 NO_OGG=1 NO_VORBIS=1 NO_VORBISFILE=1
# NO_MINIZ=1 NO_MINIMP3=1` (verified by diffing this glob against that
# build's [CXX]/[CC] log; see docs/audio.md): common/, soundlib/ (incl.
# plugins/ and plugins/dmo/, used by some IT/XM effects), sounddsp/, the
# libopenmpt C/C++ API glue, and the bundled stb_vorbis.c fallback decoder
# that MO3's embedded Ogg Vorbis samples need (no system libvorbis is
# available on the reference host, and stb_vorbis avoids that dependency on
# Android too).
file(GLOB AS3D_OPENMPT_SRCS CONFIGURE_DEPENDS
  "${AS3D_OPENMPT_SRC}/common/*.cpp"
  "${AS3D_OPENMPT_SRC}/soundlib/*.cpp"
  "${AS3D_OPENMPT_SRC}/soundlib/plugins/*.cpp"
  "${AS3D_OPENMPT_SRC}/soundlib/plugins/dmo/*.cpp"
  "${AS3D_OPENMPT_SRC}/sounddsp/*.cpp"
  "${AS3D_OPENMPT_SRC}/libopenmpt/*.cpp"
)

if(NOT TARGET openmpt_static)
  find_package(Threads REQUIRED)

  add_library(openmpt_static STATIC
    ${AS3D_OPENMPT_SRCS}
    "${AS3D_OPENMPT_SRC}/include/stb_vorbis/stb_vorbis.c"
  )
  set_target_properties(openmpt_static PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
  )
  target_include_directories(openmpt_static PRIVATE
    "${AS3D_OPENMPT_SRC}"
    "${AS3D_OPENMPT_SRC}/src"
    "${AS3D_OPENMPT_SRC}/common"
    "${AS3D_OPENMPT_SRC}/include"
  )
  target_compile_definitions(openmpt_static PRIVATE
    LIBOPENMPT_BUILD
    MPT_WITH_STBVORBIS
    STB_VORBIS_NO_PULLDATA_API
    STB_VORBIS_NO_STDIO
  )
  # Third-party code: soundlib uses exceptions and RTTI throughout, unlike
  # the rest of this engine. Cancel the project-wide -fno-rtti (added in the
  # root CMakeLists.txt) for this target only; -fexceptions is the compiler
  # default but is set explicitly here for clarity.
  target_compile_options(openmpt_static PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-frtti>
    $<$<COMPILE_LANGUAGE:CXX>:-fexceptions>
    -Wno-unused-parameter
    -Wno-unused-variable
  )
  if(AS3D_SANITIZE)
    # A large, old third-party codebase: ASan/UBSan findings inside it are
    # not actionable here and would drown out reports from our own code, so
    # it is excluded from sanitizer instrumentation. See docs/audio.md.
    target_compile_options(openmpt_static PRIVATE -fno-sanitize=address,undefined)
  endif()
  target_link_libraries(openmpt_static PUBLIC Threads::Threads)
  if(UNIX AND NOT ANDROID)
    target_link_libraries(openmpt_static PUBLIC m)
  endif()
endif()

target_link_libraries(as3d_audio PUBLIC openmpt_static)
target_include_directories(as3d_audio PRIVATE "${AS3D_OPENMPT_SRC}")
