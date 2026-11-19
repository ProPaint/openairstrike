# The third-party part of the net module (docs/spec/issues/180-online-coop.md 2.1, 12), like
# engine/src/formats/archive.cmake: with AS3D_NET_BACKEND=rtc, libdatachannel v0.24.6 (libjuice,
# usrsctp, plog) with Mbed TLS 3.6.8 on desktop and Android, datachannel-wasm on the web, fetched
# by tools/fetch_third_party.sh net into ${AS3D_DATA_ROOT}/third_party_local/ and added with
# add_subdirectory; linked into as3d_net. Nothing with the null backend.
#
# T3: everything below. Until then the rtc backend is a stub (engine/src/net/rtc/transport_rtc.cpp)
# and nothing third-party is fetched or linked.
if(NOT AS3D_NET_BACKEND STREQUAL "rtc")
  return()
endif()
