get_property(AS3D_MODULES GLOBAL PROPERTY AS3D_MODULES)

# The Nintendo 3DS app (docs/3ds-port.md). Configure with the devkitPro toolchain file, see
# tools/3ds_build.sh: cmake -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/3DS.cmake
# Engine modules come from engine/CMakeLists.txt with the pica/ndsp/ctr backend defaults.

# Flags of the spike. -ffp-contract=off: the script VM must not fuse a*b+c (script/module.cmake).
add_compile_options(-O2 -g -ffp-contract=off)
add_link_options(-Wl,--gc-sections)

# The vertex shader, assembled by picasso and embedded as scene_shbin / scene_shbin_size.
ctr_add_shader_library(scene_shbin OUTPUT scene.shbin source/scene.v.pica)
dkp_add_embedded_binary_library(scene_shbin_data scene_shbin)

add_executable(openairstrike
  source/main.cpp
  source/probe.cpp
  source/render.cpp)
target_include_directories(openairstrike PRIVATE source)
target_link_libraries(openairstrike PRIVATE scene_shbin_data ${AS3D_MODULES} ${AS3D_MODULES}
                                            citro3d ctru m)
target_compile_options(openairstrike PRIVATE -fno-exceptions)
set_target_properties(openairstrike PROPERTIES SUFFIX ".elf")

ctr_generate_smdh(openairstrike.smdh
  NAME "OpenAirStrike"
  DESCRIPTION "Open source engine for AirStrike 3D"
  AUTHOR "ProPaint"
  ICON ${CMAKE_CURRENT_SOURCE_DIR}/icon.png)
ctr_create_3dsx(openairstrike
  SMDH ${CMAKE_CURRENT_BINARY_DIR}/openairstrike.smdh)
