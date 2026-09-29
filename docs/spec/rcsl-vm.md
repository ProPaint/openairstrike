# RCSL virtual machine: runtime contract

Spec version 1.1. Reference implementation: `tools/ref/rcsl_vm.py` (mock host), test
`tools/ref/test_rcsl_vm.py`. Companions: [rcsl-container.md](rcsl-container.md) (file
format, operand encoding), [rcsl-opcodes-v0.md](rcsl-opcodes-v0.md) (per-opcode semantics),
[rcsl-builtins-table.md](rcsl-builtins-table.md) (builtins).

Addresses are virtual addresses in `AirStrike3D.exe` v1.70. VERIFIED-DATA claims hold for
all 339 shipped scripts; "mock run" means the 600-frame standard run of
`test_rcsl_vm.py`.

## Summary for implementers

- One thread per scripted entity (attached children included). Nothing runs at thread
  creation; `init` runs when the entity is spawned.
- Each frame the engine updates the entities newest first; an entity's update runs its
  `main` thread (resumed where it stopped), then its children's updates. After the entity
  pass come the touch pass (`touch` handlers) and the projectile/area-damage pass
  (`damage` handlers). `damage` and `callback` handlers also run synchronously whenever a
  builtin (`Damage`, `callback`, ...) triggers them.
- Handlers run to completion inside the frame; they save and restore the pc and slots
  t0..t15 of the thread, nothing else.
- Waiting is done only by LCALL (with TMO for a duration in seconds); every completed
  LCALL ends the thread's work for the frame.

## Per-frame update (VERIFIED-CODE, frame function 0x408df0)

1. Camera and scroll (0x40c260): camera scroll speed = scroll factor × 42;
   `g_map_pos` += scroll speed × frametime; the camera follows the player(s).
2. Map spawner (0x407590): each map entry whose spawn line has entered the window is
   created (0x40a080 → object builder 0x409ba0, which also creates the threads), receives
   its player index (+0x77), yaw, waypoint path and wait time, and its `init` runs at once
   (0x404fa0; after the entity itself, recursively for its attached children, each child
   first inheriting the parent's player index).
3. Player logic (0x40b440).
4. Entity pass (0x405cc0):
   1. entities removed during an earlier frame are freed together with their threads
      (0x404590) once no attachment references them;
   2. for each top-level entity, newest first (new entities are inserted at the head of
      the list at 0x458cc8): the visibility state machine (+0x1A: 1 = spawned but not yet
      in the window, 0 = in play, 2 = left the window, then removed), then the entity
      update 0x405a60 unless it already ran this frame;
   3. the touch pass 0x4055d0 over all top-level entities (see `touch`), then the
      two-player push-apart.
5. Projectiles and particles (0x414600; not while paused), which can deal area damage
   (0x404cb0 → damage routine).
6. Rendering.

**Entity update 0x405a60.** Skipped when the entity is removed. Otherwise: mark updated;
if the game is not paused: field 1 (age) += frametime and the internal time since the
last damage (+0x6F) += frametime, then **main** is dispatched when all of these hold: the
active bit (+0x1E & 0x4) is set, the visibility state is not 1, the parent (for a child)
allowed scripts, and the script has a `main` entry. Then attachment placement (children:
world origin = parent origin + field 8..10 in the parent's axes), terrain and water
snapping (field 3 flags), view test, render submission, and finally the updates of the
children, in attach order.

The active bit is set at creation, cleared and set by `deactivate`/`activate` (recursive,
0x404a90/0x404a40), and cleared by `MoveToNextWP` at the end of its path. Deactivated
entities still move, render, get touched and take damage; only `main` stops.

While the game is paused (0x458eab, also set by `EndLevel`) no main runs, age does not
advance and `time` is held.

**Main.** The dispatcher (0x41a200, kind 0) runs the interpreter from the thread's
current pc without saving anything. When the invocation returns it looks at the
instruction at the pc: if it is END or RET, the pc is reset to the `main` entry, so `main`
starts over on the next update; otherwise (a waiting or just completed LCALL) the thread
resumes there on the next update. "Done" for `main` therefore means reaching END or RET;
there is no other end state, and a main without any LCALL runs from its entry to END once
per frame. VERIFIED-CODE.

**`create`** (0x41a7a0) builds the new entity and its thread, runs its `init`
(0x41a8b0) and one full entity update including its `main` (0x41a968) synchronously,
inside the script that called `create`. The new entity was inserted at the head of the
list, so the running entity pass does not visit it again this frame. `create` writes the
new entity into the return register before running that `init`, and nothing saves the
register, so an `init` that executes RET or a value-returning builtin changes what the
caller of `create` receives (VERIFIED-CODE; see the builtins table).

## Event dispatch

Every dispatch (0x41a200 and its inlined copies at 0x404fa0, 0x404ae0, 0x4051d0,
0x41ac10, 0x41ada0, 0x41ae80) does: save the globals current thread, `self` and argument
pointer; set them to the target entity's thread, entity + 0x7B and its frame; for
handlers other than main, save the thread's pc and slots t0..t15, set pc = entry, run the
interpreter, restore the pc and t0..t15; restore the three globals. Slots 16 and up, the
PUSH/POP stack, the latent timeout and the return register are not saved. A handler whose
entry is 0xFFFFFFFF is not run. VERIFIED-CODE.

| Entry | Triggered by | Globals set before | Restored after |
|---|---|---|---|
| `init` (0) | 0x404fa0: map spawner (0x4077a4), player spawn (0x40b3f1), `create` (0x41a8b0), projectiles created by `Shoot`, the reward object spawned on a kill (0x40bb20). Recurses into attached children. Sets the internal time-since-damage to 2.0 first. | none | — |
| `main` (1) | every entity update, see above | none | nothing saved (see Main) |
| `damage` (2) | damage routine 0x404ae0(amount, attacker's player index), from `Damage`, area damage 0x404cb0 (`RadialDamage`, projectiles), `TraceLineDamage`, `Lightning`. Skipped entirely when field 4 (dead) ≠ 0, when the entity is frozen (+0x1E & 0x10, `FreezeHealth`) or is an invulnerable player. Before the handler: time-since-damage = 0, field 34 (health) −= amount. After the handler: if health ≤ 0, kill accounting, field 4 = 1.0, item drop and score. | none (`other` is **not** set) | — |
| `touch` (3) | touch pass 0x4051d0, only for top-level entities that have a collision mode (+0x5B ≠ 0, from the object definition) and are in view. Modes 2 and 3 first test the live players' boxes: on the first overlap `other` = that player, the entity's player index (+0x77) = that player's index, the handler runs and the entity's touch pass ends. Then, for every mode except 2, each other top-level entity that is in view, not removed and not frozen (modes 1 and 3: only class 2.0 enemies) is tested, and the handler runs once per overlapping entity with `other` = that entity; the pass stops when the handler removed self. | `other` (and +0x77 for players) | `other` restored to its previous value; +0x77 not restored |
| `callback` (4) | builtins `callback` (0x41ae80), `AttachCallback` (0x41ac10), `ParentCallback` (0x41ada0) | `cb_msg`, `cb_parm1`, `cb_parm2` = the caller's t1, t2, t3 | not restored |

All of these are VERIFIED-CODE (addresses as given). The names `init` and `touch` are ours.

Consequences: handlers run synchronously and to completion; `other` holds a stale value
(initially 0) outside touch handlers; `cb_*` keep the last callback's values; the damage
handler sees the health after the subtraction and before the dead flag is set, even on the
lethal hit, so scripts test health themselves.

## Globals (table at 0x457220, VERIFIED-CODE unless marked)

Each record is `{name, address for player index 0, address for player index 1}`. The
interpreter picks the address with the player index at `self`'s entity + 0x77
(0x419bf4). That index is the entity's associated player: 0 in a one-player game; in
two-player mode (flag 0x1fdb2c7) the map spawner assigns 0 or 1 at random among players
still in the game; `create`, `Shoot` and `init` propagate the creator's index to new
objects and children; the touch dispatcher sets it to the touching player's index. Only
`player` and the `p_*` globals have different addresses for the two players; the players'
records are at 0x1ebe308 + 0x171 × index (their key bindings are the [Controls] and
[Controls2] sections of config.ini).

"Scripts" counts the shipped scripts that read (r) or write (w) the global (VERIFIED-DATA).

| # | Name | Type | Scripts | Engine value | Address(es) |
|---|---|---|---|---|---|
| 0 | `self` | entity reference | r 318 | the running entity | 0x1fa7df0 |
| 1 | `other` | entity reference | r 76 (always as `Damage` target) | the toucher, set by the touch dispatch only | 0x1fa7de4 |
| 2 | `cb_msg` | float | r 34 | callback argument 1 | 0x1fa7dec |
| 3 | `cb_parm1` | float | r 3 | callback argument 2 | 0x1fa7de8 |
| 4 | `cb_parm2` | float | unused | callback argument 3 | 0x1fa7df8 |
| 5 | `player` | entity reference | r 117 | the player entity of this index | 0x1fa7dd8 / 0x1fa7ddc |
| 6 | `p_action` | float (bit mask) | rw 4 | player record +0x8C, action/input bits (bit meanings GUESS) | 0x1ebe394 / 0x1ebe505 |
| 7 | `p_scores` | float | rw 2 | player record +0x90, score | 0x1ebe398 / 0x1ebe509 |
| 8 | `p_lives` | float | rw 4 | record +0x98, lives | 0x1ebe3a0 / 0x1ebe511 |
| 9 | `p_stars` | float | rw 1 | record +0x9C | 0x1ebe3a4 / 0x1ebe515 |
| 10 | `p_speedfactor` | float | rw 5 | record +0xAC (meaning GUESS) | 0x1ebe3b4 / 0x1ebe525 |
| 11 | `p_counter1` | float | w 3 | record +0xA0, script-owned counter (meaning GUESS) | 0x1ebe3a8 / 0x1ebe519 |
| 12 | `p_counter2` | float | rw 5 | record +0xA4, script-owned counter (meaning GUESS) | 0x1ebe3ac / 0x1ebe51d |
| 13 | `p_counter3` | float | unused | record +0xA8, set to 1.0 at level start | 0x1ebe3b0 / 0x1ebe521 |
| 14 | `p_weapon` | float | rw 3 | record +0xB0, weapon index | 0x1ebe3b8 / 0x1ebe529 |
| 15 | `l_night` | float 0/1 | unused | night level flag | 0x458eac |
| 16 | `l_water` | float 0/1 | r 1 | level has water | 0x4d52b8 |
| 17 | `l_waterlevel` | float | r 20 | water height | 0x4d52c0 |
| 18 | `frametime` | float, seconds | r 87 | min(elapsed ms, 100) × 0.001 (0x420970) | 0x1fb4bcc |
| 19 | `time` | float, seconds | unused | accumulated game time | 0x1fb4bd0 |
| 20 | `camera` | pointer (not an entity) | LEA 59 | constant 0x1ebe5f4, the camera structure | 0x456f6c |
| 21 | `g_map_pos` | float | r 33 | scroll position | 0x4d52d8 |
| 22 | `g_damage_factor` | float | unused | difficulty table: 0.5, 0.7, 0.8, 1.25, 1.4 | 0x4577c0 |
| 23 | `g_health_factor` | float | unused | difficulty table: 0.3, 0.5, 0.75, 1.5, 2.0 | 0x4577c4 |

Difficulty index at 0x4577bc (default 2), number of players at 0x4577b8.

## Entity references and fields

**Representation (VERIFIED-CODE).** An entity is a 0x1E3-byte structure. A slot or
global holding an entity reference holds the address entity + 0x7B, and the 4 bytes at
that address (field 0) hold the entity's own address. `self` is set to entity + 0x7B by
every dispatch; `create` returns entity + 0x7B (0x41a876); the player globals hold player
entity + 0x7B (0x40b3d9); builtins taking an entity dereference the reference once to get
the entity (`remove` 0x41a985, `callback` 0x41ae88); `getentity` (0x41a9e0) turns a raw
entity pointer into a reference. `camera` is different: it holds the address of the
camera structure.

**Field access.** `LEA t = &base[k]` reads the pointer held by `base` (a global's value or
a slot's value) and adds 4k; the following instruction reads or writes through `[t]`. A
second LEA on the result selects a vector component (`&t[2]` is the z of a vector field).
So field k of an entity is the 4 bytes at entity + 0x7B + 4k, k = 0..89 lie inside the
structure.

**Entity field map.** Counts are scripts reading / writing (the larger count over the
vector components), from a static dataflow over all scripts that follows LEA through
`self`, `player`, `other`, `create` results, PUSH/POP and MOV copies (VERIFIED-DATA).
First-level indices used through `self`: 1, 4, 5, 8, 14, 17, 20, 23, 24, 25, 28, 32, 33,
34, 35, 37 (16 indices; the WP-16 report said 17 because it counted LEAs through DEFS
entry 0, which is not always `self`, and so picked up `camera`'s index 9); through `player`: 4, 5, 14, 34; through a `create` result: 14 (address
taken in 3 scripts, then passed on); `other` is never used for field access; no script
uses `getentity`.

| k | Offset | Type | Name | Meaning and evidence | self r/w | player r/w | Tag |
|---|---|---|---|---|---|---|---|
| 0 | 0x7B | pointer | self_ptr | back-pointer, what every reference points at (0x409ba0) | – | – | VERIFIED-CODE |
| 1 | 0x7F | float | age | seconds since creation, += frametime per update (0x405a9c) | 93/17 | – | VERIFIED-CODE |
| 2 | 0x83 | float | class | category; 2.0 = enemy (tested by `LockTarget`, `TraceLine`, `Lightning`, area damage, the touch pass), 4.0 = projectile | – | – | VERIFIED-CODE (names GUESS) |
| 3 | 0x87 | flags | flags | 0x1 terrain snap, 0x2 terrain align, 0x4 water snap, 0x10 hidden, 0x20 no view state, 0x100 not counted as enemy, 0x1000 precise collision | – | – | VERIFIED-CODE |
| 4 | 0x8B | float | dead | non-zero blocks damage; set to 1.0 after lethal damage (0x404ae0) | 29/4 | 3/0 | VERIFIED-CODE |
| 5–7 | 0x8F | vec3 | origin | world position; `move*` add to it; for children recomputed each update from the parent | 79/89 | 24/1 | VERIFIED-CODE |
| 8–10 | 0x9B | vec3 | attach_offset | child offset in parent axes (0x404850) | 18/20 | – | VERIFIED-CODE |
| 11–13 | 0xA7 | vec3 | prev_origin | engine copy of the previous origin | – | – | VERIFIED-CODE |
| 14–16 | 0xB3 | vec3 | angles | degrees; [2] = yaw; `rotate*` add to them | 20/50 | 1/1 | VERIFIED-CODE |
| 17–19 | 0xBF | vec3 | velocity | units per second; not integrated by the engine, scripts call `move(&self[17])`; `PushPlayer` and the two-player push-apart write it | 36/39 | – | VERIFIED-CODE |
| 20–22 | 0xCB | vec3 | field20 | no engine access found; scripts use it as a free vector (angular velocity) | 0/4 | – | GUESS |
| 23 | 0xD7 | float | wp_speed | speed along the waypoint path (`MoveToNextWP`, 0x405fe0) | 16/32 | – | VERIFIED-CODE |
| 24 | 0xDB | float | wp_turn_rate | yaw turn rate, degrees per second (`RotateTo*`, `RotateToNextWP`, 0x406110) | 10/77 | – | VERIFIED-CODE |
| 25 | 0xDF | float | wp_bank | bank angle factor: bank = turn × wp_bank / 4.8 (0x405fe0) | 0/7 | – | VERIFIED-CODE |
| 28–31 | 0xEB | vec4 | color | RGBA multiplier, initial 1.0, passed to the renderer | 0/34 | – | VERIFIED-CODE |
| 32 | 0xFB | float | scale | uniform model scale | 1/25 | – | VERIFIED-CODE |
| 33 | 0xFF | float | frame | converted to int and read by the renderer; animation frame | 0/8 | – | VERIFIED-CODE (copy), GUESS (meaning) |
| 34 | 0x103 | float | health | hit points, reduced by the damage routine | 79/3 | 4/2 | VERIFIED-CODE |
| 35 | 0x107 | float | damage | damage this object deals (passed to `Damage` in touch handlers; `Lightning` uses it) | 52/1 | – | VERIFIED-CODE |
| 36 | 0x10B | float | score | score for a kill | – | – | GUESS |
| 37 | 0x10F | float | wp_wait | wait time at the current waypoint, written by `MoveToNextWP` | 26/0 | – | VERIFIED-CODE |
| 38 | 0x113 | int | render_type | start of the embedded render data | – | – | VERIFIED-CODE |
| 44–52 | 0x12B | mat3 | axis | orientation matrix (`AnglesToAxis`) | – | – | VERIFIED-CODE |

Fields not listed are engine-internal and never touched by scripts.

**Camera structure** (value of `camera`, VERIFIED-CODE 0x40c260): [0..2] position,
[3..5] angles, [6] lateral velocity, [7] scroll speed (read by 54 scripts; recomputed
every frame as [9] × 42), [9] scroll factor (written by 5 scripts, read by 1). Scripts
reach [7] as `&camera[6]` followed by component 1 (LEA indices through `camera` in the
corpus: 6 and 9, VERIFIED-DATA).

## Header field 1

No code reads the script object's +0x44 after loading (every access seen after the load
is to +0x48..+0x8C). VERIFIED-CODE (scan of all loads of script pointers from the array at
0x1fa7e08). The value 16 is therefore irrelevant to execution; that it equals the number
of saved temporaries is either compiler metadata or coincidence (GUESS).

## Random numbers

The original `random` and `crandom` use the C runtime `rand()` (state × 214013 + 2531011,
result (state >> 16) & 0x7FFF) divided by 32767, i.e. [0, 1] inclusive, and
(u − 0.5) × 2 (VERIFIED-CODE, see the builtins table). The mock host deliberately uses a
different generator (below) that our C++ engine shares.

## Thread state (VERIFIED-CODE, thread creation 0x41c7d0, interpreter 0x419be0)

Each scripted entity owns exactly one thread (the entity keeps it at entity+0x57; the
dispatcher 0x41a200 takes it from there). A thread holds:

| Field | Offset | Content |
|---|---|---|
| script | +0x000 | index of the loaded script |
| DEFS table | +0x004 | resolved global records, indexed by −operand−1 |
| FUNC table | +0x008 | resolved builtin records |
| pc | +0x00C | current instruction |
| stack depth | +0x010 | 0..512 |
| stack | +0x014 | 512 × 4 bytes |
| STRG | +0x814 | base of the string literals |
| frame | +0x818 | `header[6]` slots of 4 bytes |
| code | +0x81C | base of CODE |
| timeout | +0x820 | float, the latent-call timeout (seconds) |

Creation: the frame is allocated and zeroed, then the DATA entries are applied in file
order (kind 2: slot = value bits; kind 3: slot = address of slot `value`), the pc is set
to the `main` entry, the stack is empty and the timeout 0. Nothing is executed at
creation. VERIFIED-CODE 0x41c7f0..0x41c8d6.

Everything else lives in engine globals shared by all threads: current thread
(0x1fa7de0), `self` (0x1fa7df0), argument pointer (0x1fa7df4, always the current thread's
frame), return register (0x1fa7dfc, one float), latent-done flag (0x1fa7e00).

The stack is never cleared after creation: not by the dispatcher, not at END or RET.
VERIFIED-CODE: no writer of thread +0x10 other than PUSH, POP and the zeroing at creation
was found. It does not matter for the shipped scripts: a static scan finds stack depth 0
at all 624 LCALL sites, and in the mock run every handler and every main pass leaves the
depth unchanged (VERIFIED-DATA).

**Lifetime.** The thread is created with its entity by the object builder 0x409ba0 (for
every object, attached children included) and stored at entity + 0x57; the map spawner
replaces it with a fresh thread when the map entry names a script. `remove` (0x404410)
only marks the entity (and its children) removed; a removed entity is not updated or
touched again, and it is freed together with its thread and frame at the start of the next
entity pass (0x404590). VERIFIED-CODE.

## Interpreter invocation (VERIFIED-CODE 0x419be0)

One invocation executes instructions from the thread's pc until one of:

| Stop | pc afterwards | Returns |
|---|---|---|
| END (0x00) | on the END | 0 |
| RET (0x18) | on the RET; return register = operand A | 1 |
| LCALL (0x1D) completes | after the LCALL | 0 |
| LCALL (0x1D) still waiting | on the LCALL | 0 |

**Instruction budget.** A counter starts at 10,000 (0x2710) for every invocation (nested
invocations get their own counter). It is decremented before each instruction; when it
reaches 0 after an instruction that did not stop the invocation, the engine calls its
fatal-error routine 0x4204f0 with "Script stall detected." and the game terminates. So an
invocation can execute at most 10,000 instructions; the 10,000th must be one of the
stops above. No shipped script comes near it: the longest single invocation in the mock
run executes 101 instructions (VERIFIED-DATA, mock run).

**Stack.** 512 entries; pushing a 513th entry or popping from an empty stack logs
"Script '%s': Thread stack overflow." / "...underflow." and exits the process
(VERIFIED-CODE 0x419f70, 0x41a098, exit through 0x419b90).

## CALL of a script subroutine (VERIFIED-CODE 0x41a04c..0x41a078)

`CALL B = A` with A ≥ 0 sets the pc to instruction A and runs a nested invocation on the
same thread. There is no new frame: caller and callee share all slots, temporaries
included, and the stack. Arguments are passed in whatever slots the compiler chose (the
builtin convention t0, t1, ... is used for builtins only); the callee's result travels
in the return register: RET A copies A into it, and the caller then copies the return
register into B. When the callee ends with END the return register is not written, so B
receives whatever the register held (stale value). After the nested invocation returns,
for any reason, the caller continues at the instruction after the CALL.

Temporaries are not saved by the VM. The compiler saves the live ones with PUSH before a
call and restores them with POP after it; because of the POP quirk (see below) only t0 is
actually restored.

**Nested latent case.** If the callee stops on a waiting LCALL, the nested invocation
returns 0 and the caller simply continues after its CALL: the rest of the callee is
abandoned and will not be resumed. VERIFIED-CODE (the caller restores its own pc from a
local). This never happens in the shipped scripts: no subroutine reachable through CALL or
LCALL contains an LCALL (VERIFIED-DATA, static reachability from all 387 CALL-to-script
sites). Subroutines calling further subroutines do occur and behave as expected.

## LCALL and TMO (VERIFIED-CODE 0x41a0b9..0x41a178, 0x41a07d)

`TMO A` stores A (seconds, float) in the thread's timeout. `LCALL B = A`:

1. Clear the done flag. Run the callee once:
   - builtin: the builtin may write the done flag (only `sleep` and the
     RotateTo/RotateToClamp/MoveToNextWP/RotateToNextWP implementations do, see the builtin
     table);
   - script subroutine at A: nested invocation from A; its return status (1 for RET,
     0 for END) becomes the done flag. The pc of the LCALL is restored afterwards.
2. If the done flag is non-zero, the call is **complete**.
3. Otherwise, if the timeout is exactly 0.0, the call is **waiting**.
4. Otherwise `timeout = timeout − frametime` (rounded to float); if the result is ≤ 0
   the call is complete, else waiting (our VM and the reference test `not (t > 0)`, so a NaN timeout completes at once; what the original does with NaN is not established, see issues/001; no shipped script produces one).
5. Complete: pc advances past the LCALL, timeout = 0, B = return register, the invocation
   returns 0. Waiting: pc stays on the LCALL, the invocation returns 0.

So the timeout is in seconds of game time (it is decremented by the global `frametime`
once per execution of the waiting LCALL, i.e. once per update of the thread), and the
callee is executed again on every update while the call waits. Completing an LCALL also
ends the invocation: code after an LCALL always runs on a later update.

`sleep` sets the done flag to 0, so `TMO t; LCALL sleep` waits t seconds; `LCALL sleep`
without a timeout waits forever. A timeout left over from an abandoned LCALL (see event
handlers) applies to the next LCALL of the thread.

**Main thread versus handlers.** For `main` a waiting LCALL simply resumes on the next
update. For event handlers see "Event dispatch": the handler's pc is discarded, so a
waiting LCALL in a handler ends the handler for good (after running the callee once),
while its timeout stays in the thread.

## Values and arithmetic

All slots hold 32 bits. Floats are IEEE single. Every copy made through the FPU (MOV,
PUSH, POP, RET, immediates, the return register, NEG) turns a signalling NaN into a quiet
one (bit 22 set) and preserves every other pattern, denormals included. LEA and the
pointer copies inside the dispatcher are integer moves and copy exactly.

- MUL, DIV, ADD, SUB: correctly rounded single-precision results (the x87 computes in
  higher precision and rounds once on the store; for single operands this equals IEEE
  single arithmetic). Division by zero gives ±infinity, 0/0 and ∞−∞ give the x87 default
  NaN 0xFFC00000; a NaN operand propagates quieted.
- Integer conversion: truncation toward zero; NaN, infinities and values outside the
  int32 range give −2147483648 (0x80000000). VERIFIED-CODE 0x440870.
- Integer results (BAND, BOR, logic, EQ, NE, NOT, comparisons) are converted back to float.
- GT, LT, GE, LE are false when either operand is NaN; JZ jumps only on ±0.0; JNZ jumps on
  any other value including NaN.

## Quirks an implementation must reproduce

1. POP writes operand B (always slot t0 in the shipped code), not the slot the compiler
   meant (A and C). Only the last POP of each restore sequence, which restores t0, has a
   visible effect. (rcsl-opcodes-v0.md)
2. Every completed LCALL ends the current invocation; code after it runs on a later
   update.
3. A waiting LCALL inside an event handler ends the handler permanently. Only one
   shipped script is affected: `boss1\boss1_rl.scr`, whose `init` handler waits 1.5 s
   with `sleep` and then deactivates two attachments; in the original the deactivation
   never happens and the thread keeps a timeout of 1.5 s − frametime. VERIFIED-DATA.
4. The timeout is per thread and shared by main and handlers; a handler's TMO or completed
   LCALL changes the timeout main is waiting on.
5. CALL and LCALL store the return register into B even when the callee did not write it
   (stale value).
6. END does not advance the pc; main restarts from its entry on the next update only
   because the dispatcher resets the pc when it finds END or RET.
7. Operands a handler does not use are still decoded, including indirections (a read).
8. DEFS entries whose name is unknown to the engine resolve to NULL (no error); none occur.
9. `create` puts the new entity in the return register and then runs the new entity's
   `init` and one update (with its `main`) synchronously; a RET or a value-returning
   builtin in those replaces the value the caller receives.
10. `other`, `cb_msg`, `cb_parm1`, `cb_parm2` are never cleared: scripts see stale values
    outside the handlers that set them (`other` is restored after each touch dispatch).
11. The damage handler runs after the health subtraction and before the dead flag
    (field 4) is set, also for the lethal hit.
12. A touch by a player changes the touched entity's player index (+0x77), and with it
    what `player` and `p_*` refer to for that entity from then on.
13. The main dispatcher decides whether to restart `main` by looking at the opcode under
    the pc after the invocation (END or RET), not at the invocation's return status.

Behaviours that do not matter for the shipped data: opcodes 0x06, 0x15, 0x16, 0x17, 0x19
(never used); opcodes above 0x1E (treated as no-ops, never present); the no-op argument
test before builtin CALLs (0x41a00b); signalling-NaN quieting (never triggered in the mock
run); the nested latent case (unreachable); the stack never being reset (always balanced);
stack overflow and the stall detector (never near their limits); header field 1 (never
read); the exact NaN bit patterns of invalid arithmetic; the second global address for
player index 1 in a one-player game.

## Trace format (reference VM)

The reference VM writes one line per event, ASCII, `\n`-terminated, fields separated by
one space, integers in decimal, 32-bit values as 8 lower-case hex digits of their bits:

| Line | Meaning |
|---|---|
| `E <frame> <entry>` | a dispatch starts (`entry` is init, main, damage, touch, callback) |
| `I <frame> <entry> <depth> <pc> <op> <value>` | one executed instruction; `op` two hex digits; `depth` 0 for the handler, +1 per nested subroutine; `value` is the value written (see below) or `-` |
| `B <frame> <entry> <pc> <name> <arg>... -> <ret>` | a builtin call: the documented number of argument slots t0.. as read before the call, and the return register after it |
| `R <frame> <entry> <status> <stack delta>` | the dispatch ended; status 0/1 of the invocation; stack depth change |
| `D <frame> <amount>` / `D <frame> skipped` | the mock damage routine ran on self (amount as float bits) or was skipped because field 4 ≠ 0 |
| `X <frame> <entry> <pc> <message>` | the run stopped on a VM error |

`value`: C for arithmetic, logic, comparison, NOT, NEG, LEA; A for MOV; the pushed value
for PUSH; the popped value for POP; the returned value for RET; B for CALL; B for a
completed LCALL (`-` while waiting); the timeout for TMO; `-` for END, jumps and no-ops.
For a CALL the `B` line and any nested `I` lines come before the CALL's own `I` line.

## Mock host (reference VM)

The mock host makes runs deterministic and reproducible by a C++ implementation:

- Address space (all values are what appears in slots and in the trace):
  frame slot i = 0x10000000 + 4i; engine global g (index in the table at 0x457220,
  see "Globals") = 0x20000000 + 16g; mock entity n: reference value 0x30000000 + 0x1000n,
  field k at reference + 4k, fields 0..89 mapped (the size of a real entity), all
  initially 0 except field 0, which holds reference − 0x7B like the real back-pointer.
  Access to any other address, or to an unmapped address through an operand the handler
  uses, is an error; indirections of unused operands read 0 from unmapped memory.
- Entities 0..3 exist from the start: 0 = self, 1 = player, 2 = camera, 3 = other.
  Globals: `self`, `player`, `camera` hold the references of entities 0, 1, 2;
  `frametime` = dt; `time` = accumulated dt at the start of the frame (float sum);
  everything else 0.
- Frame order in the mock: `init` (frame 0 only, like the spawner, before the entity
  pass), then the `main` update, then that frame's scheduled events in the order touch,
  damage, callback (the engine's touch pass and projectile pass come after the entity
  pass). Standard schedule: at frame 120 `touch` with `other` = entity 3 (restored to its
  previous value afterwards); at frame 240 `damage` of 10.0 through the damage routine
  (skipped if field 4 ≠ 0; field 34 −= amount; handler; field 4 = 1.0 if field 34 ≤ 0); at
  frame 360 `callback` with cb_msg = 1.0, cb_parm1 = cb_parm2 = 0.0 (not restored). A
  handler whose entry point is absent is not run. dt = 1/60 rounded to float
  (0x3c888889).
- Builtins: each reads its documented argument slots t0..t(n−1) (from
  testdata/golden/rcsl_builtins.json); vector arguments are dereferenced (3 floats must be
  readable), string arguments must be STRG string starts. Return register: `random` =
  u, `crandom` = 2u − 1 (computed in float), where u comes from xorshift32 (state seeded
  with 1; `x ^= x << 13; x ^= x >> 17; x ^= x << 5`, 32-bit) as
  u = (next >> 8) / 16777216.0, the same generator as `as3d::Rng`; one generator per run;
  builtins returning an entity get a fresh mock entity (next free n); other builtins
  that return a value return 0.0; builtins that return nothing leave the register alone.
  Under LCALL a mock builtin is done immediately when the timeout is 0, otherwise it is
  never done by itself and the timeout ends the wait.
- The mock does not simulate entity lifetime or other scripts: `remove`, `deactivate`,
  `create` etc. are stubs, self keeps running after removing itself, and mock entities
  other than self have no script (so `callback`, `Damage` and `create` run no handler).
  `time` is the float sum of dt at the start of the frame.
- Checks made by `test_rcsl_vm.py` on top of VM errors: each dispatch leaves the stack
  depth unchanged; for every builtin call, every argument slot t0..t(n−1) was written
  since the previous call in the same invocation (a waiting LCALL is checked on its first
  execution only). This is a weak check (a slot written for another purpose counts); the
  kind check per argument is the static cross-check in rcsl-builtins-table.md.

## Changelog

- 1.0 (WP-21/22): first version.
- 1.1: NaN timeout wording corrected (issue 001).
