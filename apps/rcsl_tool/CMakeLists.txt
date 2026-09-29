get_property(AS3D_MODULES GLOBAL PROPERTY AS3D_MODULES)

add_executable(rcsl_tool main.cpp mock_host.cpp runner.cpp)
target_link_libraries(rcsl_tool PRIVATE ${AS3D_MODULES} ${AS3D_MODULES})
