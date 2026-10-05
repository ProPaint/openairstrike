# 165: a stuck explosion and a player that never comes back (report of 2026-10-04)

Reported from phone play, in all three games: an explosion stays on screen (its particles
keep coming at one spot, debris hangs in the air), and when the player dies at the same
moment the helicopter never reappears, while the rest of the level goes on. Two
screenshots: Gulf Thunder (player dead, no helicopter, lives still shown), AirStrike 3D
(the player's death explosion, `p_expl_wave` rings and `helic_dead` debris, frozen).

## What was looked for

Both symptoms are what an entity whose script no longer runs looks like: a continuous
emitter holder keeps emitting where it stopped (render-pipeline.md 6.3 stops an emitter only
when its holder is removed or deactivated), and a dead helicopter whose `main` never reaches
`RespawnPlayer` is dead for good (engine-behaviour.md 7.4; the sequels' scripts do the same
after their fall loop). The engine freezes an entity for good in exactly one place:
`World::dispatch` sets `scriptFaulted` on any VM error (stall guard, stack, unmapped
address, a builtin's failure, dispatch nesting) and never runs that thread again, where the
original terminates the game.

Not reproduced headless (`as3d_sim`, this change's `--upgrades`, `--items`, `--powerup`,
`--missile`, `--pause-every` options and the random input scripts under tools): every level
of the three games, difficulties 2 to 4, two to three seeds each, with the pilot, with
random inputs, with all helicopters, with every power-up and missile type, with pauses, at
the four screen edges, about 600 runs and 400 deaths: no script error, no stall, every
death respawned within 4 s, nesting at most 5 of the 48 allowed, at most 264 of the 1024
list entities, no lingering temporary entity but items and bosses. So the trigger is
something the simulator does not do, and it is not known yet.

## What this change does (ours, not in the original)

1. **A faulted script no longer freezes its entity.** `World::onScriptFault`: a root pool
   entity is removed (effects finish, emitters stop), a child or definition entity is
   deactivated (emitters stop). A player's helicopter is kept for the watchdog below.
   `WorldStats::faultedRemoved` counts them.
2. **Respawn watchdog** (`World::respawnWatchdog`, called from `playerFrame`): a player
   whose entity is missing, dead or faulted for `kRespawnWatchdogSeconds` (12 s, a normal
   death takes 3 to 4 s) while the level runs gets what its script would have done: one life
   less, then the native respawn; with no lives left the game-over test fires.
   `WorldStats::forcedRespawns` counts them; `as3d_sim --fault-player F` exercises it.
3. **Diagnostics.** The FPS counter (Options, or `--fps`) gets a red third line when the
   world has seen a script error, a forced respawn or a stopped entity, with the first
   error's script and message; `as3d_sim` prints the deepest handler nesting, the forced
   respawns and the stopped entities. Every event is also logged (`AS3D_WARN`: logcat on
   Android, stderr on the desktop, the browser console on the web).

The watchdog never fired in the sweeps above (checked including Gulf Thunder's bonus mission
11, where the helicopter sits dead below the ground by design and the level ends first).

## Still open

The root cause. The next occurrence should be read off the FPS counter's red line (or the
log): a script name and a VM message point at a scripting divergence; "respawn N" with no
script error means the helicopter's script was waiting for something that never came (a
latent call that never completes) or its entity was removed by something else, and the
state dump (`World::dumpState`) of that moment is the next step.
