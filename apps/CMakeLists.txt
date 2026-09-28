get_property(AS3D_MODULES GLOBAL PROPERTY AS3D_MODULES)

# Each app directory may carry its own CMakeLists.txt.
foreach(app game viewer rcsl_tool)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${app}/CMakeLists.txt)
    add_subdirectory(${app})
  endif()
endforeach()

if(AS3D_BUILD_TESTS)
  enable_testing()
  file(GLOB_RECURSE test_srcs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
  add_executable(as3d_tests ${test_srcs})
  target_include_directories(as3d_tests PRIVATE ${CMAKE_SOURCE_DIR}/third_party/doctest)
  # Listed twice so static libraries resolve regardless of dependency order.
  target_link_libraries(as3d_tests PRIVATE ${AS3D_MODULES} ${AS3D_MODULES})
  add_test(NAME as3d_tests COMMAND as3d_tests)
endif()
