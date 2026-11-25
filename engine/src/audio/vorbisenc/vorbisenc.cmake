# The Ogg Vorbis encoder of the 3DS music (as3d/ogg_vorbis_encoder.h; docs/3ds-port.md "M2.4
# Music"). Included by the projects that encode: apps/web (the music worker of "Prepare files
# for 3DS", always) and apps/audio_tool (when the source trees are there). Never part of the
# engine modules: the game itself only decodes.
#
# libogg and libvorbis are compiled here from the release source trees that
# `tools/fetch_third_party.sh vorbis` unpacks into ${AS3D_DATA_ROOT:-repo}/third_party_local/
# (SHA-256 pinned there). Encoder only: no vorbisfile. Same flags on every platform, and no fused
# multiply-add, so the desktop and WebAssembly builds compute the same samples.
#
# In: AS3D_REPO_ROOT; AS3D_VORBISENC_REQUIRED (fail when the trees are missing, else just
# report). Out: AS3D_VORBISENC_FOUND and, when found, the target as3d_vorbisenc (the encoder
# class, linking the two libraries).
if(TARGET as3d_vorbisenc)
  set(AS3D_VORBISENC_FOUND ON)
  return()
endif()

set(AS3D_LIBOGG_VERSION "1.3.6")
set(AS3D_LIBVORBIS_VERSION "1.3.7")
if(DEFINED ENV{AS3D_DATA_ROOT} AND NOT "$ENV{AS3D_DATA_ROOT}" STREQUAL "")
  set(_as3d_ve_root "$ENV{AS3D_DATA_ROOT}")
else()
  set(_as3d_ve_root "${AS3D_REPO_ROOT}")
endif()
set(AS3D_LIBOGG_SRC "${_as3d_ve_root}/third_party_local/libogg-${AS3D_LIBOGG_VERSION}")
set(AS3D_LIBVORBIS_SRC "${_as3d_ve_root}/third_party_local/libvorbis-${AS3D_LIBVORBIS_VERSION}")

if(NOT EXISTS "${AS3D_LIBOGG_SRC}/include/ogg/ogg.h" OR NOT EXISTS "${AS3D_LIBVORBIS_SRC}/lib/vorbisenc.c")
  set(AS3D_VORBISENC_FOUND OFF)
  if(AS3D_VORBISENC_REQUIRED)
    message(FATAL_ERROR "vorbisenc: libogg ${AS3D_LIBOGG_VERSION} or libvorbis ${AS3D_LIBVORBIS_VERSION} missing "
                        "under ${_as3d_ve_root}/third_party_local: run tools/fetch_third_party.sh vorbis")
  endif()
  message(STATUS "vorbisenc: no libogg/libvorbis source trees (tools/fetch_third_party.sh vorbis): not built")
  return()
endif()
set(AS3D_VORBISENC_FOUND ON)

# libogg's config_types.h, which its configure would write.
set(_as3d_ogg_gen "${CMAKE_BINARY_DIR}/as3d_libogg_include")
file(CONFIGURE OUTPUT "${_as3d_ogg_gen}/ogg/config_types.h" CONTENT
"#ifndef __CONFIG_TYPES_H__
#define __CONFIG_TYPES_H__
/* Written by engine/src/audio/vorbisenc/vorbisenc.cmake. */
#include <stdint.h>
typedef int16_t ogg_int16_t;
typedef uint16_t ogg_uint16_t;
typedef int32_t ogg_int32_t;
typedef uint32_t ogg_uint32_t;
typedef int64_t ogg_int64_t;
typedef uint64_t ogg_uint64_t;
#endif
" @ONLY)

set(_as3d_ve_flags -O2 -ffp-contract=off -w)
add_library(as3d_libogg STATIC "${AS3D_LIBOGG_SRC}/src/bitwise.c" "${AS3D_LIBOGG_SRC}/src/framing.c")
target_include_directories(as3d_libogg SYSTEM PUBLIC "${AS3D_LIBOGG_SRC}/include" "${_as3d_ogg_gen}")
target_compile_options(as3d_libogg PRIVATE ${_as3d_ve_flags})
set_target_properties(as3d_libogg PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(_as3d_vorbis_srcs mdct smallft block envelope window lsp lpc analysis synthesis psy info floor1 floor0 res0
    mapping0 registry codebook sharedbook lookup bitrate vorbisenc)
list(TRANSFORM _as3d_vorbis_srcs PREPEND "${AS3D_LIBVORBIS_SRC}/lib/")
list(TRANSFORM _as3d_vorbis_srcs APPEND ".c")
add_library(as3d_libvorbis STATIC ${_as3d_vorbis_srcs})
target_include_directories(as3d_libvorbis SYSTEM PUBLIC "${AS3D_LIBVORBIS_SRC}/include")
target_include_directories(as3d_libvorbis PRIVATE "${AS3D_LIBVORBIS_SRC}/lib")
target_compile_options(as3d_libvorbis PRIVATE ${_as3d_ve_flags})
target_link_libraries(as3d_libvorbis PUBLIC as3d_libogg)
if(UNIX AND NOT EMSCRIPTEN)
  target_link_libraries(as3d_libvorbis PUBLIC m)
endif()
set_target_properties(as3d_libvorbis PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(as3d_vorbisenc STATIC "${CMAKE_CURRENT_LIST_DIR}/ogg_vorbis_encoder.cpp")
target_include_directories(as3d_vorbisenc PUBLIC "${AS3D_REPO_ROOT}/engine/include")
target_compile_options(as3d_vorbisenc PRIVATE -fno-exceptions -ffp-contract=off)
target_link_libraries(as3d_vorbisenc PUBLIC as3d_libvorbis)
target_compile_definitions(as3d_vorbisenc INTERFACE AS3D_HAVE_VORBISENC=1)
message(STATUS "vorbisenc: libogg ${AS3D_LIBOGG_VERSION} and libvorbis ${AS3D_LIBVORBIS_VERSION} from ${_as3d_ve_root}/third_party_local")
