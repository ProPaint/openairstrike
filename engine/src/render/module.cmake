# render: platform-independent GLES 3.0 rendering (as3d/gfx.h). Assumes a current GLES
# 3.0 context exists (created by the `platform` module); never touches EGL or SDL.
target_include_directories(as3d_render PRIVATE ${CMAKE_SOURCE_DIR}/third_party/stb)

if(ANDROID)
  target_link_libraries(as3d_render PUBLIC as3d_core as3d_formats as3d_game as3d_vfs GLESv3)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(AS3D_RENDER_GLESV2 REQUIRED IMPORTED_TARGET glesv2)
  target_link_libraries(as3d_render PUBLIC as3d_core as3d_formats as3d_game as3d_vfs PkgConfig::AS3D_RENDER_GLESV2)
endif()
