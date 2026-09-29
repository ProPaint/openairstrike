# Headless simulation tool (WP-42a). Normally added from apps/CMakeLists.txt with
#   add_subdirectory(sim_tool)
# It can also be configured on its own, which builds just the engine and this tool:
#   cmake -S apps/sim_tool -B build/sim && cmake --build build/sim -j4 --target as3d_sim
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.22)
  project(as3d_sim LANGUAGES C CXX)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
  endif()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
  get_filename_component(AS3D_SIM_REPO ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
  set(AS3D_REPO_ROOT "${AS3D_SIM_REPO}")
  add_compile_definitions(AS3D_REPO_ROOT="${AS3D_SIM_REPO}")
  add_subdirectory(${AS3D_SIM_REPO}/engine ${CMAKE_BINARY_DIR}/engine)
endif()

add_executable(as3d_sim main.cpp)
# Headless: only the simulation modules, no render/platform/audio.
target_link_libraries(as3d_sim PRIVATE as3d_game as3d_script as3d_formats as3d_vfs as3d_core)
