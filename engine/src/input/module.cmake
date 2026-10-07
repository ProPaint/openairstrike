# input: keyboard/mouse action mapping, input scripts and the test pilot (as3d/input.h).
# Uses the simulation's PlayerInput (as3d_game) and SDL2's scancode constants (headers only;
# no SDL function is called here).
target_link_libraries(as3d_input PUBLIC as3d_game as3d_core)
if(NINTENDO_3DS)
  # input_mapper.cpp (keyboard/mouse) is the one file that needs SDL's scancode headers.
  get_target_property(_as3d_input_srcs as3d_input SOURCES)
  list(FILTER _as3d_input_srcs EXCLUDE REGEX "/input_mapper\\.cpp$")
  set_property(TARGET as3d_input PROPERTY SOURCES ${_as3d_input_srcs})
else()
  if(ANDROID)
    target_link_libraries(as3d_input PRIVATE SDL2)
  else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(AS3D_INPUT_SDL2 REQUIRED IMPORTED_TARGET sdl2)
    target_link_libraries(as3d_input PRIVATE PkgConfig::AS3D_INPUT_SDL2)
  endif()
endif()
