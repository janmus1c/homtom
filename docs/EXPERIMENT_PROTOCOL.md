# Experiment protocol

Every bridge operation records a complete manifest before execution, then transitions `planned -> running -> observed|failed|timed_out|interrupted`. UUIDs and monotonic SQLite IDs correlate immutable outputs, hashes, Pi events and timestamps. A fresh supervisor marks unfinished experiments interrupted before scheduling.

Required manifest fields: phase, question, hypothesis, evidence_needed, exact command_or_artifact, risks, timeout_seconds, success_criteria and recovery. Phase 0 accepts discovery only. Two unchanged failures exhaust an action's budget. Each Pi session also permits at most two identical tool requests and has bounded turns, tokens, model requests and elapsed time.

An observed operation means its output was captured, including an unavailable device or a failed test. It does not mean a hypothesis was proved. Model-written statements remain hypotheses. Tests carry their actual exit code and raw output. Boot results, backups and recovery gates require independently reviewed artifacts.

The external supervisor consumes RPC stdout before sending a prompt, correlates command responses by ID and waits for `agent_settled`. Token streaming does not reset its progress timer. Fresh sessions receive a generated evidence handoff, rather than full conversation replay. STOP/PAUSE prevent further model/tool requests; in-flight read-only calls finish or reach their deadline.
