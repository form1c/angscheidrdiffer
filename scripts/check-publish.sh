#!/bin/bash
# Checks this directory for content that should not be published.
# Usage:  ./scripts/check-publish.sh      (run from the repository)
# Result: exit 0 = nothing found, 1 = at least one finding.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR" || exit 2

# This script carries the search patterns itself and is therefore excluded.
EXCL=(--exclude-dir=build --exclude-dir=build-deb --exclude-dir=release
      --exclude-dir=.git --exclude=check-publish.sh --binary-files=without-match)
FINDEXCL=(-not -path './build/*' -not -path './build-deb/*'
          -not -path './release/*' -not -path './.git/*')
findings=0

report() {   # report <heading> <findings>
    if [ -n "$2" ]; then
        echo "FINDING: $1"
        echo "$2" | sed 's/^/    /'
        findings=$((findings + 1))
    else
        echo "ok: $1"
    fi
}

report "No paths of the development machine" \
    "$(grep -rn '/mnt/Workspace\|/home/\|/mnt/Backup' . "${EXCL[@]}" 2>/dev/null)"

report "No foreign mail addresses" \
    "$(grep -rEn '[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}' . "${EXCL[@]}" 2>/dev/null \
       | grep -v 'apps@unifyzer\.de')"

report "No backup or editor files" \
    "$(find . \( -name '*~' -o -name '*.bak' -o -name '*.orig' -o -name '*.rej' \
                 -o -name '*.swp' -o -name '*.tmp' -o -name '*.old' \) "${FINDEXCL[@]}")"

report "No image source files" \
    "$(find . \( -iname '*.psd' -o -iname '*.xcf' -o -iname '*.ai' -o -iname '*.sketch' \) \
            "${FINDEXCL[@]}")"

report "No symbolic links" "$(find . -type l "${FINDEXCL[@]}")"

report "No file above 5 MB" \
    "$(find . -type f -size +5M "${FINDEXCL[@]}" -printf '%s %p\n')"

# The source is written in English. Comments in another language are a defect,
# see CONTRIBUTING.md. German is expected only in the translated interface
# strings of the desktop files and the AppStream data, and in the sample data
# under testdata/, so those are excluded here.
report "Source and scripts are in English" \
    "$(grep -rnE '[äöüßÄÖÜ]' . "${EXCL[@]}" \
        --exclude-dir=testdata --exclude=\*.desktop --exclude=\*.metainfo.xml \
        2>/dev/null)"

# Image metadata: reported only, it does not fail the check.
if command -v python3 > /dev/null; then
    meta="$(python3 - <<'EOF' 2>/dev/null
from pathlib import Path
try:
    from PIL import Image
except ImportError:
    raise SystemExit
skip = {"transparency", "gamma", "dpi", "srgb", "chromaticity", "aspect"}
for p in sorted(Path(".").rglob("*.png")):
    if any(part in ("build", "build-deb", "release", ".git") for part in p.parts):
        continue
    keys = [k for k in Image.open(p).info if k not in skip]
    if keys:
        print(f"{p}: {', '.join(keys)}")
EOF
)"
    [ -n "$meta" ] && echo "note: images carrying metadata (informational)" \
                   && echo "$meta" | sed 's/^/    /' | head -5
fi

echo
if [ "$findings" -eq 0 ]; then
    echo "Result: no findings."
else
    echo "Result: $findings finding(s). Resolve before publishing."
fi
exit $(( findings > 0 ))
