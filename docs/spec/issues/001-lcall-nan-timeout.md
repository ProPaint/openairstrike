# 001: LCALL with a NaN timeout completes in the reference, the spec says it waits

Found by WP-40 (C++ script VM).

## Discrepancy

docs/spec/rcsl-vm.md, "LCALL and TMO", step 4: "`timeout = timeout - frametime` (rounded to
float); if the result is <= 0 the call is complete, else waiting (a NaN timeout waits forever)."

tools/ref/rcsl_vm.py (`_run`, opcode 0x1D) computes:

    th.timeout = f2b(t)
    complete = not (b2f(th.timeout) > 0.0)

For a NaN timeout (nonzero bits, so it passes the `th.timeout == 0` test) the subtraction gives
NaN, `NaN > 0.0` is False, so `complete` is True. The reference therefore completes the call on
its first execution; it does not wait forever. "<= 0" and "not > 0" differ exactly for NaN.
Reproduced: `timeout = nan; t = nan - 1/60; not (t > 0.0)` prints True.

## Impact

None on shipped data: no script produces a NaN timeout (all golden traces match). Only a
synthetic script with `TMO` of a NaN can observe it.

## Decision

The C++ VM implements the reference (`complete = !(timeout > 0)`), and a unit test in
apps/tests/script_test.cpp pins that behaviour. The spec prose should be corrected (the
original executable's behaviour with NaN is not established by this package).
