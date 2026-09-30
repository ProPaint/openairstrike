#!/usr/bin/env python3
"""Consistency check between a builtin table and its semantics spec.

Default (first game, `--game as3d`): docs/spec/rcsl-builtins-table.md against
docs/spec/rcsl-builtins-semantics.md:
- the semantics summary table has exactly one row per builtin of the table spec
  (no missing, no extra, no duplicate names);
- every summary row has a priority P0, P1 or P2;
- no P0 builtin carries GUESS in its summary confidence column;
- every builtin has a detailed entry (a heading "### `name`") in the semantics spec.

`--game as2`: testdata/golden/as2/rcsl_builtins.json (101 builtins) against the delta
docs/spec/as2/rcsl-builtins-semantics.delta.md:
- the delta's summary table has exactly one row per builtin of the JSON;
- the status is `same`, `changed` or `new`, and `new` exactly for the builtins without a
  v1.70 counterpart in the JSON;
- the priority is P0, P1 or P2, and no P0 builtin carries GUESS in its confidence column;
- every `changed` and `new` builtin has a detailed entry (a heading "### ... `name` ...");
- the entry of every `changed` builtin cites an address in both executables
  (`v170@0x...` and `as2@0x...`).

`--game gulf`: testdata/golden/gulf/rcsl_builtins.json against
docs/spec/gulf/rcsl-builtins-semantics.delta.md with the same rules, relative to AirStrike 2:
`new` exactly for the builtins whose JSON `as2` field is missing, and a `changed` entry cites
`as2@0x...` and `gulf@0x...`.

Prints one line "OK ..." on success; lists the problems and exits 1 otherwise.
Stdlib only; needs no game data.
"""
import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
TABLE = os.path.join(REPO, "docs", "spec", "rcsl-builtins-table.md")
SEM = os.path.join(REPO, "docs", "spec", "rcsl-builtins-semantics.md")
DELTA = {g: os.path.join(REPO, "docs", "spec", g, "rcsl-builtins-semantics.delta.md") for g in ("as2", "gulf")}
JSON = {g: os.path.join(REPO, "testdata", "golden", g, "rcsl_builtins.json") for g in ("as2", "gulf")}
# The game a delta is written against: its status column and the JSON field that says whether
# a builtin existed there; a `changed` entry cites both executables.
BASE = {"as2": "v170", "gulf": "as2"}


def section_rows(text, heading, with_header=False):
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
    return rows if with_header else rows[1:]  # drop the header row


def name_of(cell):
    m = re.fullmatch(r"`([A-Za-z_]\w*)`", cell)
    return m.group(1) if m else None


def check_base():
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
        return problems, None
    p0 = sum(1 for r in rows if r[4] == "P0")
    return [], "OK: %d builtins, %d P0, summary and entries match rcsl-builtins-table.md" % (len(got), p0)


def entries(text):
    """Map builtin name -> text of its detailed entry (every backticked name in a ### heading)."""
    out = {}
    parts = re.split(r"^(### .*)$", text, flags=re.M)
    for i in range(1, len(parts), 2):
        body = parts[i + 1].split("\n## ")[0].split("\n---")[0]
        for n in re.findall(r"`([A-Za-z_]\w*)`", parts[i]):
            out[n] = parts[i] + body
    return out


def check_delta(game):
    problems = []
    builtins = json.load(open(JSON[game], encoding="utf-8"))["builtins"]
    expected = {b["name"]: b for b in builtins}
    text = open(DELTA[game], encoding="utf-8").read()
    rows = section_rows(text, "## Summary table", with_header=True)
    header = [c.lower() for c in rows[0]]
    try:
        ci = {k: header.index(k) for k in ("builtin", "status", "priority", "confidence")}
    except ValueError:
        return ["summary table header must have Builtin, Status, Priority, Confidence: %r" % rows[0]], None
    got, status, prio = [], {}, {}
    for r in rows[1:]:
        n = name_of(r[ci["builtin"]]) if len(r) == len(header) else None
        if n is None:
            problems.append("unparsable summary row: %r" % (r,))
            continue
        got.append(n)
        st, pr, conf = r[ci["status"]], r[ci["priority"]], r[ci["confidence"]]
        status[n], prio[n] = st, pr
        if st not in ("same", "changed", "new"):
            problems.append("%s: bad status %r" % (n, st))
        elif n in expected and (st == "new") != (expected[n].get(BASE[game]) is None):
            problems.append("%s: status %r disagrees with the JSON's %s field" % (n, st, BASE[game]))
        if pr not in ("P0", "P1", "P2"):
            problems.append("%s: bad priority %r" % (n, pr))
        if pr == "P0" and "GUESS" in conf:
            problems.append("%s: P0 builtin with GUESS in its summary confidence" % n)
    for n in sorted(set(expected) - set(got)):
        problems.append("missing from summary table: " + n)
    for n in sorted(set(got) - set(expected)):
        problems.append("not a builtin of %s: %s" % (os.path.relpath(JSON[game], REPO), n))
    for n in sorted({n for n in got if got.count(n) > 1}):
        problems.append("duplicate summary row: " + n)
    ent = entries(text)
    for n in sorted(got):
        if status.get(n) in ("changed", "new") and n not in ent:
            problems.append("%s: %s builtin without a detailed entry" % (n, status[n]))
        if status.get(n) == "changed" and n in ent:
            body = ent[n]
            if "%s@0x" % BASE[game] not in body or "%s@0x" % game not in body:
                problems.append("%s: changed entry must cite %s@0x... and %s@0x..." % (n, BASE[game], game))
    if problems:
        return problems, None
    count = {s: sum(1 for v in status.values() if v == s) for s in ("same", "changed", "new")}
    p0 = sum(1 for v in prio.values() if v == "P0")
    return [], ("OK: %s: %d builtins (%d same, %d changed, %d new), %d P0, summary and entries match %s"
                % (game, len(got), count["same"], count["changed"], count["new"], p0,
                   os.path.relpath(JSON[game], REPO)))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--game", default="as3d", choices=["as3d"] + sorted(DELTA))
    args = ap.parse_args(argv)
    problems, ok = check_base() if args.game == "as3d" else check_delta(args.game)
    if problems:
        for p in problems:
            print("FAIL:", p)
        return 1
    print(ok)
    return 0


if __name__ == "__main__":
    sys.exit(main())
