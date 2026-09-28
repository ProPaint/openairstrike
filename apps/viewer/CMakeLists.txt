# as3d_viewer: command-line tool exercising platform/render/math (WP-19). Commands
# self-register (see registry.h); adding one only needs a new .cpp file here picked up
# by the glob below, no edits to this file.
file(GLOB_RECURSE AS3D_VIEWER_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(as3d_viewer ${AS3D_VIEWER_SRCS})

get_property(AS3D_MODULES GLOBAL PROPERTY AS3D_MODULES)
# Listed twice, like apps/tests, so the static libraries resolve regardless of order.
target_link_libraries(as3d_viewer PRIVATE ${AS3D_MODULES} ${AS3D_MODULES})

# Viewer commands issue GL draw calls directly (glDrawArrays/glDrawElements), which
# as3d::render deliberately does not wrap (draw topology/count is caller-specific).
if(NOT ANDROID)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(AS3D_VIEWER_GLESV2 REQUIRED IMPORTED_TARGET glesv2)
  target_link_libraries(as3d_viewer PRIVATE PkgConfig::AS3D_VIEWER_GLESV2)
endif()
