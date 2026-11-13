# Quickstart: first day with a GO 630

You need a TomTom GO 630 that currently runs the stock software or an earlier homtom image, a USB cable, a Mac and Docker. Read the Safety section of `README.md` first. Never write NOR flash, never write `INFORM0=3`, never run raw disk commands against the unit.

1. **Look at the docs that set expectations.** Read `docs/STATUS.md`, `docs/ARCHITECTURE.md` and `docs/flash-safety.md`. Note what is marked partial in the README status table.
2. **Prepare the Mac.** Install Docker. Run `caffeinate -dimsu` in a spare terminal whenever you stage an image; storage mode needs the Mac awake and unlocked.
3. **Check state, read-only.** Connect the unit and run `tools/homtom-state`. It prints one line: LINUX, STORAGE or not reachable. The USB link is `192.168.1.10`.
4. **Get a shell.** With the unit in LINUX, run `tools/homtom-sh 'uname -a' 10`. Read `docs/LESSONS.md` for the quirks (stderr is not returned, so add `2>&1`).
5. **Build the pieces.** `kernel/scripts/docker-build.sh`, then `kernel/scripts/build-modules-tree.sh TREE OUTDIR` against the same kernel tree, then `tools/devtools/build.sh` for the device tools.
6. **Wrap the image.** `MODDIR=OUTDIR ZIMAGE=... OUT=... kernel/scripts/build-ttsystem-6.1.sh`. It refuses mismatched modules; do not work around that.
7. **Enter storage mode.** `tools/homtom-to-storage`. If the first request is lost the tool retries once. INTERNAL then appears on the Mac.
8. **Stage and boot.** `tools/homtom-stage PATH/TO/ttsystem` (keeps the old image as `ttsystem.prev`), then `tools/homtom-to-linux`. If the unit does not come back, run `tools/homtom-recover` and wait for it to restore `ttsystem.prev`.
9. **Check the hardware.** Test on the unit: touch calibration, backlight, GUI, sound, Bluetooth pairing window. Report what you saw; sound audibility and pairing are unverified.
10. **Connect Home Assistant, optionally.** Read `docs/HA_LINK.md` and `docs/HA_DASHBOARD.md`. Create the helpers and a webhook automation on your side, keep the webhook id in an untracked file, set `HA_URL` in the environment, then run `tools/homtom-ha-bridge.py 30`. Do not commit the webhook id, PINs or addresses.

When something goes wrong, record it in `docs/LESSONS.md` as symptom, cause and fix.
