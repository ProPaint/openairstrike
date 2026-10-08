# render: platform-independent GLES 3.0 rendering (as3d/gfx.h). Assumes a current GLES
# 3.0 context exists (created by the `platform` module); never touches EGL or SDL.
target_include_directories(as3d_render PRIVATE ${AS3D_REPO_ROOT}/third_party/stb)

# as3d_game is needed for as3d/defs.h (Material::fromObjectDef, SceneLighting::fromMission1
# read ObjectDef/DefDatabase; see material.cpp and scene_lighting.cpp).
if(NOT NINTENDO_3DS)
  if(ANDROID)
    target_link_libraries(as3d_render PUBLIC as3d_core as3d_formats as3d_game as3d_vfs GLESv3)
  else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(AS3D_RENDER_GLESV2 REQUIRED IMPORTED_TARGET glesv2)
    target_link_libraries(as3d_render PUBLIC as3d_core as3d_formats as3d_game as3d_vfs PkgConfig::AS3D_RENDER_GLESV2)
  endif()
endif()

# 3DS (PICA backend, engine/src/render/pica): picasso assembles the vertex shaders of
# src/render/pica/shaders and bin2s embeds them into as3d_render (flat_shbin.h,
# flat2_shbin.h, mesh_shbin.h, terrain_shbin.h: `<name>_shbin` and `<name>_shbin_size`). The functions come
# from the devkitPro 3DS toolchain; the object library is linked straight into as3d_render.
if(NINTENDO_3DS AND AS3D_RENDER_BACKEND STREQUAL "pica")
  set(_as3d_pica_shaders ${CMAKE_CURRENT_SOURCE_DIR}/src/render/pica/shaders)
  foreach(_s flat flat2 mesh terrain)
    ctr_add_shader_library(as3d_pica_${_s}_shbin OUTPUT ${_s}.shbin ${_as3d_pica_shaders}/${_s}.v.pica)
  endforeach()
  dkp_add_embedded_binary_library(as3d_pica_shaders as3d_pica_flat_shbin as3d_pica_flat2_shbin as3d_pica_mesh_shbin
                                  as3d_pica_terrain_shbin)
  target_link_libraries(as3d_render PRIVATE as3d_pica_shaders)
  target_link_libraries(as3d_render PUBLIC citro3d ctru)
endif()
