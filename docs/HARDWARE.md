# Hardware and uncertainty ledger

These observations were supplied by the device's user; this project has not independently re-measured them:

- TomTom GO 630, label `4CJ6.000.00`, serial prefix `JB`, FCC `S4L4CH6`.
- PCB `DA0VF8MB8C0 REV.C`, sticker `31VF8MB0280 C3A`.
- Display board `LTE430W-C0B MAIN REV0.3`; visible codec `WM8750BG`.
- OpenTom Nano-X terminal, touchscreen and on-screen keyboard work.
- Kernel 2.6.13, ARM implementer 0x41 / part 0x920, ARMv4T; hardware TomTom GO, revision b000a.
- BusyBox 1.22.1, MemTotal 62264 kB; no swap observed.

Samsung S3C2443 is a research hypothesis. Board resemblance to GO 920/930 is insufficient identification. Exact SoC, GPS/Bluetooth transport, UART voltage/pinout, storage map and power mechanism are open questions. Linux 5.10 and 6.1 are research targets, not demonstrated ports.

Measurements belong in a log with command and artifact provenance. Do not promote these user-reported facts into newly captured hardware evidence.
