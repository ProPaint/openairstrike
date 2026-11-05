target_link_libraries(as3d_formats PUBLIC as3d_core)
# The archive module (as3d/archive.h): libarchive and liblzma, off on the 3DS.
include(${CMAKE_CURRENT_LIST_DIR}/archive.cmake)
