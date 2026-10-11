get_property(AS3D_MODULES GLOBAL PROPERTY AS3D_MODULES)

# Each app directory may carry its own CMakeLists.txt.
foreach(app game viewer rcsl_tool sim_tool audio_tool)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${app}/CMakeLists.txt)
    add_subdirectory(${app})
  endif()
endforeach()

if(AS3D_BUILD_TESTS)
  enable_testing()
  file(GLOB_RECURSE test_srcs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
  # The platform-neutral pieces of the PICA backend (engine/src/render/pica, otherwise built
  # for the 3DS only), tested on the desktop by tests/pica_core_test.cpp; the 3DS app's soak
  # verdict and log-push framing (apps/3ds/source/soak_report.cpp, tests/soak_report_test.cpp).
  set(pica_neutral_srcs ${AS3D_REPO_ROOT}/engine/src/render/pica/linear_arena.cpp
                        ${AS3D_REPO_ROOT}/engine/src/render/pica/tev.cpp
                        ${AS3D_REPO_ROOT}/apps/3ds/source/soak_report.cpp
                        ${AS3D_REPO_ROOT}/apps/3ds/source/wash_detect.cpp)
  add_executable(as3d_tests ${test_srcs} ${pica_neutral_srcs})
  target_include_directories(as3d_tests PRIVATE ${CMAKE_SOURCE_DIR}/third_party/doctest
                                                ${AS3D_REPO_ROOT}/engine/src/render/pica)
  # Listed twice so static libraries resolve regardless of dependency order.
  target_link_libraries(as3d_tests PRIVATE ${AS3D_MODULES} ${AS3D_MODULES})
  add_test(NAME as3d_tests COMMAND as3d_tests)
endif()
