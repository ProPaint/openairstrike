# 032: the return register is per thread in our VM

Status: open, needs a VM change to close. Raised by WP-42a.

## Original behaviour

rcsl-vm.md quirk 9 and rcsl-builtins-semantics.md D3: the return register (0x1fa7dfc) is
one engine global shared by all threads. `create` writes the new reference to it before
the new entity's `init` and first `main` run, and nothing saves it around nested handlers,
so a RET or a value-returning builtin in any nested handler (`create`, `Shoot`,
`callback`, `AttachCallback`, `ParentCallback`, `Damage`, `RadialDamage`,
`TraceLineDamage`, `Lightning`, `RespawnPlayer`) replaces the value the calling CALL
receives. Example given by the semantics spec: the player's `callback` handler ends with
`RET t0`, so the item script's `callback($player, 2, id, 0)` CALL receives that value.

## Our engine

`ScriptThread` keeps `retregBits_` per thread (engine/include/as3d/script.h). A builtin
returns through `BuiltinArgs::setReturnBits`, which only reaches the calling thread. The
game host therefore cannot reproduce the leak: `create` always returns the new reference,
and builtins with return kind "none" leave the caller's own previous value.

## Impact

None observed: missions 1..20 run 3600 frames (idle and with the as3d_sim bot) without a VM error, and the
shipped scripts that use `create`'s result (to attach parts or store references) would
have crashed the original if the leak had replaced the value. Scripts that ignore the
result of `callback` are unaffected.

## Proposed change (for the VM owner)

Make the return register shareable: for example an optional `u32* sharedRetreg` given to
`ScriptThread` (or owned by the host and reached through `IScriptHost`), used instead of
the member when set. The golden trace test (mock host, one thread per run) is unaffected.
The game host would then write the reference before running `init` in `create`.
