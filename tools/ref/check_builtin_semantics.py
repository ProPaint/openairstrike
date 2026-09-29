#!/usr/bin/env python3
"""Consistency check between docs/spec/rcsl-builtins-table.md and
docs/spec/rcsl-builtins-semantics.md.

Checks:
- the semantics summary table has exactly one row per builtin of the table spec
  (no missing, no extra, no duplicate names);
- every summary row has a priority P0, P1 or P2;
- no P0 builtin carries GUESS in its summary confidence column;
- every builtin has a detailed entry (a heading "### `name`") in the semantics spec.

Prints one line "OK ..." on success; lists the problems and exits 1 otherwise.
Stdlib only; needs no game data.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
TABLE = os.path.join(REPO, "docs", "spec", "rcsl-builtins-table.md")
SEM = os.path.join(REPO, "docs", "spec", "rcsl-builtins-semantics.md")


def section_rows(text, heading):
    """Table rows (lists of stripped cells) of the first table after `heading`."""
    lines = text.splitlines()
    try:
        start = next(i for i, l in enumerate(lines) if l.strip() == heading)
    except StopIteration:
        raise SystemExit("heading not found: %r" % heading)
    rows, in_table = [], False
    for l in lines[start + 1:]:
        if l.startswith("|"):
            in_table = True
            cells = [c.strip() for c in l.strip().strip("|").split("|")]
            if all(set(c) <= set("-: ") for c in cells):
                continue
            rows.append(cells)
        elif in_table:
            break
        elif l.startswith("## "):
            break
    return rows[1:]  # drop the header row


def name_of(cell):
    m = re.fullmatch(r"`([A-Za-z_]\w*)`", cell)
    return m.group(1) if m else None


def main():
    problems = []
    table = section_rows(open(TABLE, encoding="utf-8").read(), "## Table")
    expected = [name_of(r[1]) for r in table]
    if None in expected:
        problems.append("unparsable name in rcsl-builtins-table.md")
    sem_text = open(SEM, encoding="utf-8").read()
    rows = section_rows(sem_text, "## Summary table")
    got = []
    for r in rows:
        n = name_of(r[1]) if len(r) > 5 else None
        if n is None:
            problems.append("unparsable summary row: %r" % (r,))
            continue
        got.append(n)
        prio, conf = r[4], r[5]
        if prio not in ("P0", "P1", "P2"):
            problems.append("%s: bad priority %r" % (n, prio))
        if prio == "P0" and "GUESS" in conf:
            problems.append("%s: P0 builtin with GUESS in its summary confidence" % n)
    for n in sorted(set(expected) - set(got)):
        problems.append("missing from summary table: " + n)
    for n in sorted(set(got) - set(expected)):
        problems.append("not a builtin of rcsl-builtins-table.md: " + n)
    for n in sorted({n for n in got if got.count(n) > 1}):
        problems.append("duplicate summary row: " + n)
    headings = set(re.findall(r"^### (?:\d+\. )?`([A-Za-z_]\w*)`", sem_text, re.M))
    for n in sorted(set(expected) - headings):
        problems.append("no detailed entry: " + n)
    if problems:
        for p in problems:
            print("FAIL:", p)
        return 1
    p0 = sum(1 for r in rows if r[4] == "P0")
    print("OK: %d builtins, %d P0, summary and entries match rcsl-builtins-table.md" % (len(got), p0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
