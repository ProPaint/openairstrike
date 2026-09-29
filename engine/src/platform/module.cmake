# platform: context creation and the SDL_RWops byte stream (as3d/platform.h). Owns the only
# dependency on EGL and SDL2 in the whole engine; `render` and everything above it stay
# portable.
if(ANDROID)
  # The window backend (sdl_window.cpp) is the Android backend. There is no headless EGL
  # backend on the device: android_stub.cpp stands in for egl_headless.cpp. The SDL2
  # target comes from SDL's own CMake project, added by android/app/src/main/cpp first.
  set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/egl_headless.cpp
    PROPERTIES HEADER_FILE_ONLY TRUE)
  target_link_libraries(as3d_platform PUBLIC as3d_core as3d_vfs SDL2 GLESv3 EGL android log)
else()
  set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/android_stub.cpp
    PROPERTIES HEADER_FILE_ONLY TRUE)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(AS3D_PLATFORM_SDL2 REQUIRED IMPORTED_TARGET sdl2)
  pkg_check_modules(AS3D_PLATFORM_EGL REQUIRED IMPORTED_TARGET egl)
  pkg_check_modules(AS3D_PLATFORM_GLESV2 REQUIRED IMPORTED_TARGET glesv2)
  target_link_libraries(as3d_platform PUBLIC as3d_core as3d_vfs
    PkgConfig::AS3D_PLATFORM_SDL2 PkgConfig::AS3D_PLATFORM_EGL PkgConfig::AS3D_PLATFORM_GLESV2)
endif()
