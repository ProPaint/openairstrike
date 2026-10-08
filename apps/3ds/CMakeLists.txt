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

# game_session.cpp and audio_bridge.cpp are the apps/game files the 3DS app uses so far: GameStack
# and GameFlow need GameView, which holds the GL-side layers; the 3DS composes its frame itself
# (ctr_frame.cpp) and drives the AudioBridge itself (ctr_audio.cpp).
add_executable(openairstrike
  source/ctr_main.cpp
  source/ctr_loop.cpp
  source/ctr_frame.cpp
  source/ctr_input.cpp
  source/ctr_perf.cpp
  source/run_config.cpp
  source/main.cpp
  source/pica_test.cpp
  source/mesh_test.cpp
  source/probe.cpp
  source/render.cpp
  source/terrain_test.cpp
  source/fx_test.cpp
  source/ui_test.cpp
  source/audio_test.cpp
  source/ctr_audio.cpp
  ${AS3D_REPO_ROOT}/apps/game/game_session.cpp
  ${AS3D_REPO_ROOT}/apps/game/audio_bridge.cpp)
# The test modes and the composer use the render module's private headers (engine/src/render:
# world_scene.h; engine/src/render/pica, engine/src/ui/pica: the PICA backend's own); ctr_audio.cpp
# the ndsp device's counters (engine/src/audio/ndsp).
target_include_directories(openairstrike PRIVATE source ${AS3D_REPO_ROOT}/apps/game
                                                 ${AS3D_REPO_ROOT}/engine/src/render
                                                 ${AS3D_REPO_ROOT}/engine/src/platform/ctr
                                                 ${AS3D_REPO_ROOT}/engine/src/render/pica
                                                 ${AS3D_REPO_ROOT}/engine/src/ui/pica
                                                 ${AS3D_REPO_ROOT}/engine/src/audio/ndsp)
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
