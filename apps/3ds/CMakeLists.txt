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

# The apps/game files the 3DS app shares with the desktop: the session, the audio bridge, and the
# front end's GameFlow and GameStack, which reach the renderer only through the FlowView seam
# (flow_view.h): the desktop's GameView (game_view.cpp, GLES) is not built here, the frame
# composer (ctr_frame.cpp) is the 3DS's view (docs/3ds-port.md M2.1).
add_executable(openairstrike
  source/ctr_main.cpp
  source/ctr_loop.cpp
  source/ctr_menu_loop.cpp
  source/ctr_selector.cpp
  source/ctr_install.cpp
  source/ctr_fatal.cpp
  source/ctr_name_entry.cpp
  source/ctr_mem.cpp
  source/ctr_frame.cpp
  source/ctr_banner.cpp
  source/ctr_input.cpp
  source/ctr_perf.cpp
  source/ctr_soak.cpp
  source/ctr_report.cpp
  source/soak_report.cpp
  source/wash_detect.cpp
  source/ctr_wash.cpp
  source/build_info.cpp
  source/run_config.cpp
  source/quality_profile.cpp
  source/main.cpp
  source/pica_test.cpp
  source/mesh_test.cpp
  source/shadow_test.cpp
  source/mark_test.cpp
  source/water_test.cpp
  source/sequel_test.cpp
  source/env_test.cpp
  source/ctr_save.cpp
  source/probe.cpp
  source/render.cpp
  source/terrain_test.cpp
  source/fx_test.cpp
  source/ui_test.cpp
  source/audio_test.cpp
  source/music_test.cpp
  source/ctr_audio.cpp
  source/key_glyphs.cpp
  ${AS3D_REPO_ROOT}/apps/game/game_session.cpp
  ${AS3D_REPO_ROOT}/apps/game/audio_bridge.cpp
  ${AS3D_REPO_ROOT}/apps/game/game_flow.cpp
  ${AS3D_REPO_ROOT}/apps/game/game_stack.cpp)
# The test modes and the composer use the render module's private headers (engine/src/render:
# world_scene.h; engine/src/render/pica, engine/src/ui/pica: the PICA backend's own); ctr_audio.cpp
# the ndsp device's counters (engine/src/audio/ndsp).
target_include_directories(openairstrike PRIVATE source ${AS3D_REPO_ROOT}/apps/game
                                                 ${AS3D_REPO_ROOT}/engine/src/render
                                                 ${AS3D_REPO_ROOT}/engine/src/platform/ctr
                                                 ${AS3D_REPO_ROOT}/engine/src/render/pica
                                                 ${AS3D_REPO_ROOT}/engine/src/ui/pica
                                                 ${AS3D_REPO_ROOT}/engine/src/audio/ndsp)
# The console menus' key pictures: apps/3ds/assets/keys.inc (from keys.png, original art of this
# project, by tools/3ds_key_glyphs.py), included by source/key_glyphs.cpp.
target_include_directories(openairstrike PRIVATE assets)
target_link_libraries(openairstrike PRIVATE scene_shbin_data ${AS3D_MODULES} ${AS3D_MODULES}
                                            citro3d ctru m)
target_compile_options(openairstrike PRIVATE -fno-exceptions)
# The commit logged as build= (tools/3ds_build.sh passes it; only build_info.cpp sees it, so a new
# commit recompiles one file).
set(AS3D_GIT_COMMIT "" CACHE STRING "commit of the 3DS build (logged as build=)")
if(AS3D_GIT_COMMIT)
  set_source_files_properties(source/build_info.cpp PROPERTIES COMPILE_DEFINITIONS "AS3D_GIT_COMMIT=\"${AS3D_GIT_COMMIT}\"")
endif()
set_target_properties(openairstrike PROPERTIES SUFFIX ".elf")

# The Homebrew Launcher's and the HOME menu's titles (the CIA takes the same SMDH,
# tools/3ds_package.sh): short title, long title, publisher; the project's own icon.
ctr_generate_smdh(openairstrike.smdh
  NAME "OpenAirStrike"
  DESCRIPTION "OpenAirStrike: open source engine for AirStrike 3D and its sequels"
  AUTHOR "ProPaint / openairstrike"
  ICON ${CMAKE_CURRENT_SOURCE_DIR}/icon.png)
ctr_create_3dsx(openairstrike
  SMDH ${CMAKE_CURRENT_BINARY_DIR}/openairstrike.smdh)
