# Platform-independent modules: no SDL, no GL. Each is a static library built
# from every .cpp in its directory.
function(as3d_module name)
  file(GLOB_RECURSE srcs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/${name}/*.cpp)
  if(srcs)
    add_library(as3d_${name} STATIC ${srcs})
    target_include_directories(as3d_${name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
                                            PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/${name})
    target_compile_options(as3d_${name} PRIVATE -fno-exceptions -fno-rtti)
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
