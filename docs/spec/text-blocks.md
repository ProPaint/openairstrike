# Generic brace-block text syntax

Spec version 1.0. Reference implementation: `tools/ref/textblock.py`.
Engine implementation: `engine/src/formats/textblock.cpp`
(`engine/include/as3d/textblock.h`).

This is the plain-text, brace-delimited syntax used by all 42 game data text
files:

| Directory | Extension | Count | Named or anonymous blocks |
|---|---|---|---|
| `objects/` | `.obj` | 30 | named |
| `weapons/` | `.wpn` | 5 | named |
| `particles/` | `.ps` | 6 | named |
| `maps/` | `levels.txt` (1 file) | 1 | anonymous |

All claims in this document are VERIFIED-DATA against these 42 files unless
tagged otherwise, checked by `tools/ref/test_textblock.py` on every run.

## Encoding and line endings

- VERIFIED-DATA: all 42 files use CRLF (`\r\n`) line endings throughout.
  The parser also accepts bare `\n` (a line simply has no trailing `\r` to
  strip); nothing in the shipped data exercises this, but it costs nothing
  and downstream tools sometimes normalize line endings.
- VERIFIED-DATA: the files are Windows cp1251 text, but the grammar itself
  (`{`, `}`, `"`, `//`, whitespace, digits, `-`, `.`) is pure ASCII. Neither
  reference implementation ever decodes cp1251: bytes are passed through
  untouched, including inside comments and quoted strings. 7 of the 42 files
  (`objects/boss2.obj`, `objects/effects.obj`, `objects/helics.obj`,
  `objects/planes.obj`, `objects/ruins.obj`, `objects/tanks.obj`,
  `particles/particles.ps`) contain bytes above `0x7F` (cp1251 Cyrillic),
  and in every case they occur only inside `//` comments, never in an
  identifier, key, or unquoted argument. `TextToken.text` /
  `TextToken::text` hold the raw bytes; the Python reference stores them via
  a lossless byte-for-byte ISO-8859-1 ("latin-1") mapping purely as a `str`
  container, not as a claim that the text is Latin-1.
- VERIFIED-DATA: no file starts with a byte-order mark, and none contain a
  NUL byte. The engine parser still never assumes NUL-termination: it works
  from an explicit `(data, size)` pair throughout, so an embedded NUL in a
  hypothetical malformed or fuzzed input is just an ordinary byte inside a
  token, not a truncation point.

## Grammar

```
file       := (comment | block)*
block      := (identifier)? "{" statement* "}"
statement  := key argument* end-of-line
key        := identifier
argument   := string | number | identifier
string     := '"' char* '"'
comment    := "//" char* end-of-line
```

- One **statement per physical line**: a key followed by zero or more
  arguments, ending at the newline. There is no line-continuation syntax.
- A block name, when present, and the block's own statements are keys and
  bare/quoted argument tokens exactly like a statement's arguments — the
  grammar does not otherwise distinguish "identifier syntax" from "argument
  syntax". A bare token is any maximal run of bytes not containing
  whitespace, `{`, `}`, `"`, or the two-byte sequence `//`.
- `//` starts a line comment. It is recognized anywhere outside a quoted
  string — including, defensively, in the middle of what would otherwise be
  a bare token — and runs to the end of the physical line. VERIFIED-DATA:
  in the shipped data `//` is always preceded by whitespace or is the first
  thing on the line; nothing exercises the mid-token case.
- Quoted strings are delimited by `"..."` and must close before the end of
  the line; there is no escape syntax (VERIFIED-DATA: no file contains a
  `\"` sequence or an odd number of `"` per code portion of any line) and a
  string can't span multiple lines.
- Numbers are recognized post-hoc, per `TextToken::isNumber()` /
  `TextToken.is_number()`, not by the lexer: a token is a number if it is
  **not** a quoted string and matches `-?[0-9]+(\.[0-9]+)?` — an optional
  leading `-`, one or more digits, and an optional `.` followed by one or
  more digits. VERIFIED-DATA: no shipped number uses a leading `+`, a
  leading bare `.` (e.g. `.5`), scientific notation, or a trailing `.` with
  no fractional digits. `asFloat()`/`as_float()` and `asInt()`/`as_int()`
  return `0`/`0.0` for a non-number token, matching the "no exceptions"
  engine style (`strtof`/`strtol`, not `std::stof`).

## Block headers: name and `{` placement

VERIFIED-DATA: the overwhelming majority of blocks open as `name {` (or
just `{` for the anonymous blocks in `levels.txt`) on one line, e.g.:

```
tank_small_green {
```

but `objects/boss1.obj` line 27 has the name and `{` on separate lines:

```
boss1_rocketlaucher
{
```

The parser therefore treats a bare/quoted token seen outside any block as a
**pending block name**, not tied to a specific line: whatever `{` or other
token follows next (skipping blank/comment-only lines) resolves it. This is
also how anonymous blocks and named blocks share one code path: at
depth 0, the next `{` either confirms the pending name (named block) or, if
there is no pending name, opens an anonymous block.

VERIFIED-DATA: `}` never shares a line with other tokens in the shipped
data (every closing brace is alone on its line, `}`), and no block ever
nests inside another (see below). The tokenizer does not special-case this:
`{` and `}` are ordinary one-character tokens recognized at any position, so
`}` sharing a line with statement text, or a whole `key value` statement on
the same line as an opening `{`, would parse the same way statements
normally do — this just never happens to be exercised by real data.

## Comments

- Line comments (`//`) are the only comment syntax used or supported.
  VERIFIED-DATA: no shipped file contains `/*` or `*/` anywhere. The parser
  does not implement C-style block comments; if a file contained `/*`, the
  two characters would simply be ordinary bytes inside whatever bare token
  or comment they fall in (a `/` on its own does not start a comment; only
  `//` does), never a crash or a hang.

## Nested blocks

VERIFIED-DATA: **no block nests inside another anywhere in the 42 shipped
files** — depth never exceeds 1 in real data, checked exhaustively by
`tools/ref/test_textblock.py`. This matters because `TextBlock` (in
`engine/include/as3d/textblock.h`, not editable by this package) has no
`children` field, so there would be nowhere to put a nested block's own
identity if one existed.

Both reference implementations still handle the hypothetical defensively,
for the fuzz test and to guarantee termination on arbitrary input: an
unexpected `{` while already inside a block increments an internal "extra
depth" counter and records an error
(`line N: nested '{' inside block 'X', flattening into the parent block`);
the nested block's own statements are then appended directly to the
*enclosing* block (flattened, exactly as the task describes), and its
matching `}` just decrements the extra-depth counter instead of closing the
enclosing block early. A nested block's own name (if it had one on its own
line before the `{`) is treated like any other bare token and becomes a
degenerate zero-argument statement of the parent block — this is a lossy
but safe fallback; see "Open problems" below. **No public header change is
required for the shipped data**, since the case never occurs.

## Duplicate block names

VERIFIED-DATA: `objects/tanks.obj` defines `tank_dead` twice — once at line
41 (`model "models/tanks/tank_dead.mdl"`, ...) and again at line 180 (a
different, unrelated body: `flag FL_ONGROUND_NORMAL`, `model
"models/tanks/tank_medium/tank4.mdl"`, ...). This is the only duplicate
block name anywhere in `objects/*.obj`, and no duplicate exists in any
`.wpn`, `.ps` file, or in `levels.txt`'s `id` values.

The parser does not deduplicate or special-case this: both blocks are
parsed and appended to `TextFile::blocks` in file order, with no error. Two
`TextBlock`s with the same `name` is a legal, silent outcome; it is up to a
later name-keyed loader to decide on first-wins/last-wins semantics (see
"Object count" below — GUESS: last-wins, unverified against the exe).

## Error recovery

`TextFile::errors` collects "line N: message" strings; parsing always
continues afterward, and the function never crashes or loops forever on any
input, including arbitrary/fuzzed bytes. Concretely:

| Situation | Behaviour |
|---|---|
| Unterminated `"` (no closing quote before end of line) | The rest of the line becomes the string's content; error `line N: unterminated string`; parsing resumes on the next line. VERIFIED-DATA: never occurs in the shipped data. |
| `}` with no open block | Error `line N: unexpected '}' with no open block`; the stray `}` is discarded, one token consumed. VERIFIED-DATA: never occurs. |
| A block name (bare or quoted token) at depth 0 not followed by `{` | Error `line N: expected '{' after block name 'X'`; the pending name is dropped and the same token is re-examined as a fresh depth-0 token (so e.g. two stray words in a row each get their own error, and a stray word immediately followed by `{` still opens a block). VERIFIED-DATA: never occurs. |
| A block name pending at end of file, no `{` ever seen | Error `line N: block name 'X' at end of file with no '{'`. VERIFIED-DATA: never occurs (every file ends outside any block, past any pending name). |
| `{` never closed by matching `}` before end of file | Error `line N: unterminated block 'X' (missing '}')`, naming the block's start line; the block is still appended to `TextFile::blocks` with whatever statements it collected (best-effort). VERIFIED-DATA: never occurs; every block in the shipped data is properly closed (brace balance is exactly 0 at end of file in all 42 files). |
| Nested `{` | See "Nested blocks" above. VERIFIED-DATA: never occurs. |
| Empty file (0 bytes) | Zero blocks, zero errors. Not present in the shipped data but trivially supported (the line-splitting loop simply doesn't run). |
| Arbitrary/random bytes (fuzz input) | No situation above can recurse or spin: every code path that doesn't advance to the next token index either records an error and clears exactly one piece of pending state (guaranteeing the very next check falls through to a token-consuming branch) or is reached only once per token. The outer loop is bounded by the number of physical lines, physical lines are strictly shorter than the input, and inner loops are bounded by the number of tokens on a line. |

Every one of the "VERIFIED-DATA: never occurs" rows above is why the unit
tests for these paths use synthetic inputs (`apps/tests/textblock_test.cpp`)
rather than real game files.

## Whitespace

VERIFIED-DATA: both tabs and (rarer) runs of spaces are used to separate a
key from its arguments, sometimes inconsistently within the same file (see
`maps/levels.txt` line 17, `water\t \t"..."`, mixing a tab, a space, and a
tab). The parser treats space, tab, CR, VT and FF interchangeably as
inter-token whitespace; indentation is never significant.

## Case sensitivity

VERIFIED-DATA: statement keys are lowercase in every file except one
mixed-case outlier, `enableHelic` (`maps/levels.txt`, 8 occurrences, always
spelled the same way). `TextBlock::find()` looks keys up
case-insensitively for exactly this reason. The parser does not itself
normalize case anywhere else (block names, identifier arguments, and flag
values are stored byte-for-byte as written).

## Statement key inventory

Generated by `tools/ref/textblock.py` from the shipped data (also see
`testdata/golden/textblock_counts.json` for the raw per-file counts this is
rolled up from). Argument shapes: `STR` = quoted string, `NUM` = numeric
token, `ID` = bare non-numeric token; `-` = no arguments. This inventory is
the input for a later package's typed loaders — it is descriptive, not
prescriptive; nothing here dictates what a given key *means*.

### `objects/*.obj` — 30 files, 864 blocks, 6126 statements, 24 distinct keys

| Key | Count | Argument shapes observed (count) |
|---|---|---|
| `attach` | 1251 | `(STR,STR)` x397; `(ID,STR,STR)` x266; `(ID,ID,STR,STR)` x190; `(ID,STR)` x136; `(ID,STR,STR,ID)` x120; `(ID,STR,STR,STR)` x65; `(ID,ID,STR,STR,STR)` x52; `(STR,ID)` x10; `(ID,ID,STR,ID,STR)` x9; `(ID,ID)` x3; `(ID,STR,ID,STR,STR)` x2; `(ID,STR,ID,STR)` x1 |
| `skin` | 732 | `(STR)` x732 |
| `model` | 700 | `(STR)` x700 |
| `script` | 611 | `(STR)` x611 |
| `rflag` | 463 | `(ID)` x463 |
| `flag` | 450 | `(ID)` x450 |
| `shadow` | 302 | `(ID)` x302 |
| `blend` | 283 | `(ID)` x283 |
| `sort` | 173 | `(ID)` x173 |
| `health` | 162 | `(NUM)` x162 |
| `enemy` | 159 | `-` x159 |
| `touch` | 135 | `(ID)` x135 |
| `score` | 132 | `(NUM)` x132 |
| `type` | 108 | `(ID)` x108 |
| `min` | 108 | `(NUM,NUM,NUM,NUM)` x108 |
| `max` | 108 | `(NUM,NUM,NUM,NUM)` x108 |
| `envmap` | 69 | `(STR)` x69 |
| `envmode` | 69 | `(ID)` x69 |
| `damage` | 61 | `(NUM)` x61 |
| `light` | 13 | `(NUM,NUM,NUM,NUM)` x13 |
| `frames` | 11 | `(NUM,NUM)` x11 |
| `player` | 10 | `-` x10 |
| `bbox_scale` | 9 | `(NUM,NUM,NUM)` x9 |
| `light_dir` | 7 | `(NUM,NUM,NUM,NUM,NUM,NUM,NUM,NUM)` x7 |

`attach`'s first argument, when it's an `ID`, is a modifier keyword
(observed values include `abs`, `id`, and combinations of the two, e.g.
`attach abs id "GUNS" "tank_small_green_guns" "tag_guns"`); this spec does
not interpret the modifiers further — that belongs to a typed `.obj`
loader spec.

### `weapons/*.wpn` — 5 files, 63 blocks, 167 statements, 3 distinct keys

| Key | Count | Argument shapes observed (count) |
|---|---|---|
| `missile` | 63 | `(STR)` x63 |
| `speed` | 56 | `(NUM)` x56 |
| `flash` | 48 | `(STR)` x48 |

### `particles/*.ps` — 6 files, 80 blocks, 1031 statements, 19 distinct keys

| Key | Count | Argument shapes observed (count) |
|---|---|---|
| `blend_mode` | 80 | `(ID)` x80 |
| `texture` | 80 | `(STR,NUM,NUM)` x80 |
| `emit_rate` | 80 | `(NUM)` x80 |
| `life_time` | 80 | `(NUM)` x80 |
| `init_size` | 80 | `(NUM,NUM)` x80 |
| `init_color` | 80 | `(NUM,NUM,NUM,NUM)` x80 |
| `fade_mode` | 80 | `(ID)` x80 |
| `init_frame` | 77 | `(NUM,NUM)` x77 |
| `size` | 77 | `(NUM)` x77 |
| `init_velocity` | 74 | `(NUM,NUM,NUM,NUM,NUM,NUM)` x74 |
| `init_offset` | 70 | `(NUM,NUM,NUM,NUM,NUM,NUM)` x70 |
| `accel` | 59 | `(NUM,NUM,NUM)` x59 |
| `fade_factor` | 29 | `(NUM)` x29 |
| `rflag` | 27 | `(ID)` x27 |
| `coords` | 25 | `(ID)` x25 |
| `emit_mode` | 22 | `(ID)` x22 |
| `damage` | 5 | `(ID,NUM,NUM,NUM)` x5 |
| `anim_mode` | 3 | `(ID)` x3 |
| `draw_mode` | 3 | `(ID)` x3 |

### `maps/levels.txt` — 1 file, 24 (anonymous) blocks, 261 statements, 13 distinct keys

| Key | Count | Argument shapes observed (count) |
|---|---|---|
| `id` | 24 | `(STR)` x24 |
| `map` | 24 | `(STR)` x24 |
| `music` | 24 | `(STR)` x24 |
| `textures` | 24 | `(STR)` x24 |
| `hmin` | 24 | `(NUM)` x24 |
| `hmax` | 24 | `(NUM)` x24 |
| `fog` | 24 | `(NUM,NUM,NUM,NUM,NUM)` x24 |
| `sun` | 24 | `(NUM,NUM,NUM,NUM,NUM,NUM,NUM,NUM,NUM)` x24 |
| `water` | 23 | `(STR,NUM,NUM)` x23 |
| `name` | 20 | `(STR)` x20 |
| `night` | 14 | `-` x14 |
| `enableHelic` | 8 | `(NUM)` x8 |
| `intermission` | 4 | `(NUM,NUM,NUM,NUM,NUM,NUM)` x4 |

`name` (20/24) and `water` (23/24) are not universal: the 4 blocks missing
`name` are the intro/intermission cutscenes (`id` values `intro1`..`intro4`,
each carrying an `intermission` statement instead), which is presumably why
they have no player-visible mission name.

## Object count vs. the original game's log

VERIFIED-DATA: parsing all 30 `objects/*.obj` files yields exactly **864**
named blocks, matching the original game's own log line,
`G_LoadObjects: 864 objects parsed succefully` [sic], exactly. This is
strong evidence that "parsed" in that message counts every block the
loader's own parser produced — the same granularity as `TextFile::blocks`
here — not a count of distinct object names.

Of those 864 blocks, **863 have distinct names**: `objects/tanks.obj`
defines `tank_dead` twice (lines 41 and 180; see "Duplicate block names"
above). Since the shipped log count (864) matches the raw block count and
not the distinct-name count, the loader evidently does not reject or skip
the duplicate — GUESS (unverified against the executable): a later
`tank_dead` most likely overwrites an earlier one of the same name in
whatever name-indexed table the loader builds afterward (last-wins), since
that is the most common behavior for this kind of loader, but nothing here
proves it over first-wins; both are consistent with "864 parsed".

## Golden data

`testdata/golden/textblock_counts.json` holds, per file (relative path from
`assets_extracted/`, forward-slashed), the `blocks`, `statements`, and
`tokens` counts produced by `tools/ref/textblock.py`, plus a `summary` with
the object-count figures above and the list of duplicate object names. A
"token" is every `TextToken`/block name that isn't a brace or a comment:
one for each named block's name, plus one for each statement's key, plus
one per argument. `apps/tests/textblock_test.cpp` re-derives the same three
counts from the C++ parser for all 42 files and checks they match exactly,
in addition to its own synthetic-input unit tests and a fuzz test.

## Open problems

- `TextBlock` has no field to represent a nested block's own identity or
  children. Not currently a real problem — no nested block exists in any
  shipped file (see "Nested blocks") — but if a future data file (a mod, a
  sequel's assets, or a corrupted download) contained one, this parser
  would flatten it into the parent with a recorded error rather than
  losing data silently or crashing. If a genuine need for real nesting ever
  arises, `TextBlock` needs a `children` (or similar) field; that is an
  `engine/include/as3d/textblock.h` change outside this package's remit.
- The last-wins-vs-first-wins question for duplicate object names (see
  above) is a GUESS. Resolving it needs either the v1.70 executable's
  object-loading code (`VERIFIED-CODE`) or an in-game test showing which
  `tank_dead` definition actually gets used.

## Changelog

- 1.0 (WP-12): initial version. Grammar, edge cases, and the full
  statement-key inventory for all 42 shipped files.
