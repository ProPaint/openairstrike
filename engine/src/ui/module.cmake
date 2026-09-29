# ui: 2D batch renderer, font, HUD and hint box on top of the render module's GL wrappers.
# Needs a current GLES 3.0 context only for Renderer2D::init/flush.
target_link_libraries(as3d_ui PUBLIC as3d_core as3d_vfs as3d_formats as3d_render)
