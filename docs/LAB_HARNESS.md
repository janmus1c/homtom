# TomTom revival laboratory

A project-local Pi 1.4.0 harness for the original TomTom GO 630. It uses **only `Qwen3.8-27B-oQ4e-mtp:daily`** through the existing oMLX server at `127.0.0.1:8000`. The external Python supervisor owns credentials, model selection, deadlines, the device bridge, and durable evidence.

```sh
cd path/to/homtom
./scripts/doctor.sh
./scripts/run-discovery.sh
./scripts/run-overnight.sh
```

For a detached overnight process with its output saved locally, run `./scripts/start-overnight.py`. It prints the PID and log path and registers no macOS service. `./scripts/lab once` runs one finite queued objective.

In another terminal:

```sh
./scripts/stop.sh
./scripts/report.sh
./scripts/status.sh
```

`STOP` persists and prevents new work. Remove it to resume: `rm STOP`. Create `PAUSE` to pause scheduling; remove it to resume. The supervisor aborts active Pi work on STOP/PAUSE. An already executing read-only bridge operation has a bounded deadline before it returns. No device-changing operation is implemented.

`status.sh` verifies process identity and reports heartbeat age, the active experiment and the last independently recorded progress. A dead PID is reported as exited, even if old metadata says running. Requested output files must be freshly journaled and hashed before an objective can succeed; the next-experiment JSON must pass protocol validation. These checks establish actual output, not the correctness of every research claim. Human evidence review remains necessary.

A draft may be written at most twice per session even when each revision has different text. This prevents small-rewrite loops from evading command-hash budgets. Routine overnight document tasks use thinking off; the observed high-thinking mode exhausted an entire response budget without producing its required artifact. Output exhaustion is recorded as failure.

The overnight process starts a fresh Pi session for each objective, allows two recovery attempts, and backs off when no new evidence is available. It never restarts oMLX. It exhausts a finite offline queue and then checks for USB changes every two minutes. No-change observations do not earn more model iterations. Mac sleep or USB disconnection is recorded as an interruption; this project does not change energy settings or install a daemon.

## Isolation

Pi starts with a fresh `.runtime/agent` directory, scrubbed environment, no inherited context files, MCP, packages or built-in shell/file tools. Five explicit extensions expose bounded bridge and research tools. macOS `sandbox-exec` restricts the Pi process to project reads, draft/session writes, and one loopback gateway port. The gateway rejects any model except the exact daily ID and keeps the oMLX credential outside the Pi process. A launch-time negative test verifies that policy writes, credential reads and direct model connections fail. There is no unrestricted fallback.

`workspace_test` executes Python unittest discovery under a separate sandbox with no network. Agent draft code cannot modify the supervisor, policy or ledger. General shell execution, Docker access and firmware build toolchains are not exposed to the model. Pinned firmware builds and broader source checkout are later phases, after source/ABI identification.

The draft worker cannot fork. Its per-process CPU limit is 10 seconds, wall deadline 20 seconds and output-file limit 2 MiB. The supervisor samples its RSS every 50 ms and kills it above 512 MiB; macOS rejected an address-space resource limit, so memory sampling is not a kernel-enforced ceiling. These controls apply only to child test processes and do not alter macOS settings.

Credentials live in gitignored `.runtime/secrets.json` with mode 0600, or `OMLX_API_KEY` in the supervisor environment. No external LLM credentials are used. Installed Node 22 and Python 3.12 are reused; nothing is installed globally. `scripts/lab` pins their existing local paths to avoid shell-dependent runtime changes.

## Evidence and current limits

- `research/STATE.md`: generated facts, hypotheses and current experiments.
- `research/MORNING_REPORT.md`: generated run report and artifact hashes.
- `experiments/ledger.sqlite`: facts, devices, experiments, artifacts, builds, failures and milestones.
- `experiments/events.jsonl`: append-only event mirror; SQLite is authoritative after interruption.
- `experiments/*-rpc.jsonl` and `*-stderr.log`: actual Pi transcripts and diagnostics.
- `artifacts/`: passive discovery output, model checks and retrieved files, all local and gitignored.

The original brief is preserved in `docs/BRIEF.md`. UART/power adapters, full raw storage backups, automatic build containers and device writes are deliberately unimplemented. Named unavailable tools return explicit errors. A retrieved `ttsystem` file is not a full flash backup and never unlocks write capabilities. The first physical recovery and boot tests require further hardware evidence.

Run the integration suite with:

```sh
./scripts/test.sh
./scripts/lab smoke
./scripts/benchmark-model.py
```

The smoke run uses real Pi and the real daily model with an explicitly simulated device. The benchmark sends ten representative tool tasks with thinking off and high, preserves raw responses and measures actual reasoning-token behavior. Failed or truncated responses count as failures.
