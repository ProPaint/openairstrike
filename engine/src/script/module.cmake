target_link_libraries(as3d_script PUBLIC as3d_core)
# Traces are compared bit for bit against the Python reference interpreter: the compiler
# must not fuse or reorder floating-point operations (no -ffast-math is set anywhere in
# this tree; this additionally forbids contracting a*b+c into a single fused multiply-add,
# which would change rounding relative to tools/ref/rcsl_vm.py's arithmetic).
target_compile_options(as3d_script PRIVATE -ffp-contract=off)
