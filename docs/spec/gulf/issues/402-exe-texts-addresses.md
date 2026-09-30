# 402: `tools/exe_texts/gulf.json`: entries that do not point at their text

Status: open (a correction of `tools/exe_texts/gulf.json` for the spec owner). Raised by F2.
Spec: [../frontend.delta.md](../frontend.delta.md) 7; `re/tools/gen_exe_texts_gulf.py`.

## What was found

`tools/extract_exe_texts.py --game gulf` reads the 230 entries of `tools/exe_texts/gulf.json`
from the executable (SHA-256 `86195a96…ae077`). Two kinds of fault, found by comparing each
string with the AirStrike 2 text of the same key (`tools/exe_texts/as2.json` on AirStrike 2's
executable) and by checking that each address is the first byte of a string:

1. **Not the start of a string** (11 entries; the byte before the address is not NUL). The tool
   leaves them out with a warning, so the game uses its built-in text:

| Key | Address in gulf.json | What is there | The text nearby |
|---|---|---|---|
| `title.enter_name` | 0x0048cbf4 | "r Your Name" | "Enter Your Name" starts 4 bytes earlier |
| `title.hint` | 0x0048b918 | the end of "   Next   " | – |
| `difficulty.3` | 0x0048cd30 | the end of "Nightmare" | "Hard" follows |
| `difficulty.4` | 0x0048cd24 | the end of "Player" | "Nightmare" follows |
| `mode.0` | 0x0048cd14 | "ive" | "Single Player" follows |
| `mode.1` | 0x0048cd08 | the end of "   Apply   " | "Cooperative" follows |
| `button.ok` | 0x0048cc04 | "k   " | "   Ok   " starts 4 bytes earlier |
| `heli.2` | 0x0048bd98 | "mous Thorn" | "Venomous Thorn" starts 4 bytes earlier |
| `heli.3` | 0x0048bd88 | "mer" | "Lava Hammer" |
| `credits.4` | 0x0048b734 | "arov}" | "{Dmitry Zakharov}" |
| `credits.19` | 0x0048b7f8 | "Chernov}" | "{Artyom Chernov}" |

2. **A whole string, but another one** (kept by the tool; wrong content for the key):
   `title.top_scores` reads "  Post Scores  " (AirStrike 2: "Top Scores"); `heli.4` "Lava
   Hammer" and `heli.5` "  Restart  " (Gulf Thunder has three helicopters, keys
   `heli.name.0..2` in frontend.delta.md 3.18, but the file lists `heli.0..5`);
   `info.pages.1..8` read "1 of 7", "2 of 7", "2 of 7", "3 of 7" … "7 of 7" (shifted by one
   from `info.pages.3` on, and eight keys for seven pages); the `credits.N` keys are shifted
   against their lines (e.g. `credits.3` "{Dmitry Zakharov}", `credits.8` "3D MODELING &
   LEVEL DESIGN").

The pattern (4 bytes off, or one string off) looks like the "placed by a unique identical
string" step of the generator landing on a neighbour. The dialogue pages, the Information pages
(`info.N.*`) and the menu captions otherwise read correctly.

## Our engine

The extraction tool and the web page (`apps/web/site/files.js`) read the file the way AirStrike
2's `as2.json` is read (package E: `json` plus `override`), with Gulf Thunder's corrections in
their Gulf Thunder entry:

- read from where the text is (`override`, the address found by searching the executable for
  the NUL-delimited text): `title.top_scores` 0x48CBD0 "Top Scores", `title.enter_name`
  0x48CBF0, `title.hint` 0x48CD70 "Tutorial Tip", `difficulty.3` 0x48CD34 "Hard",
  `difficulty.4` 0x48CD28 "Nightmare", `mode.0` 0x48CD18, `mode.1` 0x48CD0C, `button.ok`
  0x48CC00 "   Ok   ", `info.pages.3..7` 0x48BE70 down to 0x48BE50 ("3 of 7" … "7 of 7");
  `heli.2` 0x48BDA4 "Steel Falcon" (GUESS: the name that follows "Red Hawk" and "Sky Keeper"
  in the executable's name block; Gulf Thunder's third helicopter's name was not seen on screen);
- left out (`leave_out`, the game's built-in text is used): `heli.3`, `heli.4`, `heli.5`,
  `info.pages.8`, `credits.4`, `credits.19`.

224 entries are written. The credits keys stay shifted. `gulf.json` is not changed here (it
belongs to the spec package); correcting it needs `re/tools/gen_exe_texts_gulf.py` re-run with
the placement fixed, then the Gulf Thunder list of `apps/web/site/files.js` regenerated from it
(apps/tests/gulf_rules_test.cpp fails until the two lists agree) and the corrections above
removed.
