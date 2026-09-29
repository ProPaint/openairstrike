# 231: `DetachEntity` on a definition child; the new null guards

Status: decided (engine choices). Raised by C3. Spec: rcsl-builtins-semantics.delta.md,
entries `DetachEntity`, `AttachEntity`, `RotateTo` (open question 5).

1. **`DetachEntity` on a definition child** (an entity built by an `attach` line, held in its
   parent's child array) is undefined in the original: the parent pointer is cleared while the
   child stays in the array. In our engine such a child is thought and freed only through its
   parent, so clearing its parent would leave it unreachable and never freed. We do nothing
   for a definition child. The shipped scripts detach only entities the boss attached with
   `AttachEntity` (VERIFIED-DATA in the delta).
2. **`AttachEntity` with a null parent** and **`RotateTo` / `lRotateTo` / `RotateToClamp` with a
   null target**: the sequels added exactly the guards our engine already applies to every
   null or stale reference (docs/spec/README.md, deviations), so both games share one
   implementation. `RotateTo` leaves the done flag at 0, so an `lRotateTo` on a null target
   waits for its timeout, as in the original.
