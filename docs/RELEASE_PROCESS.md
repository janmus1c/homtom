# Release process

Applies to pre-releases such as `v0.1.0-alpha.1`. Nothing here pushes, tags remotely or publishes; every publishing step is manual and done by the user. Safety text from `README.md` must travel with every release.

Version scheme: `MAJOR.MINOR.PATCH[-alpha.N|-beta.N|-rc.N]` in the top-level `VERSION` file. Alpha means development-only install through storage mode, partial recovery, NOR untouched. Tags are `v` plus the version. The image reports its version in `/etc/homtom-release`.

## Checklist

1. **Version bump.** Edit `VERSION`. Move the entries in `CHANGELOG.md` under the new heading (Keep a Changelog sections: Added, Changed, Fixed, Safety and resilience, Known limitations). Update `docs/RELEASE_NOTES_<version>.md`. Known limitations must be as honest as the evidence in `docs/LESSONS.md`.
2. **Clean tree.** Commit everything that goes into the image. `git status` must be clean, or the image says `git=<hash>-dirty` in `/etc/homtom-release`, and a dirty image must not be released.
3. **Build the image.** Kernel, modules from the same tree, then `kernel/scripts/build-ttsystem-6.1.sh` (README, Build). Set `SOURCE_DATE_EPOCH` for a stable build date. Keep the kernel `.config`, `System.map` and the build log with the image.
4. **Test matrix with the user.** Run on the unit with the exact bundle image, not a development copy. Record the result and date for each line in `docs/LESSONS.md` or the release notes; user-reported and measured stay separate.

   | Check | How |
   |---|---|
   | Image boots from storage-mode install | `tools/homtom-to-storage`, `tools/homtom-stage`, `tools/homtom-to-linux`, `tools/homtom-state` |
   | Version on the unit | `tools/homtom-sh 'cat /etc/homtom-release' 10` matches `VERSION` and the commit |
   | `ttsystem.prev` fallback | `tools/homtom-mark-good`, stage again, confirm `ttsystem.prev` holds the previous image |
   | Recovery: wedged userspace | stop the heartbeat, expect storage mode within about a minute |
   | Recovery: `tools/homtom-recover` | only with the user present and the power key at hand; record the outcome whichever way it goes |
   | Touch, backlight, GUI | Manual check on the unit |
   | Bluetooth visible, pair, RFCOMM `status` | Mac pairing helpers in `tools/host/` |
   | USB status endpoint and Home Assistant bridge | `docs/HA_LINK.md` |
   | Sound, microphone, GPS | state the result; known-bad items stay in Known limitations |
5. **sha256.** `shasum -a 256 ttsystem`. Write it into the release notes. The same hash must be in `ttsystem.sha256` in the bundle and match the image the hardware tested.
6. **Tag locally.** `git tag -a v<version> -m "homtom <version>"` on the release commit. Local only. Do not push the tag; the private history will not be published.
7. **Bundle.** `scripts/make-release.sh --dry-run PATH/TO/ttsystem` first, then without `--dry-run`. Output goes to the gitignored `release/` (directory, tarball and tarball checksum). The script refuses `.runtime/` paths and secret file names, checks the image is a valid TTBL image, and runs `scripts/public-scan.sh` over the bundle. It fails on any finding.
8. **Scan.** Run `PUBLIC_EMAIL=<neutral address> PUBLIC_SCAN_EXTRA='<private hostnames|names|ids>' scripts/make-release.sh --dry-run ...` so maintainer-specific terms are included. Then review images and prose by eye. The scanner skips binaries, so the image itself is only checked for obvious clear-text strings; build it from a clean checkout.
9. **Clean-history export.** `PUBLIC_NAME=<neutral> PUBLIC_EMAIL=<neutral> scripts/public-export.sh OUTDIR`, then `scripts/public-scan.sh OUTDIR`. Check that `LICENSE`, `SAFETY.md` and `CONTRIBUTING.md` are present in the export.
10. **Announce.** Only after the new repository builds from its own tree and the release assets carry checksums. The post says what it is, what works, what does not, and carries the bricking warning. No private systems, hostnames or network details.

## Do not

- Do not publish a recovery claim beyond what step 4 recorded.
- Do not include stock TomTom files, NOR dumps, unit backups, `ttsystem.lab2613`, `ttsystem.prev`, `.runtime/`, the Bluetooth PIN or link keys, webhook ids, hostnames, Bluetooth or other MAC addresses, or a personal email address in any bundle.
- Do not ship `tools/homtom-flash-bootloader` or any NOR writer.
- Do not tag, push or create a GitHub release from the private repository.
