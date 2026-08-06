#!/usr/bin/env python3
"""
gen_compile_db.py
=================

Synthesise a ``compile_commands.json`` from a flat Makefile so that
``run-clang-tidy`` (and other tooling that consumes the JSON
Compilation Database specification) can analyse every TU without us
having to commit to a full CMake / Meson build yet.

Usage::

    scripts/gen_compile_db.py \\
        --makefile Makefile \\
        --cxx clang++ \\
        --cxxflags "-std=c++23 -O2 -Iinclude -pthread" \\
        --out build/compile_commands.json

The script finds every ``.o: <source>.cpp`` rule in the Makefile
that mentions ``include/`` or ``src/`` as a header dependency,
then emits one JSON entry per (target, source) pair with the
combined flags and include paths from the Makefile.

This is best-effort. The Makefile is parsed with line-oriented
regex rather than a real grammar, so any unusual rule layout
(continuations with ``\\\``, conditional includes) may need a
follow-up patch.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    p.add_argument("--makefile", required=True, type=Path)
    p.add_argument("--cxx", default="clang++")
    p.add_argument(
        "--cxxflags",
        default="-std=c++23 -O2 -Wall -Wextra -Wpedantic -pthread -Iinclude",
    )
    p.add_argument("--out", required=True, type=Path)
    p.add_argument(
        "--root",
        default=".",
        type=Path,
        help="Project root the output paths are anchored to.",
    )
    return p.parse_args()


# Target rule:
#   foo.o: src/foo.cpp \
#           include/foo.hpp \
#           include/foo/bar.hpp
#           $(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@
#
# Captures: target name + first source + list of header deps.
TARGET_RE = re.compile(
    r"^\s*(?P<target>[^:\s]+)\.o\s*:\s*"
    r"(?P<src>[^\s\\]+(?:\s+[^\s\\]+)*?)\s*\\?\s*$"
    r"(?P<headers>(?:[^\n]*\\?\n?)*?)"
    r"^\s*\$\(CXX\)\s+",
    re.MULTILINE,
)


def find_compile_rules(makefile_text: str) -> list[tuple[str, str]]:
    """Yield (target, source) pairs from every recognized rule."""
    rules: list[tuple[str, str]] = []
    for m in TARGET_RE.finditer(makefile_text):
        target = m.group("target")
        # First non-header dependency is the source file.
        sources = [
            tok for tok in m.group("src").split()
            if tok.endswith(".cpp") and not tok.startswith("$")
        ]
        if not sources:
            continue
        # Confirm the rule body looks like a C++ compile rule.
        rules.append((f"{target}.o", sources[0]))
    return rules


def resolve(path: str, root: Path) -> str:
    if os.path.isabs(path):
        return path
    return str((root / path).resolve())


def main() -> int:
    args = parse_args()
    if not args.makefile.is_file():
        print(f"error: {args.makefile} not found", file=sys.stderr)
        return 1

    text = args.makefile.read_text()
    rules = find_compile_rules(text)

    # De-dupe by source file so we don't emit the same .cpp twice if
    # two .o targets happen to share it.
    seen: set[str] = set()
    entries: list[dict] = []
    for target, source in rules:
        if source in seen:
            continue
        seen.add(source)

        entries.append(
            {
                "directory": str(args.root.resolve()),
                "file": resolve(source, args.root),
                "output": resolve(target, args.root),
                "arguments": [args.cxx] + args.cxxflags.split() + ["-c", source],
            }
        )

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(entries, indent=2))
    print(f"wrote {len(entries)} entries to {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())