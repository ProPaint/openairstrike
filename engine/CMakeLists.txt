# Each module is a static library built from the .cpp files directly in src/<m>/ (the
# neutral part) plus those in src/<m>/<backend>/, where <backend> is AS3D_<M>_BACKEND for
# the modules that have backends (render, ui, audio, platform). Modules without a backend
# variable (core, vfs, ...) build only their own directory. Override with -DAS3D_<M>_BACKEND=.
if(NINTENDO_3DS)
  set(_as3d_backend_defaults pica pica ndsp ctr)
else()
  set(_as3d_backend_defaults gles gles sdl sdl)
endif()
list(GET _as3d_backend_defaults 0 _as3d_d_render)
list(GET _as3d_backend_defaults 1 _as3d_d_ui)
list(GET _as3d_backend_defaults 2 _as3d_d_audio)
list(GET _as3d_backend_defaults 3 _as3d_d_platform)
set(AS3D_RENDER_BACKEND "${_as3d_d_render}" CACHE STRING "render backend directory (gles, pica)")
set(AS3D_UI_BACKEND "${_as3d_d_ui}" CACHE STRING "ui backend directory (gles, pica)")
set(AS3D_AUDIO_BACKEND "${_as3d_d_audio}" CACHE STRING "audio backend directory (sdl, ndsp)")
set(AS3D_PLATFORM_BACKEND "${_as3d_d_platform}" CACHE STRING "platform backend directory (sdl, ctr)")

function(as3d_module name)
  string(TOUPPER ${name} NAME_UP)
  set(srcs_dirs ${CMAKE_CURRENT_SOURCE_DIR}/src/${name})
  set(globs ${CMAKE_CURRENT_SOURCE_DIR}/src/${name}/*.cpp)
  if(DEFINED AS3D_${NAME_UP}_BACKEND)
    list(APPEND globs ${CMAKE_CURRENT_SOURCE_DIR}/src/${name}/${AS3D_${NAME_UP}_BACKEND}/*.cpp)
  endif()
  file(GLOB srcs CONFIGURE_DEPENDS ${globs})
  if(srcs)
    add_library(as3d_${name} STATIC ${srcs})
    target_include_directories(as3d_${name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
                                            PRIVATE ${srcs_dirs})
    target_compile_options(as3d_${name} PRIVATE -fno-exceptions)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/${name}/module.cmake)
      include(${CMAKE_CURRENT_SOURCE_DIR}/src/${name}/module.cmake)
    endif()
    set_property(GLOBAL APPEND PROPERTY AS3D_MODULES as3d_${name})
  endif()
endfunction()

# Order matters only for readability; dependencies are declared in each
# module's optional module.cmake via target_link_libraries.
foreach(m core vfs formats script game audio input ui render platform)
  as3d_module(${m})
endforeach()
