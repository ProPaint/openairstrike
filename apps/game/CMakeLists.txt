# as3d_game: the playable game. Added from apps/CMakeLists.txt on desktop and from
# android/app/src/main/cpp/CMakeLists.txt on Android.
#
# as3d_game_shell holds everything the desktop executable and the Android app share: the
# session, renderer, audio bridge, touch overlay and the windowed game loop (game_loop.h).
# The session, view and audio bridge sources are also compiled into as3d_tests
# (apps/tests/game_integration_test.cpp includes them).
get_property(AS3D_MODULES GLOBAL PROPERTY AS3D_MODULES)

add_library(as3d_game_shell STATIC
  game_session.cpp game_view.cpp audio_bridge.cpp game_loop.cpp touch_overlay.cpp)
target_include_directories(as3d_game_shell PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Listed twice so static libraries resolve regardless of dependency order.
target_link_libraries(as3d_game_shell PUBLIC ${AS3D_MODULES} ${AS3D_MODULES})
if(ANDROID)
  target_link_libraries(as3d_game_shell PUBLIC SDL2 GLESv3)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(AS3D_GAME_SDL2 REQUIRED IMPORTED_TARGET sdl2)
  target_link_libraries(as3d_game_shell PUBLIC PkgConfig::AS3D_GAME_SDL2)
endif()

if(ANDROID)
  # The native library SDLActivity loads ("main", see GameActivity.getLibraries()).
  add_library(main SHARED android_main.cpp)
  target_link_libraries(main PRIVATE as3d_game_shell SDL2 SDL2main GLESv3 EGL log android)
else()
  # The target is as3d_game_app because the simulation module already owns the target name
  # as3d_game; the executable is still called as3d_game.
  add_executable(as3d_game_app main.cpp)
  set_target_properties(as3d_game_app PROPERTIES OUTPUT_NAME as3d_game)
  target_link_libraries(as3d_game_app PRIVATE as3d_game_shell)
endif()
