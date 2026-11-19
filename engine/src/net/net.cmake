# The third-party part of the net module (docs/spec/issues/180-online-coop.md 2.1, 12), like
# engine/src/formats/archive.cmake: with AS3D_NET_BACKEND=rtc, libdatachannel v0.24.6 (libjuice,
# usrsctp, plog) with Mbed TLS 3.6.8 on desktop and Android, datachannel-wasm on the web, fetched
# by tools/fetch_third_party.sh net into ${AS3D_DATA_ROOT}/third_party_local/ and added with
# add_subdirectory; linked into as3d_net. Nothing with the null backend.
#
#  - libdatachannel (MPL-2.0): its own CMake project, static (datachannel-static), with
#    NO_MEDIA, NO_WEBSOCKET, NO_EXAMPLES, NO_TESTS and USE_MBEDTLS; its submodules libjuice (MIT,
#    the ICE agent, no server), usrsctp (BSD-3, SCTP in user space) and plog (MIT, the logger)
#    come from the same tree. It uses RTTI and exceptions: -frtti on its own targets (the
#    repository builds with -fno-rtti), and transport_rtc.cpp, the one file that calls it, is
#    compiled with exceptions (the engine's modules are -fno-exceptions).
#  - Mbed TLS (Apache-2.0): its own CMake project, the three static libraries, no programs or
#    tests, with engine/src/net/rtc/mbedtls_user_config.h appended to the default configuration
#    (libdatachannel needs MBEDTLS_SSL_DTLS_SRTP for its DTLS setup even without media).
#  - datachannel-wasm (MIT, the web): the same rtc:: C++ API over the browser's RTCPeerConnection;
#    its --js-library files are link options of its target, so they reach as3d_web's link line
#    through as3d_net.
#
# The trees are fetched from here when they are missing (AS3D_NET_FETCH=0 in the environment
# forbids it: the configure step then fails with the command to run).
if(NOT AS3D_NET_BACKEND STREQUAL "rtc")
  return()
endif()

set(AS3D_LIBDATACHANNEL_VERSION "0.24.6")
set(AS3D_MBEDTLS_VERSION "3.6.8")
set(AS3D_DATACHANNEL_WASM_SHORT "f9200eb")
if(DEFINED ENV{AS3D_DATA_ROOT} AND NOT "$ENV{AS3D_DATA_ROOT}" STREQUAL "")
  set(_as3d_net_root "$ENV{AS3D_DATA_ROOT}")
else()
  set(_as3d_net_root "${AS3D_REPO_ROOT}")
endif()
set(AS3D_LIBDATACHANNEL_SRC "${_as3d_net_root}/third_party_local/libdatachannel-${AS3D_LIBDATACHANNEL_VERSION}")
set(AS3D_MBEDTLS_SRC "${_as3d_net_root}/third_party_local/mbedtls-${AS3D_MBEDTLS_VERSION}")
set(AS3D_DATACHANNEL_WASM_SRC "${_as3d_net_root}/third_party_local/datachannel-wasm-${AS3D_DATACHANNEL_WASM_SHORT}")

if(EMSCRIPTEN)
  set(_as3d_net_markers "${AS3D_DATACHANNEL_WASM_SRC}/.as3d_verified")
else()
  set(_as3d_net_markers "${AS3D_LIBDATACHANNEL_SRC}/.as3d_verified" "${AS3D_MBEDTLS_SRC}/include/mbedtls/ssl.h")
endif()
set(_as3d_net_missing OFF)
foreach(_m ${_as3d_net_markers})
  if(NOT EXISTS "${_m}")
    set(_as3d_net_missing ON)
  endif()
endforeach()
if(_as3d_net_missing)
  if("$ENV{AS3D_NET_FETCH}" STREQUAL "0")
    message(FATAL_ERROR "as3d_net: the WebRTC libraries are missing under ${_as3d_net_root}/third_party_local "
                        "and AS3D_NET_FETCH=0: run tools/fetch_third_party.sh net (or configure with -DAS3D_WITH_NET=OFF)")
  endif()
  message(STATUS "as3d_net: fetching libdatachannel ${AS3D_LIBDATACHANNEL_VERSION}, Mbed TLS ${AS3D_MBEDTLS_VERSION} and datachannel-wasm via tools/fetch_third_party.sh net")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "AS3D_DATA_ROOT=${_as3d_net_root}"
            "${AS3D_REPO_ROOT}/tools/fetch_third_party.sh" net
    RESULT_VARIABLE _as3d_net_fetch
  )
  if(NOT _as3d_net_fetch EQUAL 0)
    message(FATAL_ERROR "as3d_net: tools/fetch_third_party.sh net failed (exit code ${_as3d_net_fetch})")
  endif()
  foreach(_m ${_as3d_net_markers})
    if(NOT EXISTS "${_m}")
      message(FATAL_ERROR "as3d_net: ${_m} still missing after fetching")
    endif()
  endforeach()
endif()

set(_as3d_rtc_src "${CMAKE_CURRENT_LIST_DIR}/rtc/transport_rtc.cpp")

# Quiet, sanitizer-free third-party targets (as archive.cmake does for libarchive and liblzma).
function(_as3d_net_third_party)
  foreach(t ${ARGN})
    if(TARGET ${t})
      target_compile_options(${t} PRIVATE -w)
      if(AS3D_SANITIZE)
        target_compile_options(${t} PRIVATE -fno-sanitize=address,undefined)
      endif()
    endif()
  endforeach()
endfunction()

if(EMSCRIPTEN)
  # ---- datachannel-wasm (the web) ----
  if(NOT TARGET datachannel-wasm)
    add_subdirectory("${AS3D_DATACHANNEL_WASM_SRC}" "${CMAKE_BINARY_DIR}/third_party/datachannel-wasm" EXCLUDE_FROM_ALL)
    # It throws (std::runtime_error on a closed connection): native WebAssembly exceptions, as
    # libopenmpt in apps/web/CMakeLists.txt.
    target_compile_options(datachannel-wasm PRIVATE -fwasm-exceptions)
    _as3d_net_third_party(datachannel-wasm)
  endif()
  set_source_files_properties(${_as3d_rtc_src} PROPERTIES COMPILE_OPTIONS "-fexceptions;-fwasm-exceptions")
  target_link_libraries(as3d_net PRIVATE datachannel-wasm)
  return()
endif()

# ---- Mbed TLS ----
if(NOT TARGET mbedtls)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
  set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
  set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
  set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
  set(GEN_FILES OFF CACHE BOOL "" FORCE)
  set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)
  set(MBEDTLS_USER_CONFIG_FILE "${CMAKE_CURRENT_LIST_DIR}/rtc/mbedtls_user_config.h" CACHE FILEPATH "" FORCE)
  add_subdirectory("${AS3D_MBEDTLS_SRC}" "${CMAKE_BINARY_DIR}/third_party/mbedtls" EXCLUDE_FROM_ALL)
  _as3d_net_third_party(mbedtls mbedx509 mbedcrypto everest p256m)
  foreach(t mbedtls mbedx509 mbedcrypto everest p256m)
    if(TARGET ${t})
      set_target_properties(${t} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
  endforeach()
  # libdatachannel looks for MbedTLS::MbedTLS before find_package (USE_MBEDTLS). Imported, so
  # that libdatachannel's install(EXPORT) rules (never run here) do not ask for it in an export set.
  add_library(MbedTLS::MbedTLS INTERFACE IMPORTED GLOBAL)
  set_target_properties(MbedTLS::MbedTLS PROPERTIES INTERFACE_LINK_LIBRARIES "mbedtls;mbedx509;mbedcrypto")
endif()

# ---- libdatachannel, with libjuice, usrsctp and plog from its tree ----
if(NOT TARGET datachannel-static)
  set(NO_MEDIA ON CACHE BOOL "" FORCE)
  set(NO_WEBSOCKET ON CACHE BOOL "" FORCE)
  set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
  set(NO_TESTS ON CACHE BOOL "" FORCE)
  set(USE_MBEDTLS ON CACHE BOOL "" FORCE)
  set(USE_GNUTLS OFF CACHE BOOL "" FORCE)
  set(USE_NICE OFF CACHE BOOL "" FORCE)
  set(PREFER_SYSTEM_LIB OFF CACHE BOOL "" FORCE)
  set(NO_SERVER ON CACHE BOOL "" FORCE) # libjuice: no TURN/STUN server
  set(sctp_werror OFF CACHE BOOL "" FORCE)
  set(sctp_build_programs OFF CACHE BOOL "" FORCE)
  set(sctp_debug OFF CACHE BOOL "" FORCE)
  add_subdirectory("${AS3D_LIBDATACHANNEL_SRC}" "${CMAKE_BINARY_DIR}/third_party/libdatachannel" EXCLUDE_FROM_ALL)
  # The repository builds with -fno-rtti (root CMakeLists.txt); libdatachannel needs RTTI
  # (dynamic_cast in its description and channel code). Target options come after the
  # directory's, so -frtti wins.
  target_compile_options(datachannel-static PRIVATE -frtti -fexceptions)
  _as3d_net_third_party(datachannel-static juice-static usrsctp)
endif()

set_source_files_properties(${_as3d_rtc_src} PROPERTIES COMPILE_OPTIONS "-fexceptions")
target_link_libraries(as3d_net PRIVATE LibDataChannel::LibDataChannelStatic)
