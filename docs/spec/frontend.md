# Front end: menus, HUD, game flow, save data

Spec version 1.0 (WP-26). Source: `AirStrike3D.exe` v1.70 (image base 0x400000), read through
the Ghidra export in `re/out/v170/` and, where the export has no function, a plain disassembly
of the executable. Function names are in `re/symbols_v170.csv`. Shipped data
(`assets_extracted/`, `Settings.xml`, `config.ini`) and the original HTML manual were used as
a cross-check and feature list.

This document details what `engine-behaviour.md` §1.3, §10 and §11 summarise. Where the two
differ, this document wins; the differences are listed in §9.

Tags: `VERIFIED-CODE` (address), `VERIFIED-DATA` (checked on shipped files),
`INFERRED-SEQUEL`, `GUESS`. "ftol" is truncation toward zero.

Conventions used throughout:

- **Coordinates** are virtual 2D pixels of the 800×600 screen, origin top-left, y down
  (render-pipeline.md §8.1). A rectangle is written (x, y, w, h).
- **UVs** are written (s0, t0, s1, t1) in the engine's 2D convention of render-pipeline.md
  §8.2: the top-left corner of the quad samples (s0, t1), the bottom-left (s0, t0). With
  bottom-up TGA storage, t = 1 is the top row of the image as seen in an image viewer.
- **Colours** are given as RGBA 0..1 or as the packed value the code uses, 0xAABBGGRR
  (render-pipeline.md §8.2). "Orange" below always means 0xFF00A0FF = (1, 0.627, 0, 1);
  "rust" means 0xFF0030C0 = (0.75, 0.19, 0, 1).
- **Blend** modes: 0 opaque (no blending), 1 ALPHA, 2 ADD, 3 FILTER (destination × source
  colour) (render-pipeline.md §8.2, engine-behaviour.md §13.2).
- **Menu time** `mt` is a clock that restarts at 0 whenever a menu is pushed or popped and
  advances by frametime while any menu is shown (0x1fd93b4; 0x4292f0, 0x429360, 0x4299f0).
  Most screen animations are driven by it. VERIFIED-CODE.
- **Pulse(f, φ)** means the grey level 255 × (0.5 + 0.5 sin(f·π·mt − φ)), applied as an
  RGB colour (R = G = B) with alpha 1.

---

## 1. Screens and state machine

### 1.1 Principle

There is no separate "menu mode". A level is always loaded: either an attract level
(`intro1`..`intro4`, an intermission level with a fixed swaying camera and no players) or a
mission. Menus are a stack (depth 16) drawn over that level; a menu shown during a mission
comes with the paused flag, so the mission is frozen but still rendered behind it.
VERIFIED-CODE (engine-behaviour.md §1.3; `UI_PushMenu` 0x4292f0, `UI_Frame` 0x4299f0).

The front end is driven by these flags (engine-behaviour.md §1.3): intro running, paused,
HUD-hidden, game over, intermission level, two players.

### 1.2 Screen list

| # | Screen | Built by | Shown over | Music |
|---|---|---|---|---|
| S0 | Intro logo pages | 0x408070, 0x408260 | nothing (2D only) | none started by the intro (GUESS; the intro only stops BASS at its end, 0x408260) |
| S1 | Main menu | 0x429f60 | attract level | attract level's `music` (all four: `music\track05.mo3`, VERIFIED-DATA) |
| S2 | Exit confirmation | 0x426c90 | attract level | same |
| S3 | Start Game (mission, difficulty, players, helicopters) | 0x42b710 | attract level | same |
| S4 | Top Scores | 0x42c660 | attract level (or wherever the high-score check ran) | same |
| S5 | Name entry | 0x42a580 | attract level | same |
| S6 | Options | 0x42aea0 | attract level, or the paused mission | unchanged |
| S7 | Configure controls | 0x423520 | as S6 | unchanged |
| S8 | Information (10 pages) | 0x428c00 | attract level | same |
| S9 | Loading screen | 0x404230 | replaces everything while a level loads | none |
| S10 | Playing (HUD) | 0x4041b0 | mission | mission's `music`, looped |
| S11 | Pause (P / Pause key) | none: no menu, no text | mission, frozen | keeps playing (GUESS: no pause call on the music was found) |
| S12 | Tutorial hint box | 0x42c000 | mission, frozen | keeps playing |
| S13 | In-game menu (Esc) | 0x428fa0 | mission, frozen, HUD hidden | keeps playing |
| S14 | Game over | 0x4278a0 | mission, frozen, HUD hidden | the current module jumps to pattern order 35 (0x408c80: same module, order 0x23) |
| S15 | Mission complete | 0x4267a0 | mission, frozen, HUD hidden | keeps playing |
| S16 | Game complete (after mission 20) | 0x4274f0 | mission 20, frozen, HUD hidden | keeps playing |

There is no mission briefing, no shop or upgrade screen, no separate difficulty screen, no
helicopter-selection screen other than the grid inside Start Game and Mission Complete, no
separate credits screen (credits are page 10 of Information, and the `givemecredit` cheat),
and no save/load slot screen. VERIFIED-CODE (every menu builder is listed above; the menu
builders were enumerated through `UI_AddItem` 0x429490's callers).

### 1.3 Transitions

```
boot ──(ShowLogo=1)──> S0 intro pages ──(all pages done)──> attract level + S1
boot ──(ShowLogo=0)──────────────────────────────────────> attract level + S1

S1 Main menu:  Start Game → S3 | Top Scores → S4 | Options → S6 | Information → S8
               Exit → S2 | Esc / right click: ignored
S2:  Yes → save game.bin, quit program | No / Esc → S1
S3:  Back / Esc → S1 | Start → S9 → S10 (new campaign at the chosen mission)
S4, S8:  Back / Esc → previous menu
S6:  → S7 (configure keys); Back/OK → previous menu (details §3.6)

S10 Playing:  Esc → S13 | P/Pause ↔ S11 | ShowTutorialHint → S12 → S10
              all lives lost → S14 | EndLevel: mission 1..19 → S15, mission 20 → S16
S13 In-game:  Resume / Esc / right click → S10 | Options → S6 (pushed over S13)
              Quit → S9 (attract level) → S1          (no banking, no high-score check)
S14 Game over (buttons appear after 2 s):
              Restart → S9 → S10 same mission, lives as at its start
              Quit → bank → S9 (attract) → S1, then high-score check → S5 → S4
S15 Mission complete:
              Continue → bank → S9 → S10 next mission
              Restart → S9 → S10 same mission (no banking)
              Quit → S9 (attract) → S1 (no banking, no high-score check)
S16 Game complete:
              Continue → bank → S9 (attract) → S1, then high-score check → S5 → S4
S5 Name entry: OK / Enter → insert entry → S4 (over S1)
```

All VERIFIED-CODE: 0x429b20, 0x426a20, 0x42b3b0, 0x428d40, 0x4275e0, 0x426270, 0x426df0,
0x42a300, 0x40bde0, 0x40be90, 0x408f10.

Returning to the main menu always rebuilds it and replaces the whole menu stack
(`M_ShowMainMenu` 0x42a260). Loading the attract level after Quit reuses the attract level
chosen at boot (0x1fdb35c), it is not re-drawn at random. Loading any level runs the loading
screen (S9) and restarts that level's music.

"Esc" below always means the Esc key (0x1B) and the right mouse button (code 201, 0xC9);
the generic menu handler treats both as "back" (pop the current menu) unless the screen's own
key handler swallows them (VERIFIED-CODE 0x429630).

### 1.4 What the world does behind each screen

- S0: no level is loaded yet; the intro frame draws only 2D (0x408260). The attract level is
  loaded (with its loading screen) when the last page ends.
- S1–S8 at boot or after quitting: the attract level runs normally (not paused): its scripts
  and particles animate, the camera sways (engine-behaviour.md §9.6), no players, no HUD.
  The cheat buffer is not fed on intermission levels.
- S6/S7 opened from the in-game menu, S11–S16: the mission is paused (time frozen, see
  engine-behaviour.md §1.2) and rendered every frame; for S13–S16 the HUD is hidden.
- While any menu is on the stack, every key and mouse button goes to the menu system first
  and gameplay never sees it (0x408f10 returns after `UI_KeyEvent` 0x429190 accepts it). The
  hotkeys F5–F9, P and Esc are therefore inactive while a menu is up. VERIFIED-CODE.

## 2. Menu system (widgets and input)

<!-- SECTION-2 -->

## 3. Screens in detail

<!-- SECTION-3 -->

## 4. HUD

<!-- SECTION-4 -->

## 5. Mission flow and progression

### 5.1 What a campaign is

A campaign starts from the Start Game menu at any unlocked mission (§3.4). "New campaign"
(0x408c40) clears, for both player records, the banked score (+0x165) and the rank
accumulator (+0x169), and sets lives-at-level-start (+0x94) to 2, then runs the level start
`G_BeginLevel` (0x408b30). VERIFIED-CODE (engine-behaviour.md §1.3).

### 5.2 Level start: what is reset (VERIFIED-CODE 0x408b30)

At every level start (new campaign, Continue, Restart), for each of the `player count`
records:

| Field | Value at level start | Carries over between missions? |
|---|---|---|
| `p_scores` (+0x90) | 0 | no; the previous missions' score lives in the banked score |
| `p_lives` (+0x98) | lives-at-level-start (+0x94) | yes, through banking (§5.4) |
| `p_stars` (+0x9c) | 0 | no; folded into the rank accumulator |
| kills this level (+0x16d) | 0 | no |
| `p_counter3` (+0xa8) | 1.0 | no |
| `p_action` (+0x8c) | 0 (player 1 and player 2 always) | no |
| weapon upgrades (+0x115, 20 bytes) | all 0, then machine gun (slot 0) = 1 | **no** |
| `p_weapon` (+0xb0) | 0 (machine gun) | **no** |
| missiles and power-ups | cleared by `G_SpawnPlayer` (0x40b2f0) when the helicopter spawns | **no** |
| banked score (+0x165), rank accumulator (+0x169) | untouched | yes |
| helicopter index (+0x88) | untouched | yes (chosen in Start Game) |

Also cleared: pause, HUD-hidden, game-over flags; the level's enemy total (0x4d52f8) and
maximum score (0x4d52fc). The difficulty factors are re-applied from the stored difficulty
index (clamped to 4). VERIFIED-CODE 0x408b30.

Consequence (VERIFIED-CODE): every mission starts with the machine gun at level 1, no
missiles and no power-ups. Only lives, score (banked) and rank progress carry over. The
player scripts do not save anything across levels (they cannot: the script globals are
per-level). The manual's "you will be given upgrades" refers to pickups inside a mission.

### 5.3 End of mission (`EndLevel`, VERIFIED-CODE 0x407570 → 0x4269b0)

`EndLevel()` sets HUD-hidden and paused, then:

1. If the current level record's `enableHelic` (record +0x110) is in 0..9, the helicopter
   table entry is unlocked (0x457660 + 33·n, byte 0 = 1).
2. The mission table entry (current mission index + 1) mod 20 is unlocked (0x457838).
3. If the current mission index is 19 (the last), the Game Complete screen is built and
   pushed (§3.9); otherwise the Mission Complete screen (§3.8).

Nothing is banked at this point; banking happens when the player presses Continue. The unlock
flags change in memory immediately and are saved with the rest of game.bin at program exit
(§6.1). There is no separate end-of-level score bonus: the mission-complete tally only
displays ratios (VERIFIED-CODE 0x426360; GUESS that no other code adds score at the end).

Scripts reach `EndLevel` through `eol.scr` (autopilot fly-out, then EndLevel) and the three
boss scripts (rcsl-builtins-semantics.md). The cheat `messwiththebest` also calls it.

### 5.4 Banking (VERIFIED-CODE `G_BankScore` 0x40bd10)

For each of the `player count` records:

- banked score = ftol(banked score + p_scores);
- rank accumulator += p_stars / star total + 0.5 × p_scores / maximum level score;
- lives-at-level-start = p_lives.

Banking is called by: Mission Complete → Continue; Game Complete → Continue; Game Over →
Quit. It is **not** called by Mission Complete → Quit or Restart, Game Over → Restart, or the
in-game menu's Quit. So quitting from the mission-complete screen throws the mission's score
away (the unlocks are kept). VERIFIED-CODE 0x426270, 0x4275e0, 0x426df0, 0x428d40.

A level with no stars (star total 0) or no scoring objects (maximum 0) makes these ratios
infinite or NaN in the original. Guard both (treat a zero denominator as a ratio of 0).

### 5.5 Score, maximum score, stars, kills

- `p_scores` grows through `G_AwardScore` (enemy kills, score × difficulty score factor,
  engine-behaviour.md §6.4) and through the point items `i_pts5k` / `i_pts10k`, which add
  5000 / 10000 directly (not scaled by difficulty) (VERIFIED-DATA). Cap 10⁹.
- The maximum level score (0x4d52fc) and the enemy total (0x4d52f8) start at 0 at level
  start and grow while the level runs: each map object instantiated as the scroll advances
  adds its (difficulty-scaled) score value to the maximum and, when it is an enemy (class 2)
  without the non-target flag, 1 to the enemy total (VERIFIED-CODE 0x407590 at 0x407803..
  0x407855); `create` from a script also counts class-2 entities into the enemy total
  (VERIFIED-CODE 0x41a95e). So both are "what the player has had a chance to kill so far",
  which at mission end is the whole level. The star total (0x4d52d0) is counted once at map
  load from the `item_star` drops (VERIFIED-CODE 0x409493, 0x4095c7; engine-behaviour.md
  §10.2).
- `p_stars` += 1 per star picked up (`items\star.scr`, VERIFIED-DATA).
- Kills (+0x16d) count enemies killed by that player (engine-behaviour.md §6.1).

The HUD score is ftol(p_scores) + banked score (engine-behaviour.md §10.4).

### 5.6 Lives, extra lives, continues

- A campaign starts with lives-at-level-start = 2: the current helicopter plus two spares.
- On death the player script decrements `p_lives` and calls `RespawnPlayer`; the game is
  over when `p_lives` < 0 (one player), or when both players have `p_lives` < 0 (two
  players) (VERIFIED-CODE 0x40b440). In two-player mode, while player 1 still has lives ≥ 0
  the game goes on even if player 2 is out, and vice versa.
- The only source of extra lives is the 1-Up item (`items\i_life.scr`: `p_lives` += 1,
  VERIFIED-DATA). There is no score-based extra life and no upper limit on lives (the HUD
  draws at most 5 icons). The cheat `endisnear` sets 99.
- There are no continues in the arcade sense. Game Over offers Restart, which replays the
  same mission with the lives the mission began with, the same banked score and rank
  accumulator (the mission's own progress is discarded), as often as the player wants.
  VERIFIED-CODE 0x4275e0 → 0x408b30.

### 5.7 Unlocking

- Missions: the Start Game list shows only unlocked missions (§3.4). Missions 1 and 2 are
  unlocked in a fresh save. Completing mission i unlocks mission i + 1 (mission 20 unlocks
  mission 1, which is always unlocked).
- Helicopters: entries 0 (p_apache) and 1 (p_comanche) are unlocked in a fresh save. The
  others unlock on completing the mission whose `enableHelic` names them (VERIFIED-DATA,
  levels.txt): mission 3 → 7 (p_comanche_sand), 5 → 4 (p_comanche_white), 7 → 2
  (p_apache_white), 9 → 3 (p_apache_impala), 11 → 9 (p_comanche_green), 13 → 8
  (p_comanche_blue), 15 → 6 (p_apache_blue), 17 → 5 (p_comanche_lava).
- Unlocks are global to the save, not per player, and are never re-locked.

### 5.8 The ten helicopters

VERIFIED-DATA (`objects\player.obj`, `scripts\player\*.scr`): the helicopters differ only in
looks. All ten use player logic that is instruction-for-instruction identical; the three
scripts `player.scr`, `player_a.scr` and `player_c.scr` differ only in the model paths they
switch to when damaged. Health 400, speed, weapons and item handling are the same.

| Index | Object | Model | Skin | Script |
|---|---|---|---|---|
| 0 | p_apache | apache | apache_green | player_a |
| 1 | p_comanche | comanche_impala | comanche_impala | player |
| 2 | p_apache_white | apache | apache_white | player_a |
| 3 | p_apache_impala | apache | apache_impala | player_a |
| 4 | p_comanche_white | comanche | comanche_white | player_c |
| 5 | p_comanche_lava | comanche | comanche_lava | player_c |
| 6 | p_apache_blue | apache | apache_blue | player_a |
| 7 | p_comanche_sand | comanche | comanche_sand | player_c |
| 8 | p_comanche_blue | comanche | comanche_blue | player_c |
| 9 | p_comanche_green | comanche | comanche_green | player_c |

At boot (and after a video restart, §6.3) player 1's helicopter is 1 and player 2's is 0
(VERIFIED-CODE 0x408cc0); the choice is not saved.

### 5.9 Two-player rules (summary)

- Chosen in Start Game ("Game mode", §3.4). Each player has their own record, lives, score,
  bank, rank accumulator, bindings (`[Controls]` / `[Controls2]`) and helicopter.
- Game over only when both players are out (§5.6). Restart restarts both.
- Mission Complete and Game Complete do not draw the statistics box at all in two-player
  mode (VERIFIED-CODE 0x426360, 0x4270a0: the tally is inside a one-player test).
- Banking is done for both records.
- The high-score check is skipped in two-player mode (VERIFIED-CODE 0x40bde0), so two-player
  scores are never recorded.
- The two-player HUD is described in §4.

### 5.10 Difficulty

Chosen per campaign in Start Game (index 0..4: Very Easy, Easy, Normal, Hard, Nightmare;
default Normal). Factors (table 0x4577d0, 16 bytes per level) are in engine-behaviour.md §6.3.
The rank factor used for the mission-complete rank and for the high-score rank is the one of
the current difficulty; a campaign cannot change difficulty midway (Continue keeps it).
VERIFIED-CODE 0x408b30, 0x426360, 0x40be90.

### 5.11 Rank

Rank index from a value v (0x40bda0): 0 "Cheater" if any cheat was used this session (the
flag is never cleared); otherwise the first i in 1..5 with v < threshold[i] (3, 7.5, 14, 22,
30, table 0x457d48), else 6. Names (table 0x45650c): Cheater, Rookie, Junior Pilot, Pilot,
Master Pilot, Berserker, Elite. VERIFIED-CODE.

- Mission Complete / Game Complete display v = (p_stars / star total + 0.5 × p_scores /
  maximum score + accumulator) × rank factor, i.e. the accumulator including the mission just
  finished but before it is banked (player 1 only).
- The high-score entry stores the rank of v = accumulator × rank factor, taken after banking
  (0x40be90).

The accumulator grows by at most 1.5 per mission (all stars, maximum score), so over 20
missions the value can reach 30 × rank factor; "Elite" (≥ 30) needs near-perfect play on
Normal or above. (Arithmetic from the formulas above.)

## 6. Save file and settings

<!-- SECTION-6 -->

## 7. Touch adaptation notes

<!-- SECTION-7 -->

## 8. Open questions

<!-- SECTION-8 -->

## 9. Corrections to other specs

<!-- SECTION-9 -->

## Changelog

- 1.0 (WP-26): first version.
