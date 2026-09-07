#!/usr/bin/env python3
"""Check that every icon name used in the UI code exists in data/icons.qrc.

A misspelled or renamed icon name still compiles — the action simply shows no
icon at runtime. This script turns that silent failure into a build-time error:

    python3 scripts/check-icon-names.py

It also reports icons that are present in the resources but never referenced,
which is how a newly delivered icon set gets noticed.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
QRC = ROOT / "data" / "icons.qrc"
UI_DIR = ROOT / "code"

# Icon names are matched at their call sites rather than by shape, so that
# single-word names ("Undo", "Refresh") are covered too. Besides actionIcon()
# itself, the pane context menu in filedifftab.cpp passes the name through a
# local add() helper — a new wrapper would show up in the "unused" report.
NAME_RES = [
    re.compile(r'actionIcon\(QStringLiteral\("([^"]+)"\)\)'),
    re.compile(r'\badd\(QStringLiteral\("([^"]+)"\),'),
]
ALIAS_RE = re.compile(r'alias="([^"]+?)_(\d+)\.png"')

# Names the code asks for but tolerates the absence of (it falls back to a
# theme icon). Not an error — just reported, so a later delivery gets noticed.
OPTIONAL = {"Filter"}


def main() -> int:
    if not QRC.exists():
        print(f"error: {QRC} missing — run scripts/generate-icons-qrc.py",
              file=sys.stderr)
        return 1

    available = {m.group(1) for m in ALIAS_RE.finditer(QRC.read_text("utf-8"))}
    available.discard("diff")  # app logo, not an action icon

    used: dict[str, list[str]] = {}
    for src in sorted(UI_DIR.rglob("*.cpp")) + sorted(UI_DIR.rglob("*.h")):
        for lineno, line in enumerate(src.read_text("utf-8").splitlines(), 1):
            for name_re in NAME_RES:
                for name in name_re.findall(line):
                    used.setdefault(name, []).append(
                        f"{src.relative_to(ROOT)}:{lineno}")

    missing = {n: loc for n, loc in used.items()
               if n not in available and n not in OPTIONAL}
    pending = sorted(n for n in used
                     if n not in available and n in OPTIONAL)
    unused = sorted(available - set(used))

    for name, locations in sorted(missing.items()):
        for loc in locations:
            print(f"{loc}: error: icon {name!r} is not in data/icons.qrc")
    for name in pending:
        print(f"note: optional icon {name!r} not delivered yet "
              f"— theme icon is used instead")
    if unused:
        print(f"note: {len(unused)} icon(s) in resources but unused: "
              f"{', '.join(unused)}")

    print(f"{len(used)} icon names used, {len(available)} available, "
          f"{len(missing)} missing")
    return 1 if missing else 0


if __name__ == "__main__":
    raise SystemExit(main())
