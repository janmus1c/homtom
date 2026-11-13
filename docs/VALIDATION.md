# Validation record

Initial setup: 2026-11-01. Full machine-local outputs are under `artifacts/` and `experiments/`; they are excluded from Git.

- Real Pi 1.4.0 RPC + actual local daily-model + explicitly simulated device: completed, one device tool call, `agent_settled` observed.
- Live local oMLX catalog: HTTP 200, exact daily model advertised. Postman ran two assertions with remote event reporting disabled.
- Ten single-turn diagnostic/tool-format tasks per thinking mode: thinking off 8/10 exact-value passes in 8.266 seconds; high 9/10 in 22.378 seconds. Both produced valid tool calls on all ten tasks, with no extra calls. High returned 720 reasoning tokens; off returned zero.
- Benchmark task 7 used potentially ambiguous wording, “Record status unknown”; both modes returned “status unknown” instead of the expected “unknown”. The strict result is preserved as a failure. This small benchmark establishes tool formatting and working thinking controls, not general firmware-engineering reliability or long-session loop rates.
- macOS sandbox launch checks: protected runtime credentials unreadable, capability policy unwritable, workspace writable, direct model-port network access denied.
- Actual restricted Python worker: five checks passed, including denied subprocess creation and network access. CPU/time/output controls apply per child. Memory uses parent RSS monitoring because macOS rejected address-space resource limits.
- Local integration suite covers crash/fresh-session recovery, exact binary preservation, invalid state transitions, repeated-failure budgets, resource leases, capability denials, traversal/symlink denials, hypothesis provenance, gateway authentication, exact model guard, tool-shape validation, `agent_settled` versus `agent_end`, streaming without durable progress and session rotation. See `artifacts/integration-tests.log` for the current result.
- A three-objective real session reached the configured 60000-token session budget and was stopped by the gateway. Scheduling now rotates after every objective. The failure and raw transcript remain in the ledger.
- Physical USB discovery: no positively identified TomTom USB descriptor, network interface or external disk. No device backup, firmware build, boot, UART or recovery test has succeeded or been claimed.

Phase 0 provides the restricted harness, external supervision, mock transport, evidence store and runbook. Full storage backups, firmware build environments and physical recovery remain later gates. New validation must carry actual command output and hashes, rather than overwriting failed results.

The initial overnight run exposed a retry bug: resetting the failure count to zero made the exponential delay a float, which was passed to `range()` and terminated the supervisor. The traceback is preserved in `experiments/overnight.log`. `retry_delay()` now returns a bounded integer for zero or more failures.

Supervision now records a two-second heartbeat, verifies the actual Python process identity, distinguishes denied inspection from an absent process, and preserves final checkpoints on SIGTERM. Completion requires freshly journaled hashes for requested output files; the next-experiment JSON must satisfy the protocol schema. Regression coverage includes missing/stale outputs, tampering, invalid experiment structure, retry-budget reset, output exhaustion, actual signal shutdown and repeated rewrites with changing text. The expanded suite passes 33 tests.

Evidence review found that the first agent draft overstated what procfs queries establish. The limits are documented in `QUERY_LIMITS.md`, and a corrective objective is queued before further planning. Output validation is not a scientific fact checker; source and hardware claims continue to require independent review.

During the monitored correction, a high-thinking response used all 4096 output tokens for reasoning and saved no file. The validator correctly rejected it. Routine document objectives now use the configured thinking-off mode on the same daily model; the experiment records the actual thinking level. RPC output exhaustion is a failure even when Pi subsequently settles.

A subsequent run repeatedly shortened the same draft without new evidence. The bridge now allows at most two writes to each draft per session, independent of content hashes. The correction objective was replaced by a small four-paragraph task rather than another counting/shrinking exercise.
