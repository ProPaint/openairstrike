# apps/audio_tool: standalone command line tool for the audio module
# (WP-1A). Not wired into apps/CMakeLists.txt (owned by the orchestrator,
# which only add_subdirectory()s game/viewer/rcsl_tool); build it directly:
#   cmake -S apps/audio_tool -B build-audio-tool
#   cmake --build build-audio-tool
# One line would need to change in apps/CMakeLists.txt to fold it into the
# normal `apps/` build: add "audio_tool" to the
# `foreach(app game viewer rcsl_tool)` list.
cmake_minimum_required(VERSION 3.22)

get_directory_property(AS3D_AUDIO_TOOL_HAS_PARENT PARENT_DIRECTORY)

if(NOT AS3D_AUDIO_TOOL_HAS_PARENT)
    project(as3d_audio_tool LANGUAGES C CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE RelWithDebInfo)
    endif()
    get_filename_component(AS3D_AUDIO_TOOL_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
    add_compile_definitions(AS3D_REPO_ROOT="${AS3D_AUDIO_TOOL_REPO_ROOT}")
    add_subdirectory(${AS3D_AUDIO_TOOL_REPO_ROOT}/engine ${CMAKE_BINARY_DIR}/engine)
endif()

add_executable(audio_tool src/main.cpp)
target_link_libraries(audio_tool PRIVATE as3d_core as3d_vfs as3d_audio)
