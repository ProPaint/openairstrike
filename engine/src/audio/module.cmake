# as3d_audio: sound effects + tracker music. See docs/audio.md.
#
# Depends on as3d_core, as3d_vfs, SDL2 (the output device on both desktop and
# Android) and libopenmpt (tracker/MO3 decoding), the last of which is built
# from source here rather than found on the system (no dev headers are
# installed on the reference host, and Android needs it built from source
# regardless).

# How a new backend plugs in:
#  - Device: put the IAudioDevice in audio/<backend>/ (picked by
#    AS3D_AUDIO_BACKEND, see engine/CMakeLists.txt) and define
#    `std::unique_ptr<IAudioDevice> as3d::audio::makePlatformAudioDevice()`
#    there (declared in audio_device.h); AudioBackend::Platform selects it.
#    The device pulls samples with Mixer::render() (float) or
#    Mixer::renderS16() (int16 interleaved stereo). The sdl backend returns
#    its SDL device, the ndsp backend (3DS) its ndsp device or null when
#    ndspInit fails. Link any backend libraries from this file.
#  - Music: a second axis, independent of the backend directory. The neutral
#    glob picks up both music_openmpt.cpp and music_stub.cpp; this file
#    removes them from the target and adds exactly one, chosen by
#    AS3D_WITH_OPENMPT (ON by default: libopenmpt is fetched, built and
#    linked; OFF: music_stub.cpp, MusicDecoder::load() fails cleanly and
#    there is no music). A backend with its own decoder adds another
#    music_*.cpp the same way.
option(AS3D_WITH_OPENMPT "Build libopenmpt and tracker music support" ON)

get_target_property(_as3d_audio_srcs as3d_audio SOURCES)
list(FILTER _as3d_audio_srcs EXCLUDE REGEX "/music_(openmpt|stub)\\.cpp$")
set_property(TARGET as3d_audio PROPERTY SOURCES ${_as3d_audio_srcs})
if(AS3D_WITH_OPENMPT)
  target_compile_definitions(as3d_audio PUBLIC AS3D_WITH_OPENMPT=1)
  target_sources(as3d_audio PRIVATE ${CMAKE_CURRENT_LIST_DIR}/music_openmpt.cpp)
else()
  target_compile_definitions(as3d_audio PUBLIC AS3D_WITH_OPENMPT=0)
  target_sources(as3d_audio PRIVATE ${CMAKE_CURRENT_LIST_DIR}/music_stub.cpp)
endif()

target_link_libraries(as3d_audio PUBLIC as3d_core as3d_vfs)

if(DEFINED ENV{AS3D_DATA_ROOT} AND NOT "$ENV{AS3D_DATA_ROOT}" STREQUAL "")
  set(AS3D_AUDIO_DATA_ROOT "$ENV{AS3D_DATA_ROOT}")
else()
  set(AS3D_AUDIO_DATA_ROOT "${AS3D_REPO_ROOT}")
endif()

# ---- ndsp (the 3DS output device, audio/ndsp/) ----
if(NINTENDO_3DS)
  target_link_libraries(as3d_audio PUBLIC ctru)
endif()

# ---- SDL2 (output device) ----
if(NOT NINTENDO_3DS)
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
endif()

if(AS3D_WITH_OPENMPT)
# ---- libopenmpt (tracker/MO3 decoding), built from source ----
set(AS3D_OPENMPT_VERSION "0.8.9")
set(AS3D_OPENMPT_SRC "${AS3D_AUDIO_DATA_ROOT}/third_party_local/libopenmpt-${AS3D_OPENMPT_VERSION}+release")
if(NINTENDO_3DS)
  # The 3DS build never fetches: the root CMakeLists.txt turned AS3D_WITH_OPENMPT on only when
  # tools/3ds_build.sh passed an existing source tree (docs/3ds-port.md M2.4).
  set(AS3D_OPENMPT_SRC "${AS3D_3DS_OPENMPT_SRC}")
endif()

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

if(NINTENDO_3DS)
  # Size (docs/3ds-port.md M2.4): every format loader of libopenmpt is reachable through the
  # loader table in Sndfile.cpp, so --gc-sections keeps all of them (about 2.4 MB of the 3dsx).
  # The games ship MO3 only; the 3DS build compiles the MO3 loader and the four formats MO3 is
  # made from (IT, XM, S3M, MOD, also the formats audio_tool recognises), and a generated file
  # gives every other format of the table a probe that fails and a loader that returns false.
  set(AS3D_OPENMPT_3DS_LOADERS mo3 it xm s3m mod)
  list(FILTER AS3D_OPENMPT_SRCS EXCLUDE REGEX "/soundlib/[Ll]oad_[^/]*\\.cpp$")
  set(_kept_formats "")
  foreach(_l ${AS3D_OPENMPT_3DS_LOADERS})
    set(_f "${AS3D_OPENMPT_SRC}/soundlib/Load_${_l}.cpp")
    list(APPEND AS3D_OPENMPT_SRCS "${_f}")
    file(STRINGS "${_f}" _defs REGEX "^bool CSoundFile::Read[A-Za-z0-9_]+\\(FileReader")
    foreach(_d ${_defs})
      string(REGEX REPLACE "^bool CSoundFile::Read([A-Za-z0-9_]+)\\(.*" "\\1" _n "${_d}")
      list(APPEND _kept_formats "${_n}")
    endforeach()
  endforeach()
  # The table's formats (those under MODPLUG_TRACKER, UAX WAV MID, are not in libopenmpt's).
  file(STRINGS "${AS3D_OPENMPT_SRC}/soundlib/Sndfile.cpp" _decls REGEX "^[ \t]*MPT_DECLARE_FORMAT\\([A-Za-z0-9_]+\\)")
  set(_stub "// Generated by engine/src/audio/module.cmake: format loaders left out of the 3DS build.\n")
  string(APPEND _stub "#include \"stdafx.h\"\n#include \"Loaders.h\"\n\nOPENMPT_NAMESPACE_BEGIN\n\n")
  set(_stubbed "")
  foreach(_d ${_decls})
    string(REGEX REPLACE "^[ \t]*MPT_DECLARE_FORMAT\\(([A-Za-z0-9_]+)\\).*" "\\1" _n "${_d}")
    if(_n STREQUAL "UAX" OR _n STREQUAL "WAV" OR _n STREQUAL "MID")
      continue()
    endif()
    list(FIND _kept_formats "${_n}" _i)
    if(_i GREATER -1)
      continue()
    endif()
    list(APPEND _stubbed "${_n}")
    string(APPEND _stub "CSoundFile::ProbeResult CSoundFile::ProbeFileHeader${_n}(MemoryFileReader, const uint64 *) { return ProbeFailure; }\n")
    string(APPEND _stub "bool CSoundFile::Read${_n}(FileReader &, ModLoadingFlags) { return false; }\n")
  endforeach()
  # Two sample codecs that SampleIO reaches only for the AMS and DMF loaders' sample encodings.
  string(APPEND _stub "void AMSUnpack(mpt::const_byte_span, mpt::byte_span, int8) {}\n")
  string(APPEND _stub "uintptr_t DMFUnpack(FileReader &, uint8 *, uint32) { return 0; }\n")
  string(APPEND _stub "\nOPENMPT_NAMESPACE_END\n")
  set(_stub_file "${CMAKE_CURRENT_BINARY_DIR}/openmpt_3ds_loader_stubs.cpp")
  file(CONFIGURE OUTPUT "${_stub_file}" CONTENT "${_stub}" @ONLY)
  list(APPEND AS3D_OPENMPT_SRCS "${_stub_file}")
  list(LENGTH _stubbed _ns)
  message(STATUS "as3d_audio: 3DS libopenmpt with loaders ${_kept_formats}; ${_ns} formats stubbed")
endif()

if(NOT TARGET openmpt_static)
  if(NOT NINTENDO_3DS)
    find_package(Threads REQUIRED)
  endif()

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
    "${AS3D_OPENMPT_SRC}/soundlib"
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
  if(NINTENDO_3DS)
    # newlib's locks are libctru's (docs/3ds-port.md M2.3); no pthread library to link.
    # -Os for size (the loaders, the file and pattern code: 1.5 MB at -O2, 1.2 MB at -Os), -O2
    # for the per-sample mixing path. libopenmpt's C API catches every exception before it
    # returns, so none crosses into the engine (built with -fno-exceptions).
    target_compile_options(openmpt_static PRIVATE -Os)
    set_source_files_properties(
      "${AS3D_OPENMPT_SRC}/soundlib/Fastmix.cpp" "${AS3D_OPENMPT_SRC}/soundlib/MixFuncTable.cpp"
      "${AS3D_OPENMPT_SRC}/soundlib/MixerLoops.cpp" "${AS3D_OPENMPT_SRC}/soundlib/Sndmix.cpp"
      "${AS3D_OPENMPT_SRC}/soundlib/Snd_flt.cpp" "${AS3D_OPENMPT_SRC}/soundlib/Snd_fx.cpp"
      "${AS3D_OPENMPT_SRC}/soundlib/WindowedFIR.cpp" "${AS3D_OPENMPT_SRC}/sounddsp/DSP.cpp"
      PROPERTIES COMPILE_OPTIONS "-O2")
  else()
    target_link_libraries(openmpt_static PUBLIC Threads::Threads)
  endif()
  if(UNIX AND NOT ANDROID AND NOT NINTENDO_3DS)
    target_link_libraries(openmpt_static PUBLIC m)
  endif()
endif()

target_link_libraries(as3d_audio PUBLIC openmpt_static)
target_include_directories(as3d_audio PRIVATE "${AS3D_OPENMPT_SRC}")
endif()
