#!/bin/bash
# Build a release bundle under release/ (gitignored): image, checksum, VERSION, CHANGELOG, INSTALL.md, docs subset,
# Mac-side tools. Scans the bundle with scripts/public-scan.sh and fails on findings. No network, no git writes.
# Usage: scripts/make-release.sh [--dry-run] [--no-tar] PATH/TO/ttsystem
#   --dry-run  assemble and scan in a temporary directory, print the manifest, write nothing under release/
#   --no-tar   leave the bundle directory only
# Env: PUBLIC_EMAIL (neutral address allowed to appear), PUBLIC_SCAN_EXTRA (extra regex: private hostnames, names, ids).
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DRY=0; TAR=1; IMAGE=
for a in "$@"; do
	case "$a" in
		--dry-run) DRY=1 ;;
		--no-tar) TAR=0 ;;
		-h|--help) sed -n '2,8p' "$0"; exit 0 ;;
		-*) echo "unknown option $a" >&2; exit 2 ;;
		*) [ -z "$IMAGE" ] || { echo "only one image argument" >&2; exit 2; }; IMAGE=$a ;;
	esac
done

VERSION=$(tr -d ' \n' < "$ROOT/VERSION")
case "$VERSION" in [0-9]*.[0-9]*.[0-9]*) ;; *) echo "bad VERSION: $VERSION" >&2; exit 2 ;; esac
NAME=homtom-$VERSION

die() { echo "make-release: $*" >&2; exit 1; }

# Source of the bundle must never be local state.
real() { (CDPATH= cd -- "$(dirname -- "$1")" 2>/dev/null && printf '%s/%s\n' "$(pwd -P)" "$(basename -- "$1")"); }
if [ -n "$IMAGE" ]; then
	[ -f "$IMAGE" ] || die "image not found: $IMAGE"
	IMAGE=$(real "$IMAGE")
	case "$IMAGE" in */.runtime/*|*/bt.pin|*/btkeys|*/ha_webhook) die "refusing image from a local-state path: $IMAGE" ;; esac
	python3 - "$ROOT" "$IMAGE" <<'PY' || die "image is not a valid ttsystem (TTBL records 0x31000000 and 0x31700000 expected)"
import sys
sys.path.insert(0, sys.argv[1] + "/firmware")
import ttimage
from pathlib import Path
secs, entry = ttimage.sections(Path(sys.argv[2]).read_bytes())
assert any(a == 0x31700000 for a, _ in secs) and any(a == 0x31000000 for a, _ in secs)
PY
elif [ "$DRY" = 1 ]; then
	echo "make-release: dry run without an image; image, checksum and tarball are skipped" >&2
else
	echo "usage: $0 [--dry-run] [--no-tar] PATH/TO/ttsystem" >&2; exit 2
fi

WORK=$ROOT/release
B=$WORK/$NAME
if [ "$DRY" = 0 ]; then
	mkdir -p "$WORK"
	[ ! -e "$B" ] || die "$B exists; remove it deliberately first"
fi
STAGE=$(mktemp -d); trap 'rm -rf "$STAGE"' EXIT
S=$STAGE/$NAME
mkdir -p "$S/docs" "$S/tools"

copy() { [ -f "$ROOT/$1" ] || die "missing $1"; mkdir -p "$S/$(dirname "$2")"; cp "$ROOT/$1" "$S/$2"; }

cp "$ROOT/VERSION" "$S/VERSION"
copy CHANGELOG.md CHANGELOG.md
copy README.md README.md

# Docs subset. LESSONS.md is left out until its Bluetooth address is replaced by a placeholder; the scan enforces this.
for d in STATUS QUICKSTART ARCHITECTURE flash-safety RECOVERY HARDWARE_MAP UI_CONFIG HA_LINK HA_DASHBOARD GPS_AIDING SCREEN; do
	copy "docs/$d.md" "docs/$d.md"
done
[ ! -f "$ROOT/docs/RELEASE_NOTES_$VERSION.md" ] || copy "docs/RELEASE_NOTES_$VERSION.md" "RELEASE_NOTES.md"

# Mac-side tools: text scripts only. The NOR bootloader flasher is never shipped.
SKIP_TOOLS=' homtom-flash-bootloader '
for f in "$ROOT"/tools/homtom-*; do
	[ -f "$f" ] || continue
	b=$(basename "$f")
	case "$SKIP_TOOLS" in *" $b "*) continue ;; esac
	[ "$(head -c 4 "$f" | od -An -c | tr -d ' ')" != '177ELF' ] || continue
	cp -p "$f" "$S/tools/$b"
done

cat > "$S/LICENSE.NOTE.txt" <<'EOF'
No licence has been chosen for this pre-release. Until a LICENSE file is added by the project owner, all rights are
reserved for homtom's own code and artwork. Linux kernel patches and board code derived from the kernel are GPL-2.0.
The Poppins font is SIL OFL 1.1. TomTom is a trademark of its owner; this project is not affiliated with or endorsed
by TomTom and contains no TomTom software. Do not redistribute vendor firmware or backups of any individual unit.
EOF

cat > "$S/INSTALL.md" <<EOF
# Install homtom $VERSION (alpha, development only)

This can brick the unit. It replaces the \`ttsystem\` image that the bootloader loads; it never touches NOR flash.
Nobody has tested it on another unit. Read \`docs/flash-safety.md\` first. Never write NOR, never write INFORM0=3, never
run raw disk commands, never use \`reboot -f\`.

Requirements: a TomTom GO 630 with stock software or an earlier homtom image, a USB cable, a Mac that stays awake
and unlocked (run \`caffeinate -dimsu\` in a spare terminal). Storage mode does not appear with a locked screen.

## 1. Verify the image

    cd $NAME
    shasum -a 256 -c ttsystem.sha256

## 2. Install through storage mode (validated path)

1. Connect the unit. If it already runs a homtom image: \`tools/homtom-to-storage\` (retries once). On stock software
   or a unit that is off, reboot it with the cable attached; the bootloader offers INTERNAL (and NO NAME) to the Mac.
2. \`tools/homtom-stage ./ttsystem\`. It copies the current \`ttsystem\` to \`ttsystem.prev\` (only when the running one
   was marked good, or no \`.prev\` exists yet), copies the new image to INTERNAL and verifies it with \`cmp\`. The first
   copy onto a freshly mounted volume sometimes fails with "Permission denied"; run it again.
3. \`tools/homtom-to-linux\` ejects INTERNAL and waits for the unit to answer. Check with \`tools/homtom-state\`.
4. After you have verified the boot: \`tools/homtom-mark-good\`. This is what protects \`ttsystem.prev\` from being
   overwritten by the next staging.
5. On the unit, \`cat /etc/homtom-release\` shows the version, build date, git state and kernel version
   (\`tools/homtom-sh 'cat /etc/homtom-release' 10\`).

Keep \`ttsystem.lab2613\` on INTERNAL if it is there: several tools use it to recognise the right card.

## 3. If the new image does not come up

Validated for images whose kernel starts (the board file sets INFORM0=5 early, the watchdog then resets a wedged unit
into storage mode):

    tools/homtom-recover        # waits for storage mode, restores ttsystem.prev, verifies with cmp, ejects

UNTESTED: a successful end-to-end run of \`homtom-recover\` has not been recorded. KNOWN GAP: an image that hangs before
the kernel writes INFORM0 leaves the unit powered down (observed 2026-11-07); it then needs the power key, and after
that storage mode is available again. An image that never starts a kernel is the bootloader's business.
You can also do the same by hand in storage mode: copy \`ttsystem.prev\` over \`ttsystem\` on INTERNAL, compare, eject.

## 4. Going back to the stock software

UNTESTED, no validated procedure exists. The stock \`ttsystem\` is TomTom's and is not shipped here. If you made your own
copy of it before installing, its restore is the same single file copy as in step 2 (storage mode, copy over
\`ttsystem\`, \`cmp\`, eject). Without that copy there is no way back from this project alone. Make and keep a copy of the
original INTERNAL volume before you begin; the bundle cannot do it for you, and backups hold the unit's identity, so do
not publish them.

## Contents

\`ttsystem\`, \`ttsystem.sha256\`, \`VERSION\`, \`CHANGELOG.md\`, \`RELEASE_NOTES.md\`, \`tools/\` (Mac-side scripts), \`docs/\`,
\`LICENSE.NOTE.txt\`. Known limitations are listed in \`CHANGELOG.md\`.
EOF

if [ -n "$IMAGE" ]; then
	cp "$IMAGE" "$S/ttsystem"
fi

# Refusals: local state and secret-looking file names must not be in the bundle at all.
bad=$(cd "$S" && find . \( -name .runtime -o -name .env -o -name bt.pin -o -name btkeys -o -name ha_webhook -o -name '*.pem' -o -name '*.key' -o -name ttsystem.lab2613 -o -name ttsystem.prev -o -name '*.nordump' \) -print)
[ -z "$bad" ] || die "forbidden paths in bundle: $bad"
# The image is opaque to the scanner; grep its raw bytes for the obvious identity strings anyway.
if [ -f "$S/ttsystem" ] && LC_ALL=C grep -aEq '/Users/[A-Za-z0-9._-]+|[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+\.[A-Za-z]{2,}|/api/webhook/' "$S/ttsystem"; then
	die "image contains a home path, email address or webhook path in clear text; rebuild from a clean environment"
fi

echo "scanning bundle with scripts/public-scan.sh ..."
if ! "$ROOT/scripts/public-scan.sh" "$S"; then
	die "scan reported findings; bundle not written (fix the source, or remove the file from the bundle list)"
fi

# The checksum file is written after the scan: the scanner reads a bare hash as a key.
[ ! -f "$S/ttsystem" ] || (cd "$S" && shasum -a 256 ttsystem > ttsystem.sha256)

echo "bundle $NAME contents:"
(cd "$S" && find . -type f | sort | sed 's|^\./|  |')

if [ "$DRY" = 1 ]; then
	echo "dry run: nothing written under release/"
	exit 0
fi

mv "$S" "$B"
echo "wrote $B"
if [ "$TAR" = 1 ]; then
	(cd "$WORK" && tar -czf "$NAME.tar.gz" "$NAME" && shasum -a 256 "$NAME.tar.gz" > "$NAME.tar.gz.sha256")
	echo "wrote $WORK/$NAME.tar.gz (+ .sha256)"
fi
