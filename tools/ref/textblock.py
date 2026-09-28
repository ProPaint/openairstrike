#!/usr/bin/env python3
"""Reference parser for AirStrike 3D's generic brace-block text syntax.

Used by objects/*.obj, weapons/*.wpn, particles/*.ps and maps/levels.txt.
See docs/spec/text-blocks.md for the grammar and the edge-case decisions
this implementation follows. Stdlib only.

The files are plain text in the Windows cp1251 encoding, but every character
this parser treats specially (`{`, `}`, `"`, `/`, whitespace, digits) is
ASCII, so it never actually decodes the bytes: it works on `bytes` directly
and, where a piece of text must become a `str` (for `TextToken.text`), maps
each byte 1:1 through ISO-8859-1 ("latin-1"). That is a lossless container,
not a claim that the data is Latin-1; it exists only so a comment or a
Cyrillic asset name round-trips byte-for-byte without ever raising a
UnicodeDecodeError, matching the C++ implementation's raw-byte handling.
"""
from __future__ import annotations

import dataclasses
import glob
import os
import re
from typing import List, Optional, Tuple

_NUMBER_RE = re.compile(r"^-?[0-9]+(\.[0-9]+)?$")

_WHITESPACE = (0x20, 0x09, 0x0D, 0x0B, 0x0C)  # space, tab, CR, VT, FF


@dataclasses.dataclass
class TextToken:
    text: str
    quoted: bool = False

    def is_number(self) -> bool:
        return (not self.quoted) and _NUMBER_RE.match(self.text) is not None

    def as_float(self) -> float:
        return float(self.text) if self.is_number() else 0.0

    def as_int(self) -> int:
        if not self.is_number():
            return 0
        m = re.match(r"^-?[0-9]+", self.text)
        return int(m.group(0)) if m else 0


@dataclasses.dataclass
class TextStatement:
    key: str
    args: List[TextToken] = dataclasses.field(default_factory=list)
    line: int = 0


@dataclasses.dataclass
class TextBlock:
    name: str
    statements: List[TextStatement] = dataclasses.field(default_factory=list)
    line: int = 0

    def find(self, key: str) -> Optional[TextStatement]:
        lk = key.lower()
        for s in self.statements:
            if s.key.lower() == lk:
                return s
        return None


@dataclasses.dataclass
class TextFile:
    blocks: List[TextBlock] = dataclasses.field(default_factory=list)
    errors: List[str] = dataclasses.field(default_factory=list)

    def counts(self) -> dict:
        """Per-file block/statement/token counts, as stored in the golden JSON.

        A "token" is a key, a block name, or an argument -- i.e. every
        TextToken plus one for each statement key and each named block.
        Braces and comments are not tokens.
        """
        statements = sum(len(b.statements) for b in self.blocks)
        tokens = 0
        for b in self.blocks:
            if b.name:
                tokens += 1
            for s in b.statements:
                tokens += 1 + len(s.args)
        return {"blocks": len(self.blocks), "statements": statements, "tokens": tokens}


# ---------------------------------------------------------------------------
# Tokenizer: operates on one physical line (bytes, with any trailing \r
# already stripped). `//` starts a line comment anywhere outside quotes.
# A `"` opens a quoted string that must close on the same line; if it
# doesn't, the rest of the line becomes its (unterminated) content and the
# caller is told so it can record an error.
# ---------------------------------------------------------------------------

def _tokenize_line(line: bytes) -> Tuple[List[Tuple[str, str, bool]], bool]:
    tokens: List[Tuple[str, str, bool]] = []
    unterminated = False
    i, n = 0, len(line)
    while i < n:
        b = line[i]
        if b in _WHITESPACE:
            i += 1
            continue
        if b == 0x2F and i + 1 < n and line[i + 1] == 0x2F:  # "//"
            break
        if b == 0x7B:  # '{'
            tokens.append(("OPEN", "{", False))
            i += 1
            continue
        if b == 0x7D:  # '}'
            tokens.append(("CLOSE", "}", False))
            i += 1
            continue
        if b == 0x22:  # '"'
            j = i + 1
            while j < n and line[j] != 0x22:
                j += 1
            if j < n:
                text = line[i + 1 : j].decode("latin-1")
                tokens.append(("TOK", text, True))
                i = j + 1
            else:
                text = line[i + 1 : n].decode("latin-1")
                tokens.append(("TOK", text, True))
                unterminated = True
                i = n
            continue
        j = i
        while j < n:
            bj = line[j]
            if bj in _WHITESPACE or bj in (0x7B, 0x7D, 0x22):
                break
            if bj == 0x2F and j + 1 < n and line[j + 1] == 0x2F:
                break
            j += 1
        tokens.append(("TOK", line[i:j].decode("latin-1"), False))
        i = j
    return tokens, unterminated


def parse_text_blocks(data: bytes) -> TextFile:
    tf = TextFile()
    depth = 0        # 0 = outside any block, 1 = inside the current top-level block
    extra_depth = 0   # depth of (unsupported) nested '{' inside the current block
    pending_name: Optional[str] = None
    pending_name_line = 0
    current: Optional[TextBlock] = None

    lines = data.split(b"\n")
    for line_no, raw in enumerate(lines, start=1):
        if raw.endswith(b"\r"):
            raw = raw[:-1]
        tokens, unterminated = _tokenize_line(raw)
        if unterminated:
            tf.errors.append(f"line {line_no}: unterminated string")
        if not tokens:
            continue

        i, n = 0, len(tokens)
        while i < n:
            kind, text, quoted = tokens[i]
            if depth == 0:
                if pending_name is not None:
                    if kind == "OPEN":
                        current = TextBlock(name=pending_name, line=pending_name_line)
                        depth = 1
                        pending_name = None
                        i += 1
                        continue
                    tf.errors.append(
                        f"line {pending_name_line}: expected '{{' after block name "
                        f"'{pending_name}'"
                    )
                    pending_name = None
                    continue  # re-examine the same token, pending_name now cleared
                if kind == "OPEN":
                    current = TextBlock(name="", line=line_no)
                    depth = 1
                    i += 1
                    continue
                if kind == "CLOSE":
                    tf.errors.append(f"line {line_no}: unexpected '}}' with no open block")
                    i += 1
                    continue
                # A bare or quoted token outside any block: a candidate block name.
                pending_name = text
                pending_name_line = line_no
                i += 1
                continue
            else:
                assert current is not None
                if kind == "OPEN":
                    tf.errors.append(
                        f"line {line_no}: nested '{{' inside block '{current.name}', "
                        "flattening into the parent block"
                    )
                    extra_depth += 1
                    i += 1
                    continue
                if kind == "CLOSE":
                    if extra_depth > 0:
                        extra_depth -= 1
                        i += 1
                        continue
                    tf.blocks.append(current)
                    current = None
                    depth = 0
                    i += 1
                    continue
                # Statement: key followed by zero or more argument tokens, up to
                # the next brace (real ones never span more than one line).
                key = text
                key_line = line_no
                args: List[TextToken] = []
                i += 1
                while i < n and tokens[i][0] not in ("OPEN", "CLOSE"):
                    _, atext, aquoted = tokens[i]
                    args.append(TextToken(atext, aquoted))
                    i += 1
                current.statements.append(TextStatement(key, args, key_line))

    if pending_name is not None:
        tf.errors.append(
            f"line {pending_name_line}: block name '{pending_name}' at end of file "
            "with no '{'"
        )
    if depth == 1 and current is not None:
        tf.errors.append(
            f"line {current.line}: unterminated block '{current.name}' (missing '}}')"
        )
        tf.blocks.append(current)
    return tf


# ---------------------------------------------------------------------------
# Data-root discovery, shared with the other tools/ref/*.py reference parsers.
# ---------------------------------------------------------------------------

def data_root() -> str:
    env = os.environ.get("AS3D_DATA_ROOT")
    if env:
        return env
    # tools/ref/textblock.py -> tools/ref -> tools -> repo root (two levels up).
    return os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def extracted_dir() -> str:
    return os.path.join(data_root(), "assets_extracted")


def list_text_block_files(root: Optional[str] = None) -> List[str]:
    """Every .obj/.wpn/.ps file plus maps/levels.txt, as paths relative to
    assets_extracted, forward-slashed, sorted. Returns [] if the directory
    tree is missing (caller decides whether that is a SKIP or an error)."""
    base = root if root is not None else extracted_dir()
    names: List[str] = []
    for sub, ext in (("objects", ".obj"), ("weapons", ".wpn"), ("particles", ".ps")):
        d = os.path.join(base, sub)
        if not os.path.isdir(d):
            continue
        for fn in sorted(os.listdir(d)):
            if fn.lower().endswith(ext):
                names.append(f"{sub}/{fn}")
    if os.path.isfile(os.path.join(base, "maps", "levels.txt")):
        names.append("maps/levels.txt")
    return sorted(names)


def parse_file(path: str) -> TextFile:
    with open(path, "rb") as f:
        return parse_text_blocks(f.read())
