# 080: front-end texts live in the executable, not in the data

Status: open (decision needed). Raised by WP-26 (`frontend.md` §8 question 1).

The original draws several texts that are compiled into `AirStrike3D.exe` rather than stored
in the paks our engine loads:

- the ten Information pages (story, overview, weapons, missiles, items, credits; string
  addresses 0x449ea4..0x44b07c, layout in `frontend.md` §3.14);
- the Game Complete congratulations (0x449d6c..0x449df0, §3.9);
- the rank names (table 0x45650c), difficulty and option value names, "Choose mission:",
  the stat labels, the name-entry prompt, the exit question, the cheat messages, the default
  high-score names.

Menu button captions and screen titles are pictures in `menu\*.tga` and need nothing.

Options: (a) write our own texts (short labels are generic; the story and weapon
descriptions would be rewritten), or (b) an import step that reads the strings from the
user's executable at the listed addresses. The engine otherwise never needs the executable.

Until decided, implementations can use placeholder texts; the layouts in `frontend.md` do
not depend on the exact wording except for widths.
