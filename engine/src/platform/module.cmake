# platform: context creation (as3d/platform.h). Owns the only dependency on EGL and
# SDL2 in the whole engine; `render` and everything above it stay portable.
if(ANDROID)
  # Not implemented yet: only the dispatcher (platform.cpp) and the null-returning
  # stub are compiled. egl_headless.cpp/sdl_window.cpp need desktop-only libraries
  # (SDL2 is not linked on Android) and are excluded rather than built and unused.
  set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/egl_headless.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/sdl_window.cpp
    PROPERTIES HEADER_FILE_ONLY TRUE)
  target_link_libraries(as3d_platform PUBLIC as3d_core GLESv3 EGL android log)
else()
  set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/android_stub.cpp
    PROPERTIES HEADER_FILE_ONLY TRUE)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(AS3D_PLATFORM_SDL2 REQUIRED IMPORTED_TARGET sdl2)
  pkg_check_modules(AS3D_PLATFORM_EGL REQUIRED IMPORTED_TARGET egl)
  pkg_check_modules(AS3D_PLATFORM_GLESV2 REQUIRED IMPORTED_TARGET glesv2)
  target_link_libraries(as3d_platform PUBLIC as3d_core
    PkgConfig::AS3D_PLATFORM_SDL2 PkgConfig::AS3D_PLATFORM_EGL PkgConfig::AS3D_PLATFORM_GLESV2)
endif()
