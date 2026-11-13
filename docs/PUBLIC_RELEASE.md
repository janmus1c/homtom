# Public release checklist

How to publish homtom as a NEW public repository with a clean history. The private repo stays as it is; nothing here pushes, commits or creates a repository for you.

Tooling:

| Script | Purpose |
|---|---|
| `scripts/public-export.sh OUTDIR` | Export the current tracked tree (no history) as a one-commit repo. Needs `PUBLIC_NAME` and `PUBLIC_EMAIL` in the environment; there are no defaults. Prints every excluded path with its reason. |
| `scripts/public-scan.sh OUTDIR` | Grep the export for personal data and secrets. Prints `file:line: [kind] match`, exits 1 on findings. `PUBLIC_EMAIL` marks the neutral address as allowed; `PUBLIC_SCAN_EXTRA` takes an extra regex (put owner-specific hostnames and names there, never in the repo). |

```sh
PUBLIC_NAME='homtom contributors' PUBLIC_EMAIL='noreply@example.invalid' \
  scripts/public-export.sh /tmp/homtom-public
PUBLIC_EMAIL='noreply@example.invalid' \
  PUBLIC_SCAN_EXTRA='your-private-hostname|other-private-term' \
  scripts/public-scan.sh /tmp/homtom-public
```

Run both again immediately before the first push. Use a neutral identity you are happy to see forever; the owner's real address must not appear.

## 1. What the export excludes, and why

`artifacts/` tracks only `.gitkeep`, so no build outputs or backups leave the repo. `workspace/bootgfx/`, `.runtime/`, `ttsystem.lab*`, NOR dumps and stock splash BMPs are not tracked today; the patterns stay in the script as a safety net in case they get force-added later.

| Excluded | Decision and reason |
|---|---|
| `AGENTS.md`, `HANDOVER.md`, `HANDOVER_PROMPT.md` | Read in full. They hold lab specifics: local model name and oMLX rules, owner grants of device access, session-resume snapshots with the owner's interface and DHCP details, and instructions addressed to other agents. Not useful to outsiders. A public `CONTRIBUTING.md` replaces them. |
| `.pi/`, `supervisor/`, `bridge/`, `config/`, `device/`, `research/`, `package*.json`, `biome.json` | Local research lab harness (Pi extensions, supervisor, bridge, model prompts, evidence notes). Nothing in the kernel, userland, panel or emulator depends on it. |
| `scripts/` lab scripts (doctor, discovery, overnight, status, stop, report, test, lab, model benchmarks, gadget staging helpers) | Lab harness only. Kept: `tt.py`, `build-homtom-nor.py`, `homtom-on-storage.sh`, the two `public-*.sh` scripts. Review whether `build-homtom-nor.py` should ship at all (NOR writes are discouraged; see `docs/flash-safety.md`). |
| `tests/fake_pi.py`, `tests/test_lab.py`, `tests/fixtures/` | Test the lab harness. The other tests stay. |
| `docs/BRIEF`, `EXPERIMENT_PROTOCOL`, `GATEWAY_API`, `LAB_HARNESS`, `LOCAL_MODEL_POLICY`, `NIGHT_LOG`, `OWNER_TESTS`, `QUERY_LIMITS`, `VALIDATION`, `DEVICE_STATUS`, `BOOTGFX_HISTORY`, `bootloader-graphics` | Lab process, overnight logs, owner test sheets, and bootloader-graphics notes derived from bootloader disassembly. Re-read `BOOTGFX_HISTORY` and `bootloader-graphics` before deciding to restore either: disassembly-derived text is vendor-derived. |
| Compiled executables (ELF/Mach-O: `tools/devtools/*`, `tools/ttimage/mkttimage`, `ttimgextract`, `firmware/**/norflash`, `rawcopy`) | Generated; publish as release assets with checksums, build from source otherwise. Set `KEEP_BINARIES=1` to override. |
| Anything over 1 MiB (`MAX_BYTES` to change) | Generated or vendor-sized. Nothing tracked hits this today. |
| `workspace/gui/out/*.png` | Generated screenshots, 3 identical-size files; regenerate for docs if wanted. |
| `.runtime/`, `artifacts/`, `experiments/`, `build/`, `STOP`, `PAUSE` | Local state. |
| Stock splash BMPs, `ttsystem.lab*`, `ttsystem.prev`, NOR dumps and `.rom` files, `workspace/bootgfx/` | Copyrighted TomTom material or derived from it. Never public. |

Kept on purpose: `kernel/`, `userland/`, `panel/`, `emulator/`, `gateway/`, `firmware/` (sources), `tools/`, `brand/` (including generated homtom splash `.fb` files, which are our own artwork), `build-env/`, `workspace/` sources that are tracked, `docs/` not listed above, `README.md`, `.gitignore`.

Things to check by hand in the export:

- `.gitignore` contains `workspace/*` and `artifacts/*`; the tracked `workspace/` sources were force-added. Anyone adding files there in the public repo will be confused. Move the useful `workspace/` sources into proper directories or change the ignore rules.
- `docs/` still links to excluded files. Counted in the current export: `README.md` (LAB_HARNESS), `docs/ROADMAP.md` (NIGHT_LOG), `docs/LESSONS.md` (OWNER_TESTS, BOOTGFX_HISTORY), `docs/HARDWARE.md`, `docs/BOARD_CAPABILITIES.md`, `docs/HOMTOM_OS.md` (BRIEF, HANDOVER), `docs/emulator-plan.md` (HANDOVER, DEVICE_STATUS, bootloader-graphics), `docs/TRANSPORT.md` (config/), `firmware/norimage.py` (bootloader-graphics). Fix or drop each.
- About 24 files mention `.runtime/` and 26 mention `artifacts/`. These are local build conventions and are fine if the build instructions explain them.

## 2. Scrub list from the first scan (current tree, 2026-11-07)

The scan reported 7 findings in the export. Triage:

| File:line | Kind | Verdict |
|---|---|---|
| `docs/LESSONS.md:70` | `BD_ADDR 00:02:5B:00:A5:A5` | The `00:02:5B` prefix is the CSR vendor range and `A5A5` looks like a factory default, so it is probably not a per-unit address. Confirm it is not this unit's address; if it is, replace with `00:02:5B:xx:xx:xx`. Reported twice (mac and bd_addr rules). |
| `tools/ttimage/ttimgextract.c:3` | email in a copyright header | Upstream author attribution (GPL, 2004). Keep. Add it to the allow list or ignore. |
| `workspace/btd/README.md:74` | PIN `0000` | Documented macOS default PIN, not a secret. Keep; consider telling users to set their own `bt.pin`. Reported twice. |
| `tests/test_gateway.py:21`, `:402` | token-like strings | Test fixtures (`test-token-not-real`). Keep. |

Already clean in the export: no home paths, no owner username, no IPv4 outside `192.168.1.x`/`169.254.x.x` (netmasks `255.x.x.x` are allowed), no webhook ids, no Home Assistant hostnames, no tokens, no `bt.pin`, `btkeys` or `ha_webhook` files. `tools/homtom-ha-bridge.py` reads the webhook id from `.runtime/ha_webhook` and the URL from `HA_URL`; it holds no secret itself.

The scanner only sees the exported files. These are outside its reach, so check them by hand:

- Git history of the private repo (all commits carry a personal author email; early commits had home paths). This is why the public repo starts from a fresh single commit and the private history is never pushed.
- Images: `docs/images/splash.png`, `brand/**/*.png`, `*.fb`. Open them; a photo or screenshot may show a name, network or window title.
- `docs/` prose that identifies the owner's home network layout, phone or laptop models paired during tests, GPS coordinates (search for decimal lat/lon), and timestamps that reveal location. The scan cannot catch these.
- Refine the scan with `PUBLIC_SCAN_EXTRA` for the owner's real hostnames, Home Assistant entity names tied to a home, family names, and employer names.

## 3. Scrub steps

1. Fix every finding above in the private repo (or accept it with a note), commit there.
2. Re-run export and scan into a fresh directory; the scan must exit 0 or every remaining finding must be a conscious keep from the table above.
3. Read the export's `git log` (one commit, neutral identity) and `git show --stat` for stray files.
4. Grep the export for `AI`, `Claude`, `Qwen`, `oMLX`, `Pi` if the owner wants no mention of the lab tooling. Qwen, oMLX and Pi do not occur in the current export; check the others yourself.
5. Copy the export to its own machine path and run the unit tests there (section 7) to catch files the exclusions removed that something still needs.

## 4. History policy

- Publish a single squashed commit authored by the neutral identity. Never push, mirror or fork the private repo; its history contains personal data and the lab harness.
- Do not carry over tags, branches or commit messages. Write a new first commit message and, going forward, short imperative subjects.
- Keep the private repo as the working copy. Decide whether new work happens in the public repo (simplest, then archive the private one) or whether the export is re-run per release (each release then loses history; prefer working in public after the first import).
- If anything sensitive slips out later, rewriting public history is not enough: treat the data as leaked and rotate it (webhook id, pairing PIN, link keys).

## 5. README and branding check

Done by reading `README.md` against `docs/STATUS.md`, `docs/LESSONS.md` and recent commits. The README must say only what artifacts back up. Mismatches found today:

| README claim | Source of truth | Action |
|---|---|---|
| Touch: "Sampler written, tested idle only" | `docs/STATUS.md` says "no response to presses (unresolved)"; commit "Touch calibrated and verified" is newer; `docs/LESSONS.md` is the evidence | The three disagree. Reconcile STATUS and README with the latest LESSONS entry and cite the artifact (calibration file or log) or soften the claim. |
| LCD: "backlight PWM untested" | `docs/LESSONS.md:85` records owner-observed backlight steps (user-reported, not lab-measured) | Update to "works, owner-observed". Keep the user-reported versus measured distinction. |
| Bluetooth: "kernel `hci0 up` fails; discovery from a phone untested" | `docs/LESSONS.md:81` says kernel `hci0` is UP RUNNING on the current image; STATUS says visible on iPhone and MacBook (owner-confirmed); pairing and data channel not done | Update. Do not claim pairing or a data channel. |
| Audio: "not started" | LESSONS records a working tone tool after fixes (`homtom-snd2`) | Update to partial; list which parts are verified. |
| Table row "Linux 6.1 boots on the unit: Working" | Needs a device log artifact (`docs/LESSONS.md` boot entries) | Link the log excerpt; do not rely on the private `artifacts/`. |
| "Percentages in `docs/STATUS.md` are estimates" | Correct. | Keep. |
| Repo map | Lists `bridge/`, `supervisor/`, `workspace/` as "local research lab harness" | Remove bridge and supervisor rows. Add `panel/`, `emulator/`, `gateway/`, `firmware/`. Fix the `docs/LAB_HARNESS.md` link. |
| Architecture box mentions `homtom-ts` | Tools now include `homtom-touch`, `homtom-btd`, `homtom-statusd` | Refresh names. |
| "Cleanup of debug hooks: Not yet" | `docs/STATUS.md` phase 9 | Keep until true. |
| Licence section: "licence TBD" | Section 6 | Replace once chosen. |

Images: `brand/homtom-icon.svg` and `docs/images/splash.png` both exist in the export. Confirm they render on GitHub and that no stock TomTom artwork is visible anywhere in `docs/images/` or `brand/`. The brand name contains "TomTom" only descriptively; add a trademark line: "TomTom is a trademark of its owner; this project is not affiliated with or endorsed by TomTom."

Also strike or label owner-specific narrative ("Needs the owner" list in `docs/STATUS.md`, item 4 about going public) before publishing.

## 6. Licence

Do not add a licence until the owner chooses. Options:

| Part | Options | Notes |
|---|---|---|
| Kernel patches, board file, kernel modules (`kernel/`) | GPL-2.0 (required for derived kernel code; files already carry `SPDX-License-Identifier: GPL-2.0`) | Ship `LICENSES/GPL-2.0.txt` or a root `COPYING`. |
| Own code (`userland/`, `tools/`, `panel/`, `gateway/`, `emulator/`, scripts) | MIT or Apache-2.0 | Apache-2.0 adds a patent grant; MIT is shorter. Code linked into GPL kernel modules stays GPL. |
| Brand (mark, icon, splash art) | CC-BY-4.0 | Keep the Poppins notice. |
| Poppins fonts (`brand/fonts/`) | SIL OFL 1.1, unchanged | Keep `brand/fonts/OFL.txt` and the copyright line with the fonts. Do not relicense. |
| Third-party sources in tree | `tools/ttimage/blowfish.c`: LGPL (Paul Kocher). `tools/ttimage/ttimgextract.c`: GPL-era header by its original author. `tools/devtools/hciattach.c` and anything from BlueZ or the kernel: check headers | Keep every upstream header. List them in a `NOTICE` file. |

Placement: `LICENSE` (or `LICENSE-MIT` plus `LICENSES/`) at the repo root, an SPDX header in each source file you can edit, a "Licence" section in the README that matches, and `brand/LICENSE` for the brand terms.

Quick check before publishing: `grep -rL 'SPDX-License-Identifier' kernel tools userland panel` to see which sources lack a header.

## 7. Build from source without CI

No CI is configured; builds are local Docker commands. Gaps to close before publishing are marked TODO; do not claim a reproducible build until they are closed and tested from the public export.

Prerequisites: Docker, a Mac or Linux host, about 3 GB of disk.

Kernel (Linux 6.1.y; S3C24xx was removed in 6.2):

```sh
mkdir -p .runtime && cd .runtime
# fetch linux-6.1.191.tar.xz from kernel.org, verify its signature or sha256, then:
tar xf linux-6.1.191.tar.xz && mv linux-6.1.191 linux-6.1 && cd ..
kernel/scripts/docker-build.sh
# output: artifacts/kernel-builds/homtom-6.1/{zImage,System.map,.config,SHA256SUMS}
```

`docker-build.sh` hard-codes `/usr/local/bin/docker`; TODO make it use `docker` from `PATH`.

Out-of-tree module: `kernel/scripts/build-module.sh` (homtom-sdi) and `build-modules-tree.sh` for the Bluetooth set; see the comments in each.

Userland (ARM, ARMv4T, static):

- TODO: no Makefile or build script covers `tools/devtools/*.c` today; the binaries were built by hand and are tracked as ELF files. Add `tools/devtools/build.sh` (Docker, `gcc-arm-linux-gnueabi`, `-march=armv4t -static`) and test it from a clean export before release. Note `homtom-bl` needs its own `__aeabi_uidiv` (no libgcc), see `docs/LESSONS.md`.
- Panel: `panel/build.sh` builds `panel/build/homtom-panel` in the Docker image from `build-env/Dockerfile` (`docker build -t tomtom-revival-build:gcc334-host build-env`). It needs the TomTom gcc 3.3.4 cross toolchain at `artifacts/source/toolchain/`. That toolchain is TomTom's GPL source release; do not redistribute it. Document where to download it from TomTom's source-code page and how to verify it. TODO: consider moving the panel to the Debian cross compiler so no vendor toolchain is needed.

Image assembly: `kernel/scripts/build-ttsystem-6.1.sh` needs `tools/ttimage/mkttimage` (build it: `make -C tools/ttimage`), the userland binaries, a BusyBox build and the Bluetooth modules. TODO: document how BusyBox is produced; the exported tree does not contain it.

Tests (host only, no device): `python3 -m unittest discover -s tests -v` and `panel/tests/test_panel.c`. The emulator needs Docker (`emulator/run`, see `docs/emulator-plan.md`).

## 8. Binary artifacts versus source

| Publish as a GitHub release asset (with `SHA256SUMS`) | Build from source only |
|---|---|
| `zImage` and the matching `.config`/`System.map` for a tagged commit | Anything containing TomTom code: stock `ttsystem`, stock bootloader, NOR dumps |
| Prebuilt ARM userland tarball (`tools/devtools` set, panel) | The vendor gcc 3.3.4 toolchain |
| A staged `ttsystem` image only if it contains no vendor code and is rebuilt from the tagged commit | Backups of any individual unit (they hold the unit's identity) |
| Splash `.fb` files (own artwork) | Anything under `artifacts/` not listed on the left |

Rules: every asset has a checksum, the commit it was built from, and the build command. No release says "flash this" without the safety text from the README. Never publish a firmware image as "tested" without a device log artifact and an independent check. Do not publish a recovery claim: none exists yet (`docs/RECOVERY.md`).

## 9. Contributing and safety notes

- Add `CONTRIBUTING.md` (short): hardware-only project, confirmations of pin maps and Bluetooth results are most useful, record lessons as symptom, cause, fix in `docs/LESSONS.md`, keep claims tied to evidence, mark user-reported versus measured, commit subjects short and imperative, no personal data (usernames, paths, serials, addresses, MACs, PINs) in commits, logs or issues.
- Add an issue template that asks for the exact image, a boot log excerpt and a redaction reminder.
- Safety, copied to a root `SAFETY.md` and linked from the README: can brick the unit; NOR holds the bootloader and needs JTAG or serial to recover; never write NOR; never write `INFORM0=3` (working value 5); no raw disk commands; no USB soft-connect toggling; use `reboot`, not `reboot -f`; keep a known-good fallback `ttsystem` on INTERNAL; storage mode needs an unlocked Mac; at your own risk.
- Say clearly the project is not affiliated with TomTom and ships no TomTom software, maps or firmware.
- The exported repo drops the lab harness; keep a short note that the project was developed with local AI assistance only if the owner wants it stated. If so, state it plainly in the README and put no attribution trailers in commits.

## 10. Announcement

- Prepare the repo first (README, licence, safety, contributing, a release with checksums). Announce only once a stranger can build the kernel from the public repo.
- Draft a short post: what it is (a TomTom GO 630 as a Linux 6.1 Home Assistant edge node), what works (only what section 5 allows), what does not, the bricking warning, link to the repo.
- Suitable places: the Home Assistant community forum, the OpenTom and retro-navigator communities, relevant Linux ARM and S3C24xx mailing lists or forums, a short write-up on a personal blog. One post each, no cross-spam.
- Do not mention private systems, hostnames or the owner's network. Screenshots and photos get the same image review as section 2.
- Be ready for issues: enable discussions, and pin an "unit bricked" FAQ pointing to `docs/flash-safety.md`.

## 11. GitHub repository settings

Create the repo yourself, empty (no README, licence or `.gitignore` from the template), then push the export's single commit.

- Visibility public only after the final scan passes.
- Description and topics: `tomtom`, `s3c2443`, `linux`, `home-assistant`, `embedded`, `reverse-engineering`, `bluetooth`.
- Default branch `main`; branch protection on `main` (require PR, linear history, no force-push).
- Enable secret scanning and push protection; enable Dependabot alerts (little to scan, but free).
- Disable Wikis; enable Issues and Discussions; add issue templates and `SECURITY.md` (how to report a leaked secret).
- Commit identity: make sure the pushing machine does not rewrite the author (check `git log --format='%an %ae'` after the push). Use the neutral identity or a no-reply address; keep email privacy settings on.
- Under Settings, Actions: leave disabled unless a CI workflow is added.
- Add the licence via the root file so GitHub detects it; set social preview to `docs/images/splash.png` or the icon.
- Releases: tag `v0.1.0`, attach the section 8 assets with `SHA256SUMS`, release notes say "early, partly working, unfinished".

## 12. Final gate

- [ ] Section 2 findings resolved or consciously kept.
- [ ] `scripts/public-scan.sh` passes on a fresh export, with `PUBLIC_SCAN_EXTRA` set for owner-specific terms.
- [ ] Images reviewed by eye.
- [ ] README claims reconciled with STATUS and LESSONS (section 5).
- [ ] Licence chosen and placed (section 6).
- [ ] Kernel builds from the public export on a clean machine (section 7); TODOs closed or documented.
- [ ] Release assets built from the tagged commit, checksummed.
- [ ] No link in `docs/` points to an excluded file.
- [ ] Neutral author on the single commit.

## Stock artwork in the working tree (release scrub)
- `docs/storage-gfx-mockups/` contact sheets and per-state PNGs render the stock TomTom bootloader screens next to the homtom designs; the stock images are TomTom's artwork. Exclude the stock side (or the whole folder) from the public export; ship only the homtom designs.
- `workspace/storagegfx/out/` and `workspace/storagegfx/art/` (rendered stock frames) are git-ignored for the same reason; never add them to a public tree.
