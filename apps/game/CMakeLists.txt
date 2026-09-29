# as3d_game: the playable game (window, or headless for tests). Added from
# apps/CMakeLists.txt. The target is as3d_game_app because the simulation module already
# owns the target name as3d_game; the executable is still called as3d_game. The session,
# view and audio bridge sources are also compiled into as3d_tests
# (apps/tests/game_integration_test.cpp includes them).
add_executable(as3d_game_app main.cpp game_session.cpp game_view.cpp audio_bridge.cpp)
set_target_properties(as3d_game_app PROPERTIES OUTPUT_NAME as3d_game)
# Listed twice so static libraries resolve regardless of dependency order.
target_link_libraries(as3d_game_app PRIVATE ${AS3D_MODULES} ${AS3D_MODULES})
