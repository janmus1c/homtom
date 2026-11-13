# R1 writer: independent review (gate R1-G2), 2026-11-07

Scope: `tools/devtools/norwrite.c`, `tools/devtools/norwrite_plan.h`, the committed binary `tools/devtools/norwrite`, the generator `workspace/nor-model/gen_plan.py`, the model and harness under `workspace/nor-model/`, and the datasheet claims in `docs/NOR_PLAN.md` 9.5. This is the second-person check that `NOR_PLAN.md` 9.8 asks for. Nothing in this review touched the device. The writer ran only against host chip models (the author's and one written for this review) and, for `--dry-run`, `--plan-hash` and a usage error, under `qemu-arm` in a container.

Tags as in `NOR_PLAN.md`: [V] checked here from source, binary, dump or datasheet; [H] hypothesis.

## Verdict

**Update after the delta review (end of this file): GO for R1 with the hardened binary 8e575d24…**, which replaces dcd8297a… as the binary to stage. One item is a CONCERN, with a procedural mitigation. The original verdict on dcd8297a… follows.

**GO for R1 with the binary as built (dcd8297a…).** The review found no blocking finding. Every checklist item passes. Three hardening changes are recommended (F1-F3 below). None is needed for the measured chip. Any of them changes the binary hash, so if the author adopts one, the new binary must be reviewed again before R1-G3. The other gates (R1-G3 same-boot preconditions, R1-G4 approval, R1-G5 result) are still open and are not covered here.

## Checklist (NOR_PLAN.md 9.8)

| # | Item | Verdict | Evidence |
|---|---|---|---|
| 1 | Plan table vs generator and an independent recomputation | **PASS** | See "Plan". `gen_plan.py --check`: matches. `gen_plan.build()` written to a temp file is byte-identical to `norwrite_plan.h`. My own parser (not `writer_ref`, not `norimage`) gives the same 1,669 (word, stock, target) triples |
| 2 | `write16()` callers, command table, no erase in the default build | **PASS** | Source: `write16()` is called only from `cmd()` (table-gated) and `prog()`. `cmd_allowed` holds exactly (555,AA) (2AA,55) (555,90) (555,A0) (800,F0). The direct `write16` calls with 80/30 are only inside `#ifdef NORWRITE_ERASE_EXPERIMENT`, and the device branch has `#error` for that macro (lines 97-100), so it cannot compile without `NORWRITE_HOST`. No 10, 20, 98 or B0 anywhere outside that block |
| 3 | Reproducible build | **PASS** | Sources copied to a temp dir and built in `debian:bookworm-slim` with `arm-linux-gnueabi-gcc` 12.2.0-14 and the flags from `build.sh`. SHA-256 **dcd8297a20e0c4f3ae10569a355c3ff2040b097dc6caf65a1279ec1c759382c9**, bit-identical to the committed binary. The committed binary was not touched |
| 4 | Disassembly: ISA, stores to the NOR | **PASS** | `Tag_CPU_arch: v4T`. Mnemonics present: mov, add, ldr/ldrh/ldrb, str/strb/strh, ldm/stm/push/pop, b, bl, bx, cmp/cmn/tst, sub/rsb, eor/and/orr/bic/mvn, shifts, umull, svc. No blx, clz, ldrd/strd, pld, ldrex/strex, movw/movt, rev, sxt/uxt, barriers. Exactly **two `strh`**: `0x1028c` in `cmd()` after the table compare (byte offset = word << 1), and `0x112e8` in `run_r1` (inlined `prog`). The second follows the range check (`w - 0x1410B <= 0x7C1`), the `t & ~stock` check, the `cur == t` skip, `t & ~cur` and `cur & ~stock` stops, then `bl cmd` with (555,AA), (2AA,55), (555,A0). Every load of the `nor` pointer was traced by resolving PC-relative literals (`cmd`, `settle`, `scan`, `stop`, `run_r1` ×3); the other `str`/`strb`/`stm` instructions use `sp`, SHA context pointers, output buffers, or the address of the `nor` variable itself (`platform_map` stores the mmap result; `platform_lock`/`unlock` use `sigsave` = `nor`+4) |
| 5 | `.rodata` byte check | **PASS** | From the ELF sections: `cmd_allowed` at 0x12074 is (555,AA)(2AA,55)(555,90)(555,A0)(800,F0) and occurs once. `nw_plan_word`, `nw_plan_stock` and `nw_plan_target` equal the header arrays element for element. The u32 pairs (555,80), (555,30), (555,10), (555,20) and (055,98) do not occur in the file |
| 6 | Syscalls | **PASS** | `_start`: r0 = sp, `bl main_c`, `svc` 1 (exit) with the return code. `open` (5): `"/dev/mem"`, 0x101002 = O_RDWR \| O_SYNC (ARM O_SYNC = 0x101000), or 0x101000 read-only for `--check`. `mmap2` (192): addr 0, length 0x80000, prot 3 (1 for `--check`), MAP_SHARED (1), fd, pgoff 0. `setitimer` (104): ITIMER_REAL, {interval 0,0; value 30,0}. `rt_sigprocmask` (175): SIG_BLOCK, mask ~(1<<13) / 0xFFFFFFFF (SIGALRM left open), old mask to `sigsave`, size 8; restore with SIG_SETMASK. `clock_gettime` (263 = 0x107): CLOCK_MONOTONIC. `write` (4) to fd 1. The 6.1 config has `CONFIG_COMPAT_32BIT_TIME=y` (`workspace/kernel-trim/out/.config:521`), and `homtom-touch` already uses syscall 263 on the device |
| 7 | Gate logic in `scan()` | **PASS** | `patched` and `stock` need the raw SHA-256 to equal the compiled-in digest. `partial` needs the normalised digest (each plan word replaced by its stock value) to equal d48b41…, so every non-plan word must be identical, and every plan word must satisfy target ⊆ word ⊆ stock. RLE count bytes are equal in stock and target, so the interval rule pins them. Any other image is `foreign`. `partial` without `--resume` returns 5 before `platform_lock`, with no bus write. A wrong compiled-in stock table can only make the gate fail closed |
| 8 | Harness and unit tests | **PASS** | `run_harness.py`: 0 failures, every count identical to 9.6 (8,164 runs, 18,396,568 monitored writes, 0 monitor failures, 0 erase-capable cycles, 1,613 decoded states, 805,877 replayed reads, 103,015 fuzz ops). `python3 -m unittest`: 29 tests OK. Two or more new adversarial tests: see "Adversarial tests" |
| 9 | Stop path | **PASS** | Source and binary: every failure in the loop calls `stop()`, which sends F0@800, reports, and returns 3; the loop `break`s, `platform_unlock()` runs, and the function returns with no further `cmd`/`prog`. Harness: 0 program cycles after an injected DQ5, stuck-busy or slow program. My T1 and T6 below agree |
| 10 | Datasheet claims | **PASS** (one wording note) | Both PDFs fetched from the URLs in 9.5 and their SHA-256 values match (a5c1f93c… and 839e42f7…). See "Datasheet" |

## Plan (item 1 and task point 2)

Independent computation from `artifacts/backups/mtd0-before-flash-20261102T115819Z.bin` (SHA-256 d48b41abc85c…, 524,288 bytes), with code written for this review:

- Image chain from 0x28201, big-endian headers (len, id, width, height), next record at off + 8 + len: 24 records, end marker at 0x3ED84. Record 0: id 0, 237×51, len 3,986, RLE data 0x28211-0x2919A.
- RLE token walk over record 0: 1,377 count bytes, 2,601 value bytes, of which 2,257 are non-zero. The walk ends exactly at 0x2919A with 12,087 pixels.
- Zeroing the 2,257 non-zero value bytes touches 1,669 words, 0x1410B-0x148CC (bytes 0x28216-0x29199), and clears 9,002 bits. Palette entry 0 at 0x28000 is 0x0000.
- The header's 1,669 (word, stock, target) entries equal mine one for one. The plan SHA-256 over (u32 LE word, u16 LE target) is e6135999f3b3e403a95871aa473ec401fbf4e46901915589c2c68fd64a9d8d1e. That matches the header and the binary's `--plan-hash` under qemu.
- Applying the header's table to the dump with my own code gives **87dc81ca967579b424e3e48d1e680685549ee0d73d433d2cfd00c5c4611a384e**. Exactly 2,257 bytes change, and every one is a record 0 value byte.

Location checks, all [V]:

| Region | Plan words in it |
|---|---|
| SA0 0x00000-0x03FFF | none |
| Stage 1 and gzip, 0x08000-0x247DC (the last non-0xFF byte before the palette is 0x247DC) | none |
| Palette 0x28000-0x281FF, pad byte 0x28200, record 0 header 0x28201-0x28210 | none |
| Record 0 RLE count bytes | in plan words, but unchanged (target byte = stock byte) |
| Records 1-23, SA6 | none |
| ngffs 0x40000-0x7FFFF | none |

**Programming inside SA5, which also holds the gzip tail.** A word program is a single-address operation. Its only effect is cell ← cell AND data at the addressed word; there is no sector-wide step, unlike an erase. This is datasheet behaviour: "only an erase operation can change a 0 back to a 1", plus the Table 5 program cycle PA/PD. So programming words at 0x1410B-0x148CC cannot alter 0x20000-0x247DC. What remains is a wrong address on the bus, which software cannot rule out. The writer takes address and data only from compiled-in tables, behind two range checks.

**Each programmed word writes all 16 bits.** For every entry, target ⊆ stock, and the byte that is not meant to change has target byte = stock byte. Before the program cycle, the writer requires cur ⊆ stock and target ⊆ cur. The result is cur AND target. That equals target in the changed byte and stock in the kept byte, because cur's kept byte must equal stock (its hash or interval gate). No bit in the kept byte is asked to go 0→1, so no DQ5. I checked this for all 1,669 entries.

**Command aliasing.** Two plan words have low 12 bits equal to an unlock address: 0x142AA (target 0x00FF) and 0x14555 (target 0xFD00). Neither data value is a command byte. Even if the chip decoded only A10-A0 for unlock cycles, a data cycle that a dropped A0 turned into a command could not form 80/30/10. Two targets have a 0x80 count byte, but neither sits at an aliasing address.

## Static review notes (task point 3)

- **Byte vs word addresses** [V]: `nor` is `volatile u16 *`, so `nor[w]` is byte 2w. In the binary, `cmd` shifts the word left by 1, ID reads are at byte offsets 0, 0x200, 2, 4 and 0x20004 (words 0, 0x100, 1, 2, 0x10002 = SA5 base + 2), and RESET_WORD reads are at byte 0x1000. Word 0x555 is byte 0xAAA and 0x2AA is 0x554, as in the datasheet's word-mode table and the bootloader routine.
- **Endianness** [V]: the chip's word w holds bytes 2w (DQ0-7) and 2w+1 (DQ8-15). The dump was taken through the same `ldrh` path and stored little-endian. `sha_word` hashes low byte first. The plan's stock values are `img[2w] | img[2w+1] << 8`. All three conventions agree, and the gate hash would fail closed if they did not.
- **Mapping** [V]: O_SYNC plus a non-RAM pfn gives `pgprot_noncached` on ARMv4 (C=0, B=0, unbuffered), so stores are neither merged nor reordered. Both toggle-poll loads are separate `ldrh` instructions in the binary; the volatile access is not folded. `--check` maps read-only, so a stray store there would fault instead of reaching the chip.
- **Timeouts**: 1 ms per word (datasheet max 300 µs), with one re-check after the timeout. 1 ms for each `settle`. A 30 s ITIMER_REAL starts after the read-only gate. My rough estimate of the remaining work after the gate is about 1 s on the ARM920T [H]: two whole-chip scans of two SHA-256 streams each, plus 1,669 programs. `now_us()` wraps modulo 2^32 µs and the subtraction is wrap-safe.
- **Signals**: everything except SIGALRM is blocked during the bus phase. SIGHUP from a dropped Telnet session and SIGPIPE on stdout are therefore held, and the run continues. Only SIGKILL, SIGSTOP (pause) or the 30 s alarm can interrupt it. The alarm's default action terminates the process, which is the same case as a kill.
- **Autoselect reads vs data**: ID values are only compared, never written. The whole chip is re-hashed after the F0 and must equal the gate hash before any program cycle, so a chip left in autoselect cannot reach the loop. A status read can never equal a target: DQ7 shows ~data bit 7, which differs from the target's bit 7. A premature "done" therefore always fails the read-back.
- **Kill at any instant** (reasoned, and tested in T2 and T8 below):
  - After AA or AA/55: the next F0 is an improper sequence and returns the chip to read mode.
  - After 90 (autoselect): the next run's gate sees non-array data, refuses with no write, and `--reset` recovers.
  - After A0 (program setup): the next F0@800 either resets or is taken as data for word 0x800 in protected SA0. That costs about 2 µs busy and changes nothing; `settle` waits it out and the second F0 resets.
  - During a program: the chip finishes within 300 µs.
  - After DQ5 without the F0: the gate sees status reads, refuses, and `--reset` recovers.
  - The writer never enters an erase state.

## Findings

No blocking finding.

| ID | Severity | Finding | Fix |
|---|---|---|---|
| F1 | Concern (hardening) | `protect_sa0` is read and printed but not enforced. Why word 0x800 is safe as the reset target depends on SA0 being protected. My T2b confirms the documented residual: with SA0 unprotected and F0 after A0 taken as data, word 0x800 becomes 0x00F0 and every later run refuses the chip as foreign. Today SA0 is measured protected, twice (N2, N2b) | In `run_r1`, refuse (exit 4) unless `(prot0 & 0xff) == 0x01`, next to the SA5 check. A killed program-phase run then cannot exist on a chip where the residual applies |
| F2 | Concern (hardening) | No single-instance lock. Two concurrent `norwrite` processes could interleave cycles. Analysis: the writer only ever writes AA/55/90/A0/F0 at 555/2AA/800 and plan (word, target) pairs, so interleaving cannot form an erase. The worst case is a stop or a refusal, but it is untested | Procedural: run the writer once and check `ps` before R1. Or in code: take an O_CREAT\|O_EXCL lock file in `/tmp` before mapping |
| F3 | Note | `clock_gettime`'s return value is ignored. If it ever failed, `now_us()` would stay 0 and only the 30 s alarm would end a stuck poll. Not reachable on this kernel (item 6) | Optional: refuse when the first `clock_gettime` returns an error |
| F4 | Note | The ITIMER stays armed after `platform_unlock()`. Only read-only work follows, so it is harmless | None needed |
| F5 | Residual, not software | A weak cell that clears an extra bit (my T6) leaves a count byte or kept byte wrong. The writer stops and refuses every rerun, as designed, but record 0 is then damaged in a way only an SA5 erase can fix. Whether the bootloader's RLE decoder is bounded on a malformed record 0 is [H] (`NOR_PLAN.md` 9.5, last paragraph) | Accept as part of the R1-G4 risk statement |

## Adversarial tests (task point 5)

`adv.c` (kept in the review scratchpad, not in the repo) includes `norwrite.c` unchanged with `NORWRITE_HOST`. It uses its own chip model, not `nor_model.c`, and its own bus monitor. The monitor allows only AA@555, 55@2AA, then 90 or A0@555, F0@800, and plan (word, target) pairs, and counts any 80/30/10 byte in a command slot. After every run it checks that no non-plan word changed and that every plan word lies between target and stock.

| Test | Chip variant | Result | Verdict |
|---|---|---|---|
| T0 | Baseline | Exit 0, 1,669 program cycles, 812,559 bus ops, whole image equals 87dc81ca…, 0 monitor violations, 0 erase bytes in command slots | SAFE |
| T1 | SA5 protected but reported as unprotected, and a program to it ends in **DQ5** instead of silently | Exit 3 after exactly 1 program cycle; F0 brings the chip back to read mode; chip bit-identical to stock | SAFE |
| T2a | **F0 after A0 is taken as program data**; process killed right after the A0 cycle at 200 program positions; SA0 protected | All 200 reruns converge to 87dc81ca; word 0x800 never changes; 0 invariant or monitor failures | SAFE |
| T2b | As T2a, but SA0 **unprotected** | 200 of 200: word 0x800 becomes 0x00F0, the re-hash then differs, the writer stops (exit 3) and refuses after that. No other word changes. Confirms the residual in 9.6 and motivates F1 | SAFE (fail-closed; residual) |
| T3 | **Stale read**: the first read after any bus write returns the previous read's value | Fails closed on every attempt (exit 3 at the post-ID re-hash; 0 program cycles); no protocol violation, no change | SAFE (does not complete) |
| T4 | F0 honoured only at word 0 (address not don't-care) | Chip stays in autoselect; the re-hash differs; exit 3 with 0 program cycles; array unchanged | SAFE |
| T5 | DQ5 reads 1 on the read where the program completes (datasheet note) | The DQ6 re-check sees completion; run completes to 87dc81ca | SAFE |
| T6 | **Over-clear**: a weak cell clears one extra bit in the kept byte | Stops at the first real over-clear (exit 3 at that cycle); `--resume` classes the chip foreign and refuses with 0 cycles | SAFE (see F5) |
| T7 | Chip ignores the 90 command (reads stay array data) | ID mismatch, exit 4, 0 program cycles | SAFE |
| T8 | Process killed every 397 bus ops over the whole run (2,047 kills), alternating both F0-after-A0 readings, then `--write`/`--resume`, else `--reset` | 2,047 of 2,047 converge to 87dc81ca; 0 damage; 0 monitor violations | SAFE |

0 adversarial failures. T2b and T3 never reach the target, by design: the writer refuses instead of guessing.

## Datasheet (item 10)

Checked against the text of D1 (EN29LV400, Rev. C) and D2 (EN29LV400A, Rev. I), both with SHA-256 matching 9.5.

| Claim in 9.5 | Found | Verdict |
|---|---|---|
| Word program AA@555, 55@2AA, A0@555, PD@PA; byte mode AAA/555/AAA | D1 Table 5 | supported |
| Autoselect: 1Ch at word 100 (A8=H), 7Fh with A8=L, device 22BA bottom boot at X01, protect verify (SA)X02 = XX01/XX00 | D1 Table 4 note 1, Table 5; D2 Table 4 | supported |
| Bottom-boot sector map (SA5 = words 0x10000-0x17FFF) | D1/D2 bottom-boot sector table | supported; matches `nw_sector_word` and `SA5_WORD` |
| Improper sequences reset to read mode | D1 p. 11 | supported |
| Reset: address don't-care; ignored once programming or erasing begins; required to leave autoselect | D1 "Reset Command" | supported |
| Protected sector: DQ6 toggles about 2 µs, no change; erase of only protected sectors about 100 µs | D1 says "In Byte Programming…"; D2 says "In embedded Programming…" | supported. Wording note: D1 literally names byte programming; D2 states it for programming in general |
| DQ5: pulse-count limit, may read 1 at completion so re-check DQ6, reset needed | D1 DQ5 section | supported |
| No multi-sector erase; DQ3 = 1 right after the first 30 | D1/D2 DQ3 section | supported |
| Word program 8 µs typical / 300 µs max; sector erase 0.5 / 10 s (excludes 00h preprogram); chip erase 5 / 100 s | D1 Table 11, D2 same | supported |
| Hardware reset to read 20 µs; erase suspend at most 20 µs | D1 tREADY, erase-suspend text | supported |
| Unlock bypass removed in D2 Rev. C (2006/02/21) | D2 revision list | supported |
| No WP# or ACC pin; VLKO lock-out; pulses under 5 ns ignored | D1/D2 pin tables (A0-A17, DQ0-DQ15, CE#, OE#, WE#, RESET#, RY/BY#, BYTE#, Vcc, Vss); VLKO 2.3-2.5 V; "less than 5 ns" | supported |

No claim in 9.5 was found unsupported. Both copies are third-party mirrors, as 9.5 already states.

## Reproduction

- Plan and target: an independent Python script over the dump (record walk, token walk, apply the header table, SHA-256).
- Build: copy `norwrite.c` and `norwrite_plan.h` to a temp dir, then run `arm-linux-gnueabi-gcc -nostdlib -march=armv4t -marm -static -fno-stack-protector -O1 -fno-tree-loop-distribute-patterns -o norwrite norwrite.c` in `debian:bookworm-slim`. Then `readelf -A`, `objdump -d`, and a PC-relative literal resolver to trace every use of `nor`.
- Harness: `python3 workspace/nor-model/harness/run_harness.py OUT` and `python3 -m unittest` in `workspace/nor-model`.
- `build.sh` gained a `SUFFIX` option while this review ran (uncommitted). The compiler flags are unchanged. `norwrite.c`, `norwrite_plan.h` and the committed binary were byte-checked at the end of the review: unchanged, binary still dcd8297a….
- Datasheets: the two mirror URLs in 9.5, compared by SHA-256, then `pdftotext -layout`.

## Delta since the reviewed build (dcd8297a…)

Added after this review, by the author, to adopt F1-F3. The delta was reviewed later, in "Delta review" below. R1-G2 holds for dcd8297a… only until a second person checks the points below. Details, counts and the full diff: `NOR_PLAN.md` 9.9.

| | Reviewed | Now |
|---|---|---|
| `tools/devtools/norwrite.c` | commit 48918a2 | SHA-256 45fe498dc84e8b27ce9884addbb2cb86df709b1c5b1a052ff359fec13c75391e, +168/−10 lines |
| `tools/devtools/norwrite_plan.h` | a6e08398… | unchanged |
| `tools/devtools/norwrite` | dcd8297a…, 24,412 bytes | **8e575d247dc0cc2eda6feaf6d0f0af90ffbc3f946c3fc898c53cc6260499cacf**, 26,100 bytes |
| `workspace/nor-model/harness/harness.c` | 48918a2 | +144 lines: simulated lock file and pid table, new cases (sections 11-12), lock-left check in `run()` |
| `build.sh` flags, toolchain | as in item 3 | unchanged; the same container still rebuilds 48918a2 to dcd8297a… |

What changed in the writer, and nothing else:

1. **F1**: in `run_r1`, straight after the SA5 check: `(prot0 & 0xff) != 0x01` → `platform_unlock`, message, **exit 6**. `prot0` is the existing autoselect read of word 0x002. It runs after the ID check and the re-hash, before the first program cycle. The autoselect command writes come before it, as for the SA5 refusal.
2. **F2**: `locked(write, arg)` wraps `check(0)`, `check(1)`, `run_r1(0)` and `run_r1(1)` in `norwrite_main`. These are the only four calls that change. `lock_take()` creates `/tmp/norwrite.lock` with O_EXCL and the decimal pid. On EEXIST it reads the file. If the pid is gone (kill(pid, 0) = ESRCH) or is our own, the lock is stale: it is unlinked and the create retried once. Otherwise **exit 7**, with no bus access and the file left alone. Empty or non-numeric content counts as held. `lock_drop()` unlinks the file after the wrapped call returns. `platform_lock()` now also installs `on_alarm` for SIGALRM (rt_sigaction 174). The handler unlinks the lock if `lock_held` is set, writes `STOP: 30 s hard timeout` and calls `exit(3)`. It does no bus access, and before this change the default action killed the process at that point. The host build gets the lock primitives through the `nw_host_lock` hook; with the hook NULL, as in an unchanged `adv.c`, the lock is always free.
3. **F3**: `now_us()` sets `clock_bad` when `clock_gettime` returns an error. `locked()` calls `now_us()` once after taking the lock and returns **exit 4** before any bus access if it failed.

Verify in minutes:

- Source: `git diff 48918a2 -- tools/devtools/norwrite.c`. Check that no line touches `cmd_allowed`, `cmd()`, `prog()`, `scan()`, `wait_done()`, `settle()`, `reset_chip()`, `stop()`, the program loop or the `NORWRITE_ERASE_EXPERIMENT` block.
- Build: `build.sh` on a temporary copy of `norwrite.c` and `norwrite_plan.h`, then compare with 8e575d24….
- Disassembly: `Tag_CPU_arch: v4T`; still exactly two `strh`, at `0x102a4` (`cmd`) and `0x11570` (`locked`, into which `run_r1` and `prog` are now inlined). `.bss` goes through one anchor at 0x16bf8 (`clock_bad` +0, `nor` +4, `sigsave` +8, `lock_held` +0x10), so trace offsets from it rather than a literal `nor` address. The only non-stack stores through it are `now_us` +0, `platform_map` +4 (as before) and `locked` +0x10 ×2. The new syscalls are getpid 20, open 5 (`/tmp/norwrite.lock`, 0xC1, 0644 or read-only), write 4, read 3, close 6, kill 37 (signal 0), unlink 10, rt_sigaction 174, and exit 1 in `on_alarm`. The `.data` template at 0x16be4 is {0x10ab4 = `on_alarm`, 0, 0, ~0, ~0}.
- Harness: `python3 workspace/nor-model/harness/run_harness.py`. Every pre-existing line except the totals equals the run on the reviewed source. The new lines are SA0 exit 6 (stock chip and partial `--resume`), the lock-held cases exit 7 with 0 bus ops (12 of 12), stale/own-pid exit 0, lock not creatable exit 7, clock failure exit 4, and 1,902 stale locks removed after cuts. Totals: 8,184 runs, 18,411,614 monitored writes, 0 failures.
- `adv.c` unchanged: T0, T2a and T3-T8 as above. T1 and T2b now print UNSAFE because their chip variants report SA0 unprotected (T1's `prot_reports = 0` zeroes every sector's protect word), and the writer now refuses them with exit 6 and 0 program cycles. With T1 reporting SA0 and T2b asserting the refusal, all tests are SAFE.

Points for the delta reviewer:

- The stale-lock reclaim is not atomic. Two copies started together right after a killed run could both reclaim, and then both run. Untested; the procedural `ps` check stays.
- `--check` now writes a file in `/tmp` and refuses (exit 7) while another copy runs.
- Exit 6 is reached after the autoselect command writes, not before every bus write. The protect bit is only readable in autoselect.
- `on_alarm` is checked only from the disassembly. Neither the harness nor qemu reaches the 30 s alarm inside the bus phase.
- `lock_held` is `volatile`. In the binary, `lock_drop` stores 0 before it calls `lk_unlink`.

## Delta review (hardened build 8e575d24…), 2026-11-07

Second-person check of the author's delta above. Reviewed from `git diff 48918a2 -- tools/devtools/norwrite.c`, a fresh container build, the disassembly, the author's harness, `adv.c`, the author's `adv-delta.c`, and a new lock test `adv-lock.c`. All tests ran on host models or under qemu-arm; nothing ran on the device.

Inputs [V]: `norwrite.c` SHA-256 45fe498d…, `norwrite_plan.h` a6e08398… (no diff since 48918a2), committed binary 8e575d24….

### Verdict per item

| # | Item | Verdict | Evidence |
|---|---|---|---|
| D1 | Diff scope: plan table, command table, set of NOR stores unchanged in logic | **PASS** | Every changed line is in: the header comment, the new defines and exit codes, the `lock_held`/`clock_bad` globals, the host lock hooks, `now_us()` (return check only), new `lk_*` and `on_alarm`, `platform_lock()` (`rt_sigaction` added before `setitimer`), one new SA0 block in `run_r1` right after the SA5 refusal, new `lock_take`/`lock_drop`/`locked`, and the four `norwrite_main` dispatch lines. No change to `cmd_allowed`, `cmd()`, `prog()`, `scan()`, `wait_done()`, `settle()`, `reset_chip()`, `stop()`, `plan_ok()`, the program loop or the erase block. `norwrite_plan.h` is byte-identical to 48918a2 |
| D2 | Reproducible build, stores, ISA, erase pairs | **PASS** | A temp-dir build in `debian:bookworm-slim` with the `build.sh` flags gives **8e575d247dc0cc2eda6feaf6d0f0af90ffbc3f946c3fc898c53cc6260499cacf**, as claimed. `Tag_CPU_arch: v4T`; no ARMv5+ mnemonic. `.bss` anchor 0x16bf8: `clock_bad` +0, `nor` +4, `sigsave` +8, `lock_held` +0x10. I traced all 15 PC-relative loads of the anchor. Exactly **two** `strh` go through the loaded `nor` pointer: `0x102a4` in `cmd` behind the table compare, and `0x11570` in `locked` (inlined `run_r1`/`prog`) after the same range and subset checks and the AA/55/A0 `cmd` calls as before. A grep hit `strhi` at `0x10128`: that is a conditional 32-bit `str` of 1 to `clock_bad`, not a halfword store. Other anchor stores: `nor` variable (+4, `platform_map`), `lock_held` (+0x10, ×2 in `locked`). `cmd_allowed` occurs once; (555,80) (555,30) (555,10) (555,20) (055,98) do not occur. `--plan-hash` and `--dry-run` under qemu print e6135999… and the unchanged plan |
| D3a | A lock failure can never lead to a bus access | **PASS** | `locked()` returns before `check`/`run_r1`, and so before `platform_map`, whenever `lock_take()` fails. Under qemu with the real binary, each of these gives exit 7 with "no bus access" and leaves the path untouched: live pid 1, live `sleep` pid, `"123\n"`, empty file, directory, dangling symlink. A dead pid, and a symlink to a file holding a dead pid, are reclaimed: the symlink itself is unlinked, not its target. `adv-lock.c` L2-L4: 0 bus ops on every refusal. Harness: 12 of 12 held-lock runs exit 7 with 0 bus ops |
| D3b | Lock removed on every exit | **PASS** | Normal returns: `lock_drop()` after the wrapped call (`adv-lock.c` L6: `--check`, `--write`, `--reset`, `--resume` all leave no file; qemu exit 4 leaves no file). SIGALRM: `rt_sigaction(14, {on_alarm, 0, 0, ~0, ~0}, NULL, 8)` in the binary (template at 0x16be4). `on_alarm` loads `lock_held` from memory, calls `unlink`, writes 24 bytes and calls `exit(3)`. It makes no bus access, the same as the default termination it replaces. Checked statically only; nothing here reaches the 30 s alarm. SIGKILL: the file stays and the next run reclaims it (L1: the killed run made 0 bus writes, the next run completed R1 and removed the lock; harness: 1,902 stale locks reclaimed) |
| D3c | Stale-reclaim race | **CONCERN** | `adv-lock.c` L5 reproduces it. Copy B reads a stale pid and decides to unlink. Copy A reclaims first and creates its own lock. B's `unlink` then removes A's live lock, B's retry succeeds, and **both run**. This needs a stale lock (a previous SIGKILL or power cut) and two copies started within the same few syscalls. Consequence, reasoned and not modelled: the writer only ever sends AA/55/90/A0 at 555/2AA, F0 at 800 and plan (word, target) pairs. Interleaving can therefore AND a plan target into a plan word, or turn a command write into program data for 0x555, 0x2AA or 0x800. Those are all in SA0, which every run that reaches a program cycle has now verified as protected. No erase byte is ever written. Expected outcome: a stop or refusal, not damage. Fix, for a later build: replace the pid file with `flock(fd, LOCK_EX \| LOCK_NB)` (syscall 143) on a held-open `/tmp/norwrite.lock`. The kernel drops it when the process dies, so no reclaim is needed. Procedural mitigation for this build, enough for R1: before the run, `ps` shows no `norwrite` and the operator starts exactly one copy |
| D3d | SA0 gate before any program cycle; autoselect writes before it harmless | **PASS** | Binary: `cmp sl(prot0 & 0xff), #1` at 0x11318 comes after the ID reads (0x11028-0x11040), the F0, the re-hash and the ID/SA5 checks, and before the loop (0x114xx), which holds the only path to `0x11570`. The writes before it are F0@800 ×2, AA@555, 55@2AA, 90@555 and F0@800. These are the same cycles N2/N2b ran on this unit [M], and the old build sent them too. Read mode is proven by the whole-chip re-hash. One theoretical case is left. A chip with SA0 *unprotected*, left in program setup by an old-binary run, could take the first F0 as data for word 0x800. With the new binary nothing on such a chip ever gets to A0: harness "SA0 not protected" exit 6 with 0 program cycles, and `adv-delta.c` T2b' 0 kills reaching program setup. The unit has never run dcd8297a…, so the case cannot arise |
| D4 | `clock_gettime` failure | **PASS** | `now_us()` sets `clock_bad` on an error return (`cmn r0,#4096; strhi`). `locked()` calls it once after the lock and refuses with exit 4 before any bus access. Harness: exit 4, 0 bus ops, lock removed. Later failures are not re-checked; the 30 s alarm still bounds them |
| D5 | Author's harness and unit tests | **PASS** | `run_harness.py`: 0 failures. 8,184 runs, 18,411,614 monitored writes, 0 monitor failures, 0 erase-capable cycles, 1,613 decoded states; the replay and fuzz checks pass. New lines: exit 6 ×2, exit 7 12/12, stale and own-pid exit 0, clock exit 4. `python3 -m unittest`: OK |
| D6 | Adversarial suites on the hardened source | **PASS** | `adv-delta.c`: 0 failures (T0-T8, T2b' asserts exit 6). Unchanged `adv.c`: T0, T2a and T3-T8 SAFE. T1 and T2b print UNSAFE only because their chip variants report SA0 unprotected, which the writer now refuses with exit 6 and 0 program cycles. That is the intended F1 behaviour. With SA0 reported, T1 (`adv-delta.c`) stops after 1 cycle as before. `adv-lock.c` L1-L4 and L6 SAFE; L5 is D3c |

### Notes

- A FIFO at `/tmp/norwrite.lock` would block `lk_read`'s `open` until a writer appears. That is a hang before any bus access, not a safety issue.
- `--check` now needs a writable `/tmp` and exits 7 while another copy runs. This is intended.
- After `platform_unlock()` the alarm stays armed with the handler installed. Only output and read-only work follow, so it is harmless (F4 as before).

### Recommendation

**GO with the hardened binary 8e575d247dc0cc2eda6feaf6d0f0af90ffbc3f946c3fc898c53cc6260499cacf** as the R1-G2 artefact. Stage it in R1-G3 in place of dcd8297a…. Add one precondition to R1-G3: `ps` shows no running `norwrite` and `/tmp/norwrite.lock` does not exist, then start exactly one copy. Moving the lock to `flock(2)` is recommended for any later rebuild but is not required for R1. Any rebuild needs another delta review.
