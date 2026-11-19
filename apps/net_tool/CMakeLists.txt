# as3d_net_tool (docs/spec/issues/180-online-coop.md 13 "T3"): connects two rtc transports and
# measures them, without the game. Added from apps/CMakeLists.txt when the rtc backend is built.
#   as3d_net_tool pair [--stun none]   host and guest in one process (the smoke test)
#   as3d_net_tool host                 prints the invite code, reads the reply from stdin
#   as3d_net_tool join <invite>        prints the reply code
add_executable(as3d_net_tool main.cpp)
target_include_directories(as3d_net_tool PRIVATE ${AS3D_REPO_ROOT}/engine/src/net/rtc)
target_link_libraries(as3d_net_tool PRIVATE as3d_net as3d_core)
