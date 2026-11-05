# The archive module of as3d_formats (as3d/archive.h, engine/src/formats/archive/,
# docs/spec/archive.md): the player's archives and installers for the importer. Desktop,
# Android and the web; never the 3DS, which does not import (AS3D_WITH_ARCHIVES is forced off
# there and the sources under archive/ are not compiled; as3d/zip_reader.h stays for it).
#
# Third-party code, built from source like libopenmpt (audio/module.cmake):
#  - liblzma (xz ${AS3D_XZ_VERSION}, 0BSD): xz's own CMake project with the decoders only
#    (LZMA1, LZMA2 and the BCJ filters that 7z and Inno Setup use), no encoders, no threads,
#    no tools, no NLS. The Inno Setup reader uses its raw decoders directly.
#  - libarchive ${AS3D_LIBARCHIVE_VERSION} (BSD-2): a source list (the read side: zip, RAR 4
#    and 5, 7z, tar, CAB, ISO 9660, with the gzip filter) and the hand-written configuration
#    header archive/libarchive_config.h (PLATFORM_CONFIG_H), the same on glibc, bionic and
#    Emscripten: no OpenSSL, libxml2, expat, bzip2, zstd, lz4, lzo, iconv, ACLs or xattrs.
#  - zlib: the system's on the desktop, the NDK's libz on Android, Emscripten's port on the
#    web (apps/web/CMakeLists.txt adds -sUSE_ZLIB=1).
# The source trees are fetched into ${AS3D_DATA_ROOT}/third_party_local/ by
# tools/fetch_third_party.sh archive (pinned SHA-256), from here when they are missing.
if(NINTENDO_3DS)
  set(AS3D_WITH_ARCHIVES OFF)
else()
  option(AS3D_WITH_ARCHIVES "Read the player's archives and installers (libarchive, liblzma)" ON)
endif()

if(NOT AS3D_WITH_ARCHIVES)
  target_compile_definitions(as3d_formats PUBLIC AS3D_WITH_ARCHIVES=0)
  return()
endif()
target_compile_definitions(as3d_formats PUBLIC AS3D_WITH_ARCHIVES=1)

set(AS3D_LIBARCHIVE_VERSION "3.8.11")
set(AS3D_XZ_VERSION "5.8.4")
if(DEFINED ENV{AS3D_DATA_ROOT} AND NOT "$ENV{AS3D_DATA_ROOT}" STREQUAL "")
  set(_as3d_archive_root "$ENV{AS3D_DATA_ROOT}")
else()
  set(_as3d_archive_root "${AS3D_REPO_ROOT}")
endif()
set(AS3D_LIBARCHIVE_SRC "${_as3d_archive_root}/third_party_local/libarchive-${AS3D_LIBARCHIVE_VERSION}")
set(AS3D_XZ_SRC "${_as3d_archive_root}/third_party_local/xz-${AS3D_XZ_VERSION}")

if(NOT EXISTS "${AS3D_LIBARCHIVE_SRC}/libarchive/archive.h" OR NOT EXISTS "${AS3D_XZ_SRC}/src/liblzma/api/lzma.h")
  message(STATUS "as3d_formats: fetching libarchive ${AS3D_LIBARCHIVE_VERSION} and xz ${AS3D_XZ_VERSION} via tools/fetch_third_party.sh archive")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "AS3D_DATA_ROOT=${_as3d_archive_root}"
            "${AS3D_REPO_ROOT}/tools/fetch_third_party.sh" archive
    RESULT_VARIABLE _as3d_archive_fetch
  )
  if(NOT _as3d_archive_fetch EQUAL 0)
    message(FATAL_ERROR "as3d_formats: tools/fetch_third_party.sh archive failed (exit code ${_as3d_archive_fetch})")
  endif()
  if(NOT EXISTS "${AS3D_LIBARCHIVE_SRC}/libarchive/archive.h" OR NOT EXISTS "${AS3D_XZ_SRC}/src/liblzma/api/lzma.h")
    message(FATAL_ERROR "as3d_formats: libarchive or xz source still missing under ${_as3d_archive_root}/third_party_local after fetching")
  endif()
endif()

# ---- zlib ----
if(ANDROID)
  # The NDK ships libz with its headers in the sysroot.
  add_library(as3d_zlib INTERFACE)
  target_link_libraries(as3d_zlib INTERFACE z)
elseif(EMSCRIPTEN)
  # Emscripten's port: -sUSE_ZLIB=1 at compile and link time puts zlib.h on the include path
  # and the library on the link line (apps/web/CMakeLists.txt adds the link flag too).
  add_library(as3d_zlib INTERFACE)
  target_compile_options(as3d_zlib INTERFACE "-sUSE_ZLIB=1")
  target_link_options(as3d_zlib INTERFACE "-sUSE_ZLIB=1")
else()
  find_package(ZLIB)
  if(ZLIB_FOUND)
    add_library(as3d_zlib INTERFACE)
    target_link_libraries(as3d_zlib INTERFACE ZLIB::ZLIB)
  else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(AS3D_ZLIB REQUIRED IMPORTED_TARGET zlib)
    add_library(as3d_zlib INTERFACE)
    target_link_libraries(as3d_zlib INTERFACE PkgConfig::AS3D_ZLIB)
  endif()
endif()

# ---- liblzma, through xz's CMake project ----
if(NOT TARGET liblzma)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(XZ_NLS OFF CACHE BOOL "" FORCE)
  set(XZ_THREADS no CACHE STRING "" FORCE)
  set(XZ_SMALL OFF CACHE BOOL "" FORCE)
  set(XZ_ENCODERS "" CACHE STRING "" FORCE)
  set(XZ_DECODERS "lzma1;lzma2;delta;x86;powerpc;ia64;arm;armthumb;arm64;sparc;riscv" CACHE STRING "" FORCE)
  set(XZ_CHECKS "crc32;crc64;sha256" CACHE STRING "" FORCE)
  set(XZ_MICROLZMA_ENCODER OFF CACHE BOOL "" FORCE)
  set(XZ_MICROLZMA_DECODER OFF CACHE BOOL "" FORCE)
  set(XZ_LZIP_DECODER OFF CACHE BOOL "" FORCE)
  set(XZ_TOOL_XZ OFF CACHE BOOL "" FORCE)
  set(XZ_TOOL_XZDEC OFF CACHE BOOL "" FORCE)
  set(XZ_TOOL_LZMADEC OFF CACHE BOOL "" FORCE)
  set(XZ_TOOL_LZMAINFO OFF CACHE BOOL "" FORCE)
  set(XZ_TOOL_SCRIPTS OFF CACHE BOOL "" FORCE)
  set(XZ_DOC OFF CACHE BOOL "" FORCE)
  set(XZ_DOXYGEN OFF CACHE BOOL "" FORCE)
  add_subdirectory("${AS3D_XZ_SRC}" "${CMAKE_BINARY_DIR}/third_party/xz" EXCLUDE_FROM_ALL)
  set_target_properties(liblzma PROPERTIES POSITION_INDEPENDENT_CODE ON)
  # xz exports its API headers only for an installed package: its users get them from the tree
  # here (BEFORE, so that a system lzma.h of another version is never the one compiled against).
  target_include_directories(liblzma BEFORE INTERFACE "$<BUILD_INTERFACE:${AS3D_XZ_SRC}/src/liblzma/api>")
  target_compile_definitions(liblzma INTERFACE LZMA_API_STATIC)
  if(AS3D_SANITIZE)
    target_compile_options(liblzma PRIVATE -fno-sanitize=address,undefined)
  endif()
endif()

# ---- libarchive, from its sources ----
if(NOT TARGET as3d_libarchive)
  set(_la "${AS3D_LIBARCHIVE_SRC}/libarchive")
  set(_la_srcs
    archive_acl.c archive_check_magic.c archive_cmdline.c archive_crc32.c archive_cryptor.c
    archive_digest.c archive_entry.c archive_entry_copy_stat.c archive_entry_link_resolver.c
    archive_entry_sparse.c archive_entry_stat.c archive_entry_strmode.c archive_entry_xattr.c
    archive_hmac.c archive_match.c archive_options.c archive_pack_dev.c archive_parse_date.c
    archive_pathmatch.c archive_ppmd7.c archive_ppmd8.c archive_rb.c
    archive_read.c archive_read_add_passphrase.c archive_read_append_filter.c
    archive_read_data_into_fd.c archive_read_open_fd.c archive_read_open_file.c
    archive_read_open_filename.c archive_read_open_memory.c archive_read_set_format.c
    archive_read_set_options.c archive_read_support_filter_none.c
    archive_read_support_filter_gzip.c archive_read_support_filter_xz.c
    archive_read_support_format_7zip.c archive_read_support_format_cab.c
    archive_read_support_format_iso9660.c archive_read_support_format_rar.c
    archive_read_support_format_rar5.c archive_read_support_format_tar.c
    archive_read_support_format_zip.c archive_string.c archive_string_sprintf.c archive_time.c
    archive_util.c archive_version_details.c archive_virtual.c archive_blake2sp_ref.c
    archive_blake2s_ref.c)
  list(TRANSFORM _la_srcs PREPEND "${_la}/")
  add_library(as3d_libarchive STATIC ${_la_srcs})
  target_include_directories(as3d_libarchive PUBLIC "${_la}" PRIVATE "${CMAKE_CURRENT_LIST_DIR}/archive")
  target_compile_definitions(as3d_libarchive
    PUBLIC LIBARCHIVE_STATIC
    PRIVATE PLATFORM_CONFIG_H="libarchive_config.h" __LIBARCHIVE_BUILD=1)
  if(ANDROID)
    # archive.h includes android_lf.h (64-bit file offsets on bionic) from libarchive's tree.
    target_include_directories(as3d_libarchive PUBLIC "${AS3D_LIBARCHIVE_SRC}/contrib/android/include")
  endif()
  target_compile_options(as3d_libarchive PRIVATE -w)
  set_target_properties(as3d_libarchive PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_link_libraries(as3d_libarchive PUBLIC liblzma as3d_zlib)
  if(AS3D_SANITIZE)
    target_compile_options(as3d_libarchive PRIVATE -fno-sanitize=address,undefined)
  endif()
endif()

file(GLOB _as3d_archive_srcs CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/archive/*.cpp")
target_sources(as3d_formats PRIVATE ${_as3d_archive_srcs})
target_link_libraries(as3d_formats PUBLIC as3d_vfs PRIVATE as3d_libarchive liblzma as3d_zlib)
