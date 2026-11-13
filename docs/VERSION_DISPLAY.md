# Showing the version on the device (proposal)

Status: proposal only. No source of `homtom-statusd` or the GUI was changed.

## Source of truth

`kernel/scripts/build-ttsystem-6.1.sh` writes `/etc/homtom-release` into the initramfs, four `key=value` lines:

    version=0.1.0-alpha.1
    built=2026-11-07T12:00:00Z
    git=abc1234            (suffix -dirty when the tree had uncommitted changes)
    kernel=6.1.191

Set `SOURCE_DATE_EPOCH` for a reproducible `built=` value. The file is a few dozen bytes and read-only in practice; nothing else in the image parses it yet.

## Status daemon

`workspace/statusd/homtom-statusd.c` (and `statusd_core.h`) already builds a JSON object per request. Proposal:

1. Read `/etc/homtom-release` once at start (the file never changes while running), copy the four values into static buffers, truncating each to 31 bytes.
2. Emit one nested object at the end of the JSON, so existing consumers that ignore unknown keys keep working:
   `"release":{"version":"0.1.0-alpha.1","built":"2026-11-07T12:00:00Z","git":"abc1234","kernel":"6.1.191"}`
   A missing file yields `"release":null` rather than an error, like the other unknown values (`null`, never 0).
3. Add a case to `test_core.c` for the JSON writer with and without the file, and keep the existing field order test stable by appending.
4. The Mac-side bridge (`tools/homtom-ha-bridge.py`) can post `version` as an attribute or a text helper (`input_text`) next to the other helpers; it should not post `git` or `built` if the user treats them as noise.
5. Bluetooth `status` returns the same JSON, so the version is visible over RFCOMM for free.

## GUI

`workspace/gui/` (`gui.c`, `cfg.c`, config in `INTERNAL:/homtom/ui.conf`, `docs/UI_CONFIG.md`) has a System page.

1. Add a read-only text item "Version" on the System page, fed by the `release.version` value (the GUI already reads statusd values from its input file; one more key `release_version` and `release_git`).
2. Render it small at the bottom: `homtom 0.1.0-alpha.1 (abc1234)`. Keep it a config option (`show_version=1` default) so a user can hide it.
3. Add the new text to the host preview and update the golden screenshots in `workspace/gui/tests/golden.sha256` in the same change; the e2e tests in `docs/E2E.md` can assert it by screenshot.
4. The boot text console can print `homtom-init: release <version> <git>` once from `init.sh` (one line to `/dev/kmsg`, via the existing `k` helper) so the version also lands in `homtom-boot.log` on INTERNAL. This needs a one-line change to the init heredoc in the build script, not to statusd or the GUI.

## Order

First the build-script change (done), then the init log line (smallest, helps support), then statusd, then the GUI. Each is its own image and goes through `homtom-stage` with the usual fallback.
