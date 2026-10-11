target_link_libraries(as3d_vfs PUBLIC as3d_core)
# The desktop's read-ahead thread (prefetch_thread.cpp, std::thread); the 3DS uses libctru's
# threads (engine/src/platform/ctr), the web none.
if(NOT NINTENDO_3DS AND NOT EMSCRIPTEN)
  find_package(Threads REQUIRED)
  target_link_libraries(as3d_vfs PUBLIC Threads::Threads)
endif()
