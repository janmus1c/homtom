# Local gateway contract

The supervisor binds a random port on `127.0.0.1` for the lifetime of one Pi session. Every route requires the per-session `Authorization: Bearer` token. Pi receives that token; the upstream oMLX key remains in the supervisor. Unknown paths return 404; missing auth returns 401; policy denials return 403; failed operations return 502 with an error string.

| Method and path | Contract |
| --- | --- |
| GET `/v1/models` | Returns only the exact authorized daily model. |
| POST `/v1/chat/completions` | Accepts that exact model ID only; forwards to the fixed loopback oMLX endpoint. Model request count, output tokens and session usage are bounded. SSE is streamed to Pi. |
| GET `/state` | Returns the bounded current device checkpoint (at most 4000 characters) and current capability JSON. Full history stays on disk. |
| POST `/tool` | `{ "name": "device_probe", "params": {} }`; accepts only named typed operations. The bridge records the manifest before executing. Returns experiment ID and actual result. |
| POST `/journal` | Bounded extension lifecycle metadata; stored as `pi_extension` events, never promoted to facts. |

No endpoint changes capabilities, settings, oMLX, routes or macOS services. The gateway is not published, exposed on LAN or stored as a persistent service. HTTP authentication, model mismatch, deadlines and process recovery have local integration coverage; real model streaming has a separate Pi smoke test.

Research requests use nested `chat_template_kwargs` with `enable_thinking=false` and `preserve_thinking=false`, plus temperature 0.7, top_p 0.8 and top_k 20. The gateway enforces those request settings. Input has an independent 48000-byte cap; after upstream usage reports more than 12000 prompt tokens, the session accepts no further model requests. Bytes are not token counts. Output remains capped at 4096 tokens within the conservative 24576-token Pi context configuration. Each objective starts a fresh session with only its declared tools. A research read returns at most 16000 bytes; larger evidence needs a curated excerpt.
