target_link_libraries(as3d_vfs PUBLIC as3d_core)

# IStream/IFileSource are pure-interface classes (every virtual member is
# either pure or an inline `= default` destructor), so they have no "key
# function" and their vtable is emitted as a weak definition in every
# translation unit that uses them. This module is built with -fno-rtti (see
# as3d_module in engine/CMakeLists.txt), but consumers such as as3d_tests are
# not, so under UBSan's -fsanitize=vptr the vtable/typeinfo emitted here can
# disagree with the one emitted at a caller's call site, producing spurious
# "invalid vptr" reports even though dispatch is correct. Disable just that
# check for this module and anything that links it; every other sanitizer
# check still applies.
if(AS3D_SANITIZE)
  target_compile_options(as3d_vfs PUBLIC -fno-sanitize=vptr)
endif()
