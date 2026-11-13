# TomTom GO 630 — Autonomous Firmware Research Laboratory

## Mission and operating contract

Build a local-first, autonomous firmware reverse-engineering and kernel-porting laboratory for an original TomTom GO 630, controlled from a MacBook Pro (Apple Silicon) through the Pi coding-agent harness and **only the user's locally hosted Qwen3.8-27B-oQ4e-MTP model via oMLX** for agent reasoning and coding. Use existing local oMLX configuration and avoid external paid LLM APIs. The user accepts risk of rendering the **TomTom device** unbootable, but this does not authorize risking the Mac, other devices, production services, private credentials or personal files.

This is a real engineering program, not a conversational simulation. Produce working tooling, test it, collect evidence, preserve progress, and enable autonomous experiments once a stable physical connection and recovery procedure exist. If the device is absent, start with offline source analysis, build-system research, scaffolding, and simulated integration tests. Never pretend the device is connected or a physical experiment succeeded.

Do **not** install new firmware or write raw flash during the initial discovery stage. After the supervisor validates backups, boot selection, and a recoverable test path, allow unattended device-specific experimental writes using an explicit project-owned capability flag. Prefer removable SD and replaceable boot images; never conflate ordinary filesystem writes with immutable boot ROM or undocumented flash operations. Device unavailability or a loss of console is a legitimate result; do not invent success and do not retry destructive operations indefinitely.

## Observations from the user's actual device (ground truth)

- Physical label: TomTom GO 630, `4CJ6.000.00`, serial prefix `JB`, FCC ID `S4L4CH6`.
- Main PCB silk: `DA0VF8MB8C0 REV.C`; assembly sticker `31VF8MB0280 C3A`.
- Display board: `LTE430W-C0B MAIN REV0.3` (photo marking, 2007-02-15).
- Visible codec marking: `WM8750BG` (Wolfson audio codec).
- Running OpenTom Nano-X terminal with a working touchscreen and on-screen keyboard.
- Kernel reports CPU implementer 0x41, CPU part 0x920, architecture ARMv4T, ARM920T-class core, hardware `TomTom GO`, revision `b000a`.
- `uname -a` begins `Linux (none) 2.6.13 #1 Wed Mar 8 ...` (2023 build; full line not captured).
- BusyBox 1.22.1.
- `MemTotal: 62264 kB`; `MemFree: 49516 kB` in one snapshot; no swap.
- `/proc/cmdline` shows `root=/dev/mmcblk0p2` (does **not** prove that partition is mounted as `/`).
- `/` appears as `rootfs` in the mount output; `mtd0` mounts as `ngffs` at `/mnt/flash`; `/dev/mmcblk1` is mounted as VFAT at `/mnt/sdcard`; `/dev/mmcblk0p1` is mounted as VFAT at `/mnt/movinand`.
- `/proc/partitions` shows `mtdblock0` of 512 KiB and a 2 GB class `mmcblk1` device. The captured list is partially clipped.
- Exact SoC, bootloader, image provenance, Bluetooth transport, power-control mechanism, full block-device mapping, and backup/recovery path are **not confirmed**.
- Samsung S3C2443 is a research candidate, **not an identification**. GO 920/930 PCB similarities do not prove GO 630 component population.
- Prior discussion proposed Linux 5.10 as a substantive port and Linux 6.1 as a research target. Neither has been shown booting on this TomTom; the current working reference is 2.6.13.

## Official integration contracts to verify against the installed versions

- Pi extensions: https://pi.dev/docs/latest/extensions
- Pi SDK: https://pi.dev/docs/latest/sdk
- Pi RPC mode: https://pi.dev/docs/latest/rpc
- Pi RPC commands: https://pi.dev/docs/latest/rpc-commands
- Pi JSON event stream: https://pi.dev/docs/latest/json
- Pi local model configuration: https://pi.dev/docs/latest/models
- OpenTom references: https://github.com/LeddaZ/OpenTom and https://github.com/micahlt/OpenTom
- OpenTom documentation historically reports USB root Telnet + FTP, usually `192.168.1.10` or `192.168.1.200`, recommending Linux for compatibility. Treat both IPs as hypotheses until enumerated on the actual device.

Pi RPC exposes `prompt`, `get_state`, `new_session`, `get_session_stats`, `set_thinking_level`, `abort`, etc. Read JSONL from stdout continuously; parse responses by request `id`; treat `agent_settled` (not `agent_end`) as the final idle event. Pi extensions provide `tool_call`, `tool_execution_end`, `agent_before_settle`, `agent_settled`, `session_start`, and `session_shutdown`. Check the actual installed API before writing imports and compiling extensions.

## Architecture to implement

### A. External supervisor (separate OS process, independent of Pi and the model)

Implement a durable orchestrator, preferably Python asyncio or TypeScript, that launches Pi in `--mode rpc` as a child process. It must:

1. Validate oMLX availability and model ID before starting; reuse actual local config, do not assume port or model name.
2. Validate that model produces legitimate tool calls, and check whether thinking controls actually map through this provider. Use measured behavior rather than an assumed internal thinking flag.
3. Create/reconnect Pi sessions and attach an event listener **before** issuing a prompt.
4. Persist each raw JSONL response, stderr, experiment record, model/tool usage, and state transition to project-local files or SQLite with monotonic IDs and timestamps.
5. Track agent progress from actual `turn_end`, `tool_execution_end`, durable artifact changes, and `agent_settled`, not token streaming alone. Long model generation is not itself progress.
6. Enforce per-action, per-experiment, and per-session time limits. Detect repetition by normalized command hashes, repeated identical outputs, unchanged hypotheses, and attempts that yield no new evidence.
7. On stuck processes: record the current experiment as `interrupted`, abort or terminate the child, create a fresh Pi session, and inject a concise checkpoint from verified state. Never restart Qwen/oMLX automatically if another service owns it, unless specifically authorized and isolated.
8. Back off and use an offline-work queue when oMLX, USB, or the TomTom is unavailable. No uncontrolled restart storm.
9. Provide a `STOP`/pause file or local CLI command and atomic capability controls for `read_only`, `write_removable`, `write_internal`, and `power_control`. Default `read_only`. Enforce them outside natural-language prompts.
10. Write an executive summary after each settled run and a final morning report showing newly verified facts, tests, failures, builds, writes, and next steps. Optional Telegram notification via already configured, explicitly scoped credentials, not required for MVP.
11. Ensure no more than one actor can access the device for a mutating experiment at a time (resource lease/mutex).
12. Do not accumulate huge Pi sessions: rotate after a bounded number of experiments/turns or context budget; use compact evidence handoffs, not giant conversation replay.

### B. Pi extensions / tool restrictions

Build project-local extensions in `.pi/extensions/`:

- `tomtom-tools`: typed tools `device_probe`, `device_shell_read`, `device_backup`, `device_reboot`, `device_transfer`, `device_flash`, `device_wait_for_boot`, `device_capture_console`, `experiment_record`. Use explicit parameters, subprocess argument arrays, timeouts, and structured results. Device commands are executed through a separately permissioned **device bridge**, not the host bash tool.
- `experiment-guard`: intercept `tool_call`, classify operation and phase, reject actions without correct capability, enforce resource lease and attempt budget. A plain substring filter against bash commands is **not** a security boundary. Use OS-level isolation, restricted user/container, tight mounts, and remove unrestricted access to sensitive host paths. The model must not be able to rewrite its guard policy, supervisor, or capabilities in the same execution context.
- `experiment-journal`: append before/after snapshots to a durable log; correlate Pi event IDs, model turns and actual tests.
- `experiment-state`: inject only current objective, known facts, current phase, last experiment, available tools and constraints at startup. Never rely on previous chat history as the authoritative state.
- `recovery-watch`: support session-level bookkeeping, but keep watchdog logic in the independent supervisor.

Do not enable extensions that auto-confirm arbitrary shell commands. Don't use generic root access to the Mac. Limit the agent working directory to a purpose-built project and an isolated build environment. Protect production `dev/*`, `.env`, SSH credentials, Home Assistant tokens, and other service configurations; discovery of those files is not required by this project.

### C. Device bridge (adapters and transport)

Implement adapters behind a common interface:

- macOS USB enumeration: `system_profiler SPUSBDataType`, `networksetup -listallhardwareports`, `ifconfig -a`, `diskutil list`.
- USB Ethernet bridge: detect USB product/VID/PID, network mode (ECM/RNDIS/other), host interface, MAC/IP; if RNDIS is unsupported on macOS, test Linux VM with explicit USB passthrough.
- OpenTom Telnet for interactive commands, FTP for transfers; isolate both to the USB link, disable route leakage, do not assume they are encrypted or authenticated.
- Mounted USB storage (if presented): read-only inventory, file copy, SHA-256, compare.
- Serial UART adapter (future): console capture, known voltage verified with multimeter before connection; do not guess pad pinout or inject 5 V.
- Power cycle mechanism (future): only after actual hardware control is verified. A USB disconnect or software reboot is not a power cycle of a battery-powered device.

Every adapter has `detect()`, `is_available()`, `read_info()`, and optionally `execute()`, `reboot()`, `recover()` with explicit capability reporting. A failed action returns a bounded error rather than hanging.

### D. Persistent research memory

Create a database (SQLite fine) with:

- `facts`: statement, provenance link/screenshot/command, confidence `observed|inferred|hypothesis|rejected`, timestamp, revision.
- `devices`: board ID, kernel, storage mapping, USB interface, current connectivity, last-seen.
- `experiments`: UUID, goal, hypothesis, expected observable, exact command/script/image, preconditions, timeout, outcome, artifacts, next decision.
- `artifacts`: file path, SHA-256, byte count, tool version, provenance, related experiment.
- `builds`: git commit, toolchain digest, kernel config, patches, output hash, boot test status.
- `failures`: signature, recurrence count, first/last occurrence, remediation attempted, cooldown.
- `milestones`: phase, exit criteria, evidence references, completion state.

Use append-only JSONL logs for raw transcripts and filesystem snapshots. Keep a concise `STATE.md` generated from durable data (not freeform speculative agent memory). Save **actual** binaries/logs/hash digests independently of chat summaries.

## Experiment protocol (MANDATORY)

Before every action capable of changing state, write:

```json
{
  "experiment_id":"exp-000001",
  "phase":"discovery",
  "question":"Which medium contains the running boot image?",
  "hypothesis":"A removable SD card contains OpenTom's ttsystem",
  "evidence_needed":["read-only listings", "file hashes", "mount map"],
  "command_or_artifact":"exact operation here",
  "risks":["none beyond read-only access"],
  "timeout_seconds":30,
  "success_criteria":"reproducible observation with source paths",
  "recovery":"abort operation; no storage modifications"
}
```

The supervisor records `planned -> running -> observed|failed|timed_out|interrupted`. The LLM's explanation is not evidence. Evidence must be from command output, immutable artifact, screenshot, hardware measurement, or reproducible build/test output. A test failure is useful if recorded accurately.

**No blind trial-and-error loops:** if the same hypothesis/action fails twice without an identified change in conditions, branch to a different diagnostic strategy. Do not report percent confidence or success rates without an actual experiment basis.

## Phases and exit gates

### Phase 0 — Build the laboratory (device may be offline)

- Inspect existing local Pi, oMLX, model/config, virtualization and USB facilities without altering unrelated setups.
- Initialize private project repository (under `janmus1c` if GitHub integration is available, otherwise local repo ready for push; don't claim remote created without confirmation).
- Implement supervisor, typed experiment journal, capability gate, mock transport and integration tests.
- Test model tool calling, heartbeat, crash restart, session rotation and exact-data recovery with a fake device.
- Exit: crash/recovery integration tests pass; explicit runbook exists; no uncontrolled host privileges.

### Phase 1 — Non-destructive USB + firmware discovery

- Enumerate USB descriptors, interfaces, network devices, mounted volumes; inspect `uname`, `/proc/cpuinfo`, `dmesg`, `/proc/meminfo`, `/proc/mtd`, `/proc/partitions`, `/proc/mounts`, `/proc/cmdline`.
- Inventory `/mnt/sdcard`, `/mnt/movinand`, `/mnt/flash`, `/etc/rc*`, init scripts, `/dev`, loaded modules and peripheral service processes.
- Identify whether OpenTom root runs in initramfs; `root=/dev/mmcblk0p2` is only a kernel argument.
- Retrieve `ttsystem`, boot configuration, kernel symbols/build metadata and bootloader versions where possible; create hashes.
- Back up all accessible relevant storage, including small MTD areas, only after confirming the safe read method. Use checksums and verify backup readability.
- Inspect source/patch provenance and ABI (ARM OABI versus EABI) before choosing toolchain.
- Exit: detailed hardware inventory, raw backups verified, explicit uncertainty ledger, USB access path, image provenance.

### Phase 2 — Reproducible original build

- Locate correct OpenTom revision and patched Linux 2.6.13 kernel; don't assume a modern fork corresponds byte-for-byte to installed artifacts.
- Build a deterministic container/VM toolchain with pinned versions and checksums; begin with reproducing known-good boot image format.
- Static tests for kernel/image structure, filesystem contents and ARM machine compatibility.
- Exit: build instructions, version-pinned toolchain, artifact hashes, meaningful equivalence with known-good software.

### Phase 3 — Reversible first custom boot

- Prove actual boot priority and removable-medium fallback, e.g. by controlled reversible test, before writing any experimental boot image.
- Create the smallest modified userspace that prints custom diagnostic output or draws a splash, then boots.
- Use a device-verified recovery mechanism. If recovery remains unverified, continue all offline development, but do not silently cross to internal flash writes.
- Exit: captured boot logs + visually confirmed UI + reliable next boot after test.

### Phase 4 — Kernel porting ladder

- Start by characterizing original board drivers and Samsung S3C24xx lineage, verifying exact SoC rather than assuming S3C2443.
- Milestones: CPU/MMU -> console -> interrupts/clocks -> RAM -> MMC/SD -> display/framebuffer -> touchscreen -> power/battery -> audio -> GPS -> Bluetooth.
- Test an intermediate version as justified by existing support, then target Linux 5.10 as a research milestone and Linux 6.1 as an ambitious candidate, not guaranteed compatibility.
- Smallest boot proof: kernel log/console + initramfs shell. Graphics and boot robustness before feature completeness.
- For every kernel build, preserve config, patch set, compiler version, artifact hash and exact hardware outcome.

### Phase 5 — Firmware product

- A standalone open-source firmware distribution, minimal init/BusyBox, native touch UI, optional Home Assistant bridge over verified Bluetooth Classic transport, battery and audio support.
- Run kernel/platform regression tests and reproducible build pipeline; publish device flashing/recovery docs only when verified.

## Qwen inference / thinking policy

The model is Qwen3.8-27B-oQ4e-MTP hosted by the user's local oMLX server. Derive model ID, endpoint, tool-call support, reasoning flags and context size from the live server + Pi registry; do not hardcode speculative API options. Use Pi `models.json` OpenAI-compatible configuration if correct for the installed endpoint; verify `GET /v1/models`, single prompt, tool use, and available thinking levels.

Do NOT always run maximum thinking. Implement adaptive stages:

- Observation & repetitive execution: thinking off/low **if the API truly supports this**; short context, strict tool calls.
- Hypothesis selection and multi-file engineering: medium/high thinking.
- Unexpected failures, boot-chain or ABI analysis: temporarily highest **actually supported** thinking level with a fixed token/time budget.
- Judge outcomes by independent tests and evidence, not the agent's self-score.

Benchmark at least 10 representative local tasks with and without thinking and record success rate, speed, valid tool-call rate, and loops. Choose the most reliable configuration; MTP acceleration is not proof of greater correctness. Keep most prompts within a compact working context (suggest starting around 8k–24k effective tokens, benchmark instead of assuming optimal). Provide `STATE.md`, experiment context, and only relevant logs/files to each new session.

## Overnight run policy

- Build + test the harness before leaving it unattended.
- Always keep a device-independent work queue (source inspection, driver mapping, docs, static analysis) so disconnection doesn't halt the entire night.
- Hardware experiment queue is serial and bounded. For an unreachable device: probe with backoff, never continuously hammer USB/reboot/flash.
- Per-action hard timeout; per-experiment attempt budget; per-session token/turn/time budget; limited recovery retries with escalating cooldown.
- No infinite micro-loop: require measurable progress (new verified fact, artifact, test result, resolved uncertainty) to earn further iterations.
- Store a human-readable morning report with: verified findings, inferred claims, tested builds, failed experiments, remaining obstacles, exact device state, git commits, artifact hashes, logs, and the next three experiments.
- A `STOP` file halts scheduling new work promptly; retain enough process control to terminate active work safely.
- Treat actual physical loss of recoverability as a hard blocker requiring a distinct wired debug/recovery method, not something another prompt can solve.

## Deliverables for the first implementation pass

```text
tomtom-revival/
  README.md
  AGENTS.md
  docs/
    HARDWARE.md
    BOOT_CHAIN.md
    TRANSPORT.md
    RECOVERY.md
    EXPERIMENT_PROTOCOL.md
  supervisor/
    ...                  # Pi RPC supervisor, restart loop, state machine
  bridge/
    ...                  # USB discovery + Telnet/FTP + mock transport
  .pi/extensions/
    ...                  # tool guard, device tools, journal, session handoff
  research/
    STATE.md
    FACTS.md
    SOURCES.md
  experiments/
    ledger.sqlite        # generated, gitignored
    events.jsonl         # generated, gitignored
  artifacts/
    backups/             # generated, gitignored
    kernel-builds/       # generated, gitignored
  tests/
    ...                  # mocked crash/timeout/loop/hardware tests
  scripts/
    doctor.sh
    run-discovery.sh
    run-overnight.sh
    stop.sh
    report.sh
```

Provide working commands for: `doctor`, `discovery`, `overnight`, `stop`, and `report`. The initial `overnight` run should stay in `read_only` mode, performing discovery and offline engineering indefinitely until a verified recovery path and explicitly enabled device-write capability exist. Do not add production credentials to the repository.

## First action NOW

Inspect the local Pi/oMLX setup and current workspace, inspect the official Pi interfaces, build the Phase 0 skeleton and mock tests, then perform only passive USB enumeration if the TomTom is physically connected. Create the first `STATE.md` with verified existing observations, all open questions, and links to the evidence. Do not attempt any flash writes or claim a boot test before a device bridge and recovery path have been independently verified. Keep running finite experiments until the current phase's exit criteria are met, pause/reason when stuck, and ensure another fresh process can pick up the project from persisted state without previous conversation context.
