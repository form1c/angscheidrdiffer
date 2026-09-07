#!/usr/bin/env python3
"""Generate data/icons.qrc from the graphics/ folder.

Run this after the icon set in graphics/icons/<size>/ changes:

    python3 scripts/generate-icons-qrc.py

Embedded sizes are deliberately limited to the ones the UI actually asks for
(16-64, see code/ui/iconloader.h) — the larger renditions would only bloat the
binary. The app logo comes from graphics/logo/.
"""
from __future__ import annotations

import sys
from pathlib import Path

ICON_SIZES = [16, 22, 24, 32, 48, 64]
LOGO_SIZES = [16, 22, 24, 32, 48, 64, 128, 256]

ROOT = Path(__file__).resolve().parent.parent
ICON_DIR = ROOT / "graphics" / "icons"
LOGO_DIR = ROOT / "graphics" / "logo"
QRC = ROOT / "data" / "icons.qrc"


def icon_names() -> list[str]:
    """Base names present in *every* embedded size (largest is the reference)."""
    reference = ICON_DIR / str(ICON_SIZES[-1])
    names = sorted(p.name[: -len(f"_{ICON_SIZES[-1]}.png")]
                   for p in reference.glob(f"*_{ICON_SIZES[-1]}.png"))
    complete, incomplete = [], []
    for name in names:
        missing = [s for s in ICON_SIZES
                   if not (ICON_DIR / str(s) / f"{name}_{s}.png").exists()]
        (incomplete if missing else complete).append((name, missing))
    for name, missing in incomplete:
        print(f"warning: skipping {name!r} — missing sizes {missing}",
              file=sys.stderr)
    return [name for name, _ in complete]


def main() -> int:
    if not ICON_DIR.is_dir():
        print(f"error: {ICON_DIR} not found", file=sys.stderr)
        return 1

    names = icon_names()
    lines = ["<RCC>", '    <qresource prefix="/icons">']
    for size in LOGO_SIZES:
        if (LOGO_DIR / f"diff_{size}.png").exists():
            lines.append(f'        <file alias="diff_{size}.png">'
                         f'../graphics/logo/diff_{size}.png</file>')
    lines += ["    </qresource>", '    <qresource prefix="/actions">']
    for name in names:
        for size in ICON_SIZES:
            lines.append(f'        <file alias="{name}_{size}.png">'
                         f'../graphics/icons/{size}/{name}_{size}.png</file>')
    lines += ["    </qresource>", "</RCC>", ""]

    QRC.write_text("\n".join(lines), encoding="utf-8")
    print(f"{QRC.relative_to(ROOT)}: {len(names)} icons "
          f"x {len(ICON_SIZES)} sizes = {len(names) * len(ICON_SIZES)} entries")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
