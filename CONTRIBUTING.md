# Contributing

homtom runs on one piece of hardware: the TomTom GO 630 (Samsung S3C2443, Linux
6.1). There is no generic target. Changes are judged on that unit.

Read `SAFETY.md` first. Do not send changes that write NOR flash.

## Most useful contributions

- Confirmations of pin maps, register values and Bluetooth results on your own
  unit, with the evidence.
- Fixes to docs that disagree with the hardware.
- Tests, build fixes and tool fixes.

## Evidence rules

- Tie every claim to evidence: a log, a register dump, a measurement, a file
  hash. Say where it is.
- Label each observation as user-reported (someone saw it, no measurement) or
  lab-measured (a tool recorded it). Do not mix them.
- Do not write that something works, boots or recovers unless a test showed it.
  "Untested" is an acceptable status.
- Correct wrong claims explicitly instead of editing them away.

## Lessons

Record a lesson in `docs/LESSONS.md` as three parts: symptom, cause, fix. If the
cause is unconfirmed, say so.

## Commits

- Short imperative subject, for example `Fix storage-mode timeout`.
- No body unless the reason is not obvious.
- Do not add personal data to commits, logs, issues or pull requests: user
  names, home paths, serial numbers, addresses, MAC addresses, PINs, link keys,
  hostnames.

## Pull requests

Describe what changed and which unit and image you tested it on, or say it is
untested. Keep one topic per pull request. Do not include TomTom firmware, maps,
NOR dumps or unit backups.

## Licensing

Kernel code (`kernel/`) is GPL-2.0-only. Other original code is MIT. Brand
artwork is CC-BY-4.0. By contributing you agree to license your change under the
licence of the directory it goes into. New source files should start with an
`SPDX-License-Identifier` line. See `LICENSE` and `NOTICE`.
