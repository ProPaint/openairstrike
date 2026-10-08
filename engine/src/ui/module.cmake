# ui: 2D batch renderer, font, HUD and hint box on top of the render module's GL wrappers.
# Needs a current GLES 3.0 context only for Renderer2D::init/flush.
target_link_libraries(as3d_ui PUBLIC as3d_core as3d_vfs as3d_formats as3d_render)
# The PICA flush (src/ui/pica/renderer2d_pica.cpp) draws through the render module's PICA frame
# interface (pica_frame.h, shader_set.h, tev.h).
if(NINTENDO_3DS AND AS3D_UI_BACKEND STREQUAL "pica")
  target_include_directories(as3d_ui PRIVATE ${AS3D_REPO_ROOT}/engine/src/render/pica)
  target_link_libraries(as3d_ui PUBLIC citro3d ctru)
endif()
