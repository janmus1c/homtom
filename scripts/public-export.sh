#!/bin/bash
# Export the CURRENT tracked tree (not history) as a fresh single-commit repo for public release.
# Usage: PUBLIC_NAME='Name' PUBLIC_EMAIL='address' scripts/public-export.sh OUTDIR
# OUTDIR must not exist or must be empty. Nothing in the source repo is modified.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-}
[ -n "$OUT" ] || { echo "usage: PUBLIC_NAME=... PUBLIC_EMAIL=... $0 OUTDIR" >&2; exit 2; }
: "${PUBLIC_NAME:?PUBLIC_NAME is required (neutral author name, no default)}"
: "${PUBLIC_EMAIL:?PUBLIC_EMAIL is required (neutral author email, no default)}"
MAXBYTES=${MAX_BYTES:-1048576}

if [ -e "$OUT" ] && [ -n "$(ls -A "$OUT" 2>/dev/null)" ]; then
	echo "refusing: $OUT exists and is not empty" >&2
	exit 2
fi
case "$(CDPATH= cd -- "$OUT" 2>/dev/null && pwd || echo "$OUT")" in
	"$ROOT"|"$ROOT"/*) echo "refusing: OUTDIR must be outside the source repo" >&2; exit 2 ;;
esac

# Compiled ELF/Mach-O files are also dropped (set KEEP_BINARIES=1 to keep them); files over MAX_BYTES (default 1 MiB) too.
# Extended regexes matched against the repo-relative path. Keep in sync with the export policy.
EXCLUDE='
^\.runtime/
^\.pi/
^AGENTS\.md$
^HANDOVER.*\.md$
^STOP$
^PAUSE$
^node_modules/
^package(-lock)?\.json$
^biome\.json$
^artifacts/
^experiments/
^build/
^research/
^config/
^device/
^bridge/
^supervisor/
^workspace/bootgfx/
^workspace/gui/out/
^docs/images/(?!splash\.png)
(^|/)ttsystem\.lab[^/]*$
(^|/)ttsystem\.prev$
(^|/)(nor|NOR)[^/]*\.(bin|img|dump)$
\.(nordump|norimg|rom)$
(^|/)(stock|original)[^/]*\.(bmp|bin|img)$
^scripts/(benchmark-model|check-worker|eval-learn-alignment|install-device-connectivity|postman-check|prepare-gadget-build|record-host-usb|stage-gadget-trial|start-overnight)\.py$
^scripts/(doctor|host-discovery|report|run-discovery|run-overnight|status|stop|test)\.sh$
^scripts/lab$
^tests/(fake_pi|test_lab|fixtures/)
^docs/(BRIEF|EXPERIMENT_PROTOCOL|GATEWAY_API|LAB_HARNESS|LOCAL_MODEL_POLICY|NIGHT_LOG|OWNER_TESTS|QUERY_LIMITS|VALIDATION|DEVICE_STATUS|BOOTGFX_HISTORY|bootloader-graphics)\.md$
^firmware/(tools|overlay/usr/bin)/(norflash|rawcopy)$
'
LIST=$(mktemp)
SKIPPED=$(mktemp)
EXFILE=$(mktemp); trap 'rm -f "$LIST" "$SKIPPED" "$EXFILE"' EXIT
printf '%s\n' "$EXCLUDE" > "$EXFILE"

git ls-files -z | EXCLUDE_FILE="$EXFILE" MAXB="$MAXBYTES" perl -0ne '
	BEGIN { local $/ = "\n"; open(my $f, "<", $ENV{EXCLUDE_FILE}) or die; @re = grep { length } map { chomp; $_ } <$f>; }
	chomp;
	my $p = $_; my $why = "";
	for my $r (@re) { if ($p =~ /$r/) { $why = "pattern $r"; last } }
	if ($why eq "" && -f $p && -s $p > $ENV{MAXB}) { $why = "size " . (-s $p) . " > $ENV{MAXB}" }
	if ($why eq "" && !-f $p) { $why = "not a regular file" }
	if ($why eq "" && !$ENV{KEEP_BINARIES} && open(my $b, "<:raw", $p)) { read($b, my $m, 4); close $b; $why = "compiled executable (publish as release asset)" if $m =~ /^(\x7fELF|\xcf\xfa\xed\xfe|\xce\xfa\xed\xfe|\xca\xfe\xba\xbe)/ }
	if ($why ne "") { print STDERR "$p\t$why\n" } else { print "$p\0" }
' >"$LIST" 2>"$SKIPPED"

mkdir -p "$OUT"
OUT=$(CDPATH= cd -- "$OUT" && pwd)
n=0
while IFS= read -r -d '' p; do
	mkdir -p "$OUT/$(dirname "$p")"
	cp -p "$p" "$OUT/$p"
	n=$((n + 1))
done < "$LIST"

(
	cd "$OUT"
	git init -q -b main .
	git config user.name "$PUBLIC_NAME"
	git config user.email "$PUBLIC_EMAIL"
	git config commit.gpgsign false
	[ -f LICENSE ] || echo "note: no LICENSE file; choose one before publishing (not added automatically)" >&2
	git add -A
	GIT_AUTHOR_NAME=$PUBLIC_NAME GIT_AUTHOR_EMAIL=$PUBLIC_EMAIL \
	GIT_COMMITTER_NAME=$PUBLIC_NAME GIT_COMMITTER_EMAIL=$PUBLIC_EMAIL \
		git commit -q -m "Initial public import"
)
echo "exported $n files to $OUT ($(wc -l < "$SKIPPED" | tr -d ' ') excluded)"
echo "excluded paths and reasons:"
cut -c1-110 "$SKIPPED" | sed "s/^/  /"
echo "next: scripts/public-scan.sh $OUT"
