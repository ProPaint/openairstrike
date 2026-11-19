# net: online two-player co-op (docs/spec/issues/180-online-coop.md). The neutral sources in
# src/net/ (protocol, codes, lockstep, loopback pair) have no dependency beyond as3d_core and are
# always built (the tests need them); the transport comes from src/net/${AS3D_NET_BACKEND}/:
# rtc (libdatachannel / datachannel-wasm, the third-party part in net.cmake) or null.
#
# AS3D_WITH_NET (0 or 1) is a public compile definition, so that apps/game can guard what pulls
# apps/game/net_play.h; AS3D_NET_RTC says which backend was built.
target_link_libraries(as3d_net PUBLIC as3d_core)
if(AS3D_WITH_NET)
  target_compile_definitions(as3d_net PUBLIC AS3D_WITH_NET=1)
else()
  target_compile_definitions(as3d_net PUBLIC AS3D_WITH_NET=0)
endif()
if(AS3D_NET_BACKEND STREQUAL "rtc")
  target_compile_definitions(as3d_net PUBLIC AS3D_NET_RTC=1)
else()
  target_compile_definitions(as3d_net PUBLIC AS3D_NET_RTC=0)
endif()
# The build id of the Hello check (net_common.cpp only: a new commit recompiles one file).
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/src/net/net_common.cpp PROPERTIES
  COMPILE_DEFINITIONS "AS3D_BUILD_ID=\"${AS3D_BUILD_ID}\"")
include(${CMAKE_CURRENT_LIST_DIR}/net.cmake)
