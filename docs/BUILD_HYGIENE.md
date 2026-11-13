# Build and staging hygiene

Status labels: **verified** = run on this Mac in the session that wrote this page; **proposed** = analysed or sketched, not adopted. Nothing here touched the device. The full build (`kernel/scripts/build-ttsystem-6.1.sh`) was not run, only analysed.

## 1. Staging: `tools/homtom-stage` (verified)

Behaviour now:

1. Reads the source, computes size and sha256.
2. Copies to `INTERNAL:/ttsystem.new` (same volume), `sync`, compares size and sha256 of the staged file with the source. Up to 4 attempts with 1, 2, 3 s sleeps (`HOMTOM_STAGE_TRIES` overrides). The readback hashes through python3 with `F_NOCACHE` where available, else `shasum`; this is best effort, macOS may still answer from its page cache, so a clean mount after ejecting is the only independent proof.
3. Only after a verified copy: the `ttsystem.prev` rule is unchanged (replace it only when `homtom/current.good` exists or no prev exists, else print the "never marked good" note); the new prev is written to `ttsystem.prev.new`, verified, renamed.
4. Removes `homtom/current.good`, renames `ttsystem.new` to `ttsystem`, `sync`.
5. Prints `staged <16-char sha256> <bytes> bytes (attempt N)`.

Exit codes: 0 ok, 1 not in storage mode, 2 bad usage/unreadable source, 3 copy or verify failed (ttsystem untouched), 4 prev could not be written (ttsystem untouched), 5 rename failed.

Tests run against a scratch directory via `HOMTOM_VOLUME`: first stage, stage with and without the good marker (prev kept or replaced as before), a cp that fails twice then works (succeeds on attempt 3), a cp that always fails (rc 3, ttsystem byte-identical afterwards), a cp that truncates (caught by the size check, rc 3). Not tested: a real FAT volume.

Gaps left alone, same pattern, worth fixing the same way:
- `tools/homtom-recover` copies `ttsystem.prev` over `ttsystem` in place; an interrupted copy leaves no bootable image. Use the temp-name, verify, rename sequence there too.
- `tools/homtom-stage-live` copies to `ttsystem` in place on the device (it does verify afterwards over FTP).

## 2. Userland lint: `scripts/lint-userland.sh` (verified)

Checks every `#!` script in `userland/` plus the `init.sh` text embedded in the build script (extracted between the `cat > "$ROOTFS/init.sh"` heredoc markers) against the stock BusyBox 1.22.1. Run `scripts/lint-userland.sh [--strict] [file ...]`; it prints `file:line: ERROR|CHECK [kind] text`, exits 1 on ERROR (or CHECK with `--strict`). `# lint-ok` on a line waives a reviewed finding.

What it flags:
- command words that are not in `kernel/scripts/busybox-applets.txt`, not an ash builtin, not a function defined in the same file, not a shipped tool (`userland/*`, `tools/devtools/*` binaries, busybox). Understands `sv`, `exec`, `command`, `nice`, `env`, `nohup` and `tcpsvd HOST PORT cmd` wrappers, case patterns, heredoc bodies, quoted text, `$(...)`, backticks, `$(( ))`.
- `grep -E/-P`, `egrep`, fractional `sleep`, bash-only commands (`source`, `declare`, `let`, `pushd`, ...), arrays, `<( )`, `<<<`, `&>`, `$'..'`, brace expansion, `(( ))`, `trap ERR`.
- CHECK (cannot be verified from the repo): `[[ ]]` (runs as the `test` applet), `==` in tests, `${var//x/y}` and `${var:o:l}`, `read -t/-n/-p`, `date -d`, `sed -i`, `grep -r`, `$RANDOM`.
- `sh -n` per file. This is the Mac's `sh`, not ash, so it only catches plain syntax errors.

Verified facts about the device shell: `strings` of the stock binary (`.runtime/emu/initramfs/bin/busybox`, "BusyBox v1.22.1 (2023-03-08)") contains "arithmetic syntax error" and "bad number", so `$(( ))` arithmetic is compiled in; `local` is present as a builtin. Whether `ASH_BASH_COMPAT` (`${var//}`, `${var:o:l}`) is compiled in is not visible in the repo (no BusyBox `.config` is stored); those stay CHECK. The stock BusyBox applet list has no `printf`, `tr`, `tail`, `dd`, `cmp`, `md5sum`, `timeout`, `seq`, `tee`, `mkfifo`; the lint reports all of them via the applet list rather than a hard-coded list.

Result on the current tree: 18 files (17 scripts + init.sh), 0 ERROR, 0 CHECK, 0 syntax. A deliberately bad test file (tr, tail, printf, dd, cmp, md5sum, timeout, seq, array, `[[ ]]`, `grep -E`, `sleep 0.5`, `${x//a/b}`) produced all expected findings, with no false positives from comments or heredoc text. Limits: it is a tokenizer heuristic, not a shell parser; unusual constructs (eval'd strings, commands built from variables) are not seen.

Proposed: run it before every build (it takes well under a second).

## 3. Reproducibility

### tools/ttimage (verified)

Re-packing the two sections of `artifacts/kernel-builds/homtom-6.1-trimA/ttsystem.ladder-full` (extracted with `firmware/ttimage.py`: initramfs section at 0x31000000, 1213519 bytes; zImage section at 0x31700000, 3100136 bytes) with `tools/ttimage/mkttimage` gives a byte-identical file, sha256 `d6df7349a1b1f8de2b9fa4c3e48b6a9bbd2961032df0bf24792f42643227b7c2` (matches the recorded `.sha256`). `mkttimage` reads two files and writes magic, lengths, addresses, MD5+Blowfish signatures and the fixed trailer; it has no timestamps or randomness. The packer is deterministic.

### build-ttsystem-6.1.sh (analysed, not run: NOT deterministic as written)

Evidence from two existing images (`ladder-ui`, `ladder-full`): gzip header mtime is 0 (`gzip -n` works), but cpio inode numbers differ between builds (e.g. first entry 1151834 vs 6361212, the rest in the 6.3 to 6.36 million range) and entry mtimes are the build second (1794059773..75 vs 1794061929..31). Sources of variation, in order of effect:

1. `find . | cpio -o -H newc` stores real inode numbers of the container's overlay filesystem and the directory order of `find`, both filesystem and run dependent.
2. cpio entry mtimes: everything is created during the build (`cp` without `-p`, `ln`, `mknod`, `cat >`), so every mtime is "now".
3. `/etc/homtom-release` `built=` uses `SOURCE_DATE_EPOCH` or the current time, and `git=` appends `-dirty` when the tree has uncommitted changes (it currently always does). The two images above predate this file, so it is not yet in an image; once it is, every build differs here too.
4. `apt-get install cpio gzip` in `arm32v5/debian:bookworm-slim` is unpinned and the image tag floats: the cpio and gzip/zlib versions can change the bytes.
5. Inputs: `userland/homtom-*` and `tools/devtools/*` binaries come from the working tree; they are only reproducible if those binaries themselves are rebuilt reproducibly (not examined).

Proposed changes (exact):

a. In the build script, set `EPOCH=${SOURCE_DATE_EPOCH:-$(git -C "$ROOT" log -1 --format=%ct 2>/dev/null || date +%s)}` so the default is the last commit time, and pass `-e SOURCE_DATE_EPOCH=$EPOCH` to the container.
b. Replace `find . | cpio -o -H newc 2>/dev/null | gzip -n -9` by a normalised pack, either with GNU cpio >= 2.12:
   `find . -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} + ; find . | LC_ALL=C sort | cpio -o -H newc --reproducible -R 0:0 2>/dev/null | gzip -n -9`
   (`--reproducible` normalises inode numbers and device numbers; check `cpio --version` in the container first), or on the Mac with the repo's own writer, which already numbers inodes sequentially: `ttimage.write_cpio(sorted(entries, key=name))` with every `mtime` set to the epoch, then `gzip.compress(..., compresslevel=9, mtime=0)`. The second form was tested here: taking the entries of `ladder-full`, shuffling them and perturbing every mtime, then normalising, gave identical gzip output both times. It does not preserve hardlinks (none are used).
c. Make the dirty marker explicit but optional: keep `-dirty` for development builds, and for a release build refuse to build when the tree is dirty (the release process already needs a clean tree).
d. Pin the container: `arm32v5/debian@sha256:<digest>` and `apt-get install cpio=<ver> gzip=<ver>`, or drop apt entirely by using the Python writer (b, second form), which removes the container's cpio/gzip from the equation and needs no network.
e. Acceptance test to add after adopting: build twice into two output directories, `cmp` the two `ttsystem` files; fail the build script on difference.

Until then, compare images by section content, not by whole-file hash, and treat the manifest (below) as the record of what was actually staged.

## 4. Hash manifest: `scripts/manifest-add.sh` (verified, adoption proposed)

`scripts/manifest-add.sh FILE...` appends `<name> <size> <sha256> <UTC date>` to `artifacts/kernel-builds/MANIFEST.sha256` (name relative to `artifacts/kernel-builds`; a repeated name+sha256 is not added twice). `scripts/manifest-add.sh --verify` recomputes every listed file that still exists and exits 1 on a mismatch. Tested against a scratch tree: add, duplicate, missing file (rc 1), verify ok, verify mismatch after modification.

Proposed use: after every `tools/homtom-stage` that prints `staged <hash16> ...`, run `scripts/manifest-add.sh <the image you staged>`; the line's sha256 must start with the printed 16 characters. `artifacts/` is git-ignored, so the manifest is local evidence; copy relevant lines into the experiment notes or commit message when an image is booted or marked good. Optional follow-up: have `homtom-stage` call it (not done, to keep staging free of repo coupling).

## What to adopt

1. `tools/homtom-stage` (done, ready to use) and apply the same temp-rename-verify pattern to `homtom-recover`.
2. `scripts/lint-userland.sh` before each build; fix any ERROR, review CHECKs.
3. `scripts/manifest-add.sh` after each stage.
4. Build determinism changes a, b (Python writer form recommended), d, e in `build-ttsystem-6.1.sh` when no build is in flight; none were applied to that file.
