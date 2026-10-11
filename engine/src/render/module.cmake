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
# src/render/pica/shaders into one binary and bin2s embeds it into as3d_render
# (pica_all_shbin.h: `pica_all_shbin` and `pica_all_shbin_size`). The functions come from the
# devkitPro 3DS toolchain; the object library is linked straight into as3d_render.
if(NINTENDO_3DS AND AS3D_RENDER_BACKEND STREQUAL "pica")
  set(_as3d_pica_shaders ${CMAKE_CURRENT_SOURCE_DIR}/src/render/pica/shaders)
  # One binary with one entry point (DVLE) per program, in the order of shader_set.h's Shader enum
  # (Flat, Mesh, Terrain, Flat2, Env, Water): the programs share the code blob, so switching
  # programs uploads no shader code (round 3, R3.2). Embedded as pica_all_shbin.h.
  ctr_add_shader_library(as3d_pica_all_shbin OUTPUT pica_all.shbin
                         ${_as3d_pica_shaders}/flat.v.pica ${_as3d_pica_shaders}/mesh.v.pica
                         ${_as3d_pica_shaders}/terrain.v.pica ${_as3d_pica_shaders}/flat2.v.pica
                         ${_as3d_pica_shaders}/env.v.pica ${_as3d_pica_shaders}/water.v.pica)
  dkp_add_embedded_binary_library(as3d_pica_shaders as3d_pica_all_shbin)
  target_link_libraries(as3d_render PRIVATE as3d_pica_shaders)
  target_link_libraries(as3d_render PUBLIC citro3d ctru)
endif()
