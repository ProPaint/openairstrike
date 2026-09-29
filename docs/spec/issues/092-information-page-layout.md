# 092: Information page line slots and icons

Status: implemented (WP-46), partly GUESS.

- **Line slots.** `frontend.md` §3.14 gives the page texts' address range but not where the
  blank lines go. The page builders store each string pointer into a per-line array with
  `mov dword [reg+disp8], imm32`; the slot is (disp8 - 0x14) / 4 and skipped slots are blank
  lines (for example page 1 has 13 strings in 15 lines). `tools/extract_exe_texts.py` reads the
  slot from that instruction (found by searching `.text` for the string's address) and writes
  it into the key (`info.N.L`). Body line L is drawn at y = 144 + 18 L. Page 10 (credits) uses
  slots 4 to 14, so the names start below the centred title at y = 144.
- **Icons.** Drawn at (60, 154 + 72 k) for paragraph k as the spec says; with paragraphs that
  are not 4 lines apart (page 5: slots 0, 4, 7, 13) an icon can sit a line or two away from its
  heading. Which icon each paragraph shows is a GUESS from the headings (spec §8 question 5):
  page 4 weapons 0-3; page 5 weapons 5, 6, 4, 8 (BPG, GOROX, Laser, Wave); page 6 weapons 7, 9;
  page 7 missiles 0-3; page 8 missile 4; page 9 power-ups 3, 1, 2, 0 (Cluster, Nuclear, Rocket
  Strike, Lightning).
- **Without the texts file** the screen shows the title "INFORMATION" and three lines saying the
  texts are not installed and how to import them; the page spinner still works.
