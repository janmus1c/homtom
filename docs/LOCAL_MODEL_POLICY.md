# Local model operation

The worker uses only `Qwen3.8-27B-oQ4e-mtp:daily` through the existing loopback oMLX service. No server settings, global packages or macOS services were changed.

## Research applied

The following Learn files were read on 2026-11-01:

- `<workspace>/llm/learn/docs/PROMPTS.md`
- `<workspace>/llm/learn/docs/research/prompting-local-models.md`
- `<workspace>/llm/learn/docs/research/prompting-local-models-a.md`
- `<workspace>/llm/learn/docs/research/qwen38-omlx-community.md`
- `<workspace>/llm/learn/apps/server/src/tutor/pi.ts`

Learn distinguishes vendor guidance, reported measurements and untested recommendations. Its prompt-length budgets are starting points, not established optima for firmware work. The installed Pi documentation independently confirms the current `chat-template`, `chatTemplateKwargs`, sampling and streaming-usage settings.

## Corrections

The old harness selected Pi's `qwen` format, which sends a top-level thinking switch. It also injected the entire growing STATE report before each objective and exposed all 13 tools, including unavailable hardware operations. Reads could return 512000 bytes. These choices wasted attention and made context growth harder to inspect.

The gateway now enforces nested `chat_template_kwargs` with thinking and historical thinking disabled. Sampling uses temperature 0.7, top_p 0.8 and top_k 20. No penalty or server reconfiguration was introduced.

Each objective starts a fresh Pi session with a short prompt from `config/prompts/research.md`, a current device checkpoint and two or three relevant tools. The bridge also enforces that tool set. Full history stays in SQLite and artifacts. The worker writes durable results under `workspace/`; the supervisor validates freshness and hashes.

## Context and supervision

Pi uses a conservative 24576-token context configuration. The gateway rejects inputs above 48000 serialized UTF-8 bytes and stops accepting model requests after upstream usage reports more than 12000 prompt tokens. Bytes are an independent limit, not an exact token estimate. Research reads are capped at 16000 bytes; larger source material needs a curated excerpt. Output is capped at 4096 tokens.

`./scripts/status.sh` shows the latest actual token usage, request byte count, tools, heartbeat and active experiment. Request, turn, progress and session limits remain enforced externally. One objective per session avoids transcript accumulation; an interrupted objective retains its artifacts. The configured overnight deadline is recorded in `config/lab.json`.

## Verification

All 36 harness tests passed, including nested thinking enforcement, input and observed-token limits, tool scope and bounded checkpoint delivery. Extension lint passed.

Three sequential live daily-model evaluations extracted four hardware facts and wrote validated JSON through the real Pi tools. All three passed in 6.71, 10.36 and 6.81 seconds. The largest reported prompt was 1936 tokens; all requests reported zero reasoning tokens. Evidence: `artifacts/learn-alignment-eval.json` and `artifacts/learn-alignment-tests.log`.

These runs verify the small extraction workflow and request settings. They do not measure long-horizon engineering quality. The worker's real image-parser and module-validation tasks provide the next checks. Firmware claims still require independent build and hardware evidence.
