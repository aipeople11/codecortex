<!-- SPDX-License-Identifier: MIT; Copyright (c) 2026 Saurabh Verma -->
# OpenTelemetry (OTel) integration — design

Status: **proposal for review, nothing built yet.** Author: Hafeez Izaz. Reviewer: Saurabh Verma.

## 1. Goal

CodeCortex runs a small **local** OTLP receiver. Claude Code on the same laptop sends its own telemetry to it, giving exact tokens and cost per request, live, on the user's dashboard.

**Scope: local only.** Data stays on the user's machine. CodeCortex does not export or forward telemetry to any other system (Grafana, Datadog, company collectors). Off by default; the user opts in.

## 2. Why

| Today | With OTel |
|---|---|
| Tokens known only after the prompt ends (transcript read) | Per request, live |
| Cost estimated from a price table | Cost as reported by Claude Code |
| Answers matched to prompts by time window | Exact link through `prompt.id` |
| Depends on Claude Code's internal transcript format, which can change between versions | Uses a documented, supported export |

The transcript reader stays as a fallback when OTel is not enabled.

**Out of scope:** Claude Desktop chat (sends no telemetry to extensions), Codex and Gemini CLI (event names not yet verified; revisit later), any export off the machine.

## 3. What Claude Code emits (from the [monitoring docs](https://docs.claude.com/en/docs/claude-code/monitoring-usage))

- Enabled by `CLAUDE_CODE_ENABLE_TELEMETRY=1`, `OTEL_LOGS_EXPORTER=otlp`, `OTEL_METRICS_EXPORTER=otlp`, `OTEL_EXPORTER_OTLP_ENDPOINT=<url>`.
- **No default protocol.** We set `OTEL_EXPORTER_OTLP_PROTOCOL=http/json` (gRPC needs HTTP/2, protobuf needs a decoder).
- Claude Code does **not** pass `OTEL_*` variables to MCP servers or hooks, so the installer must write them into Claude Code's settings.
- Events used:
  - `claude_code.api_request`: `model`, `input_tokens`, `output_tokens`, `cache_read_tokens`, `cache_creation_tokens`, `cost_usd`, `cost_usd_micros`, `duration_ms`, `request_id`, `query_source`.
  - `claude_code.tool_result`: `tool_name`, `tool_use_id`, `success`, `duration_ms`, `error_type`, `tool_result_size_bytes`.
  - `claude_code.user_prompt`: `prompt.id`, `prompt_length` (prompt text is redacted by default).
  - Shared: `session.id`, `prompt.id`, `event.sequence`, `event.timestamp`.
- Metrics (`claude_code.token.usage`, `claude_code.cost.usage`) are used only as a fallback if the logs signal is off.

## 4. Receiver

- Listener thread in the CodeCortex process, bound to **`127.0.0.1:4318`** (standard OTLP/HTTP port; override `CODECORTEX_OTLP_PORT`). Loopback only, never configurable to a non-local address.
- Fixed port because Claude Code is configured with one address. **First CodeCortex process wins the port;** others skip. All sessions write to the shared telemetry JSONL, so one receiver serves all. Every process retries the bind every 30 s in case the owner exits.
- Routes: `POST /v1/logs`, `POST /v1/metrics`. `POST /v1/traces` returns 200 and discards.
- `application/json` only; protobuf gets `415` with a hint to use `http/json`.
- Limits: 1 MB body, 64 connections, 200 requests/s, 5 s read timeout. Replies `200 {}` promptly so Claude Code is never blocked.
- If port 4318 is in use by something else, the receiver stays off and Health shows "port busy".

## 5. Privacy rules

1. Off until the user opts in.
2. Read only an **allow-list** of attributes (section 3). Everything else is dropped on read.
3. Always drop `prompt`, `prompt_text`, `response`, `tool_input`, `tool_parameters`, `new_context`, even if Claude Code sends them.
4. Never store `user.email`, `user.account_uuid`, `user.account_id`, `organization.id`.
5. A unit test fails if any content attribute reaches the telemetry file.

## 6. Mapping to existing events

| OTel input | CodeCortex event | Notes |
|---|---|---|
| `api_request` | `TokenUsage` (one per request) | Adds `costUsdMicros`; `cache_creation_tokens` stored in the 5-minute bucket, flagged "5m+1h combined"; `source=host_reported` |
| `user_prompt` | `RunStarted` with `prompt.id` as run id | Exact grouping per prompt |
| `tool_result` for `mcp__codecortex*` tools | enriches the matching `ToolCall` | Host-measured duration and result size. Other tools: name, success, duration only |

**New event fields** (optional, so existing readers are unaffected): `costUsdMicros`, `requestId`, `promptId`.

**Dedupe with the transcript reader:** once OTel events for a `session.id` have been seen, `recordTranscriptUsage` skips that session. The dashboard also dedupes by `requestId`.

## 7. Opt-in install

`codecortex install --host claude --otel`:

1. Reads Claude Code settings. **If any `OTEL_*` setting already exists, leave everything unchanged** and tell the user (their organisation may already collect Claude Code telemetry; Claude Code supports one destination).
2. If managed (enterprise) settings control telemetry, stop and say so.
3. Otherwise writes only: `CLAUDE_CODE_ENABLE_TELEMETRY=1`, `OTEL_LOGS_EXPORTER=otlp`, `OTEL_METRICS_EXPORTER=otlp`, `OTEL_EXPORTER_OTLP_PROTOCOL=http/json`, `OTEL_EXPORTER_OTLP_ENDPOINT=http://127.0.0.1:4318`.
4. Never sets `OTEL_LOG_USER_PROMPTS`, `OTEL_LOG_ASSISTANT_RESPONSES`, `OTEL_LOG_TOOL_DETAILS` or `OTEL_LOG_TOOL_CONTENT`.
5. `codecortex uninstall --otel` removes exactly what it added.

## 8. Files

New:
- `src/telemetry/otlp_json.h`: minimal reader for OTLP/JSON logs and metrics requests.
- `src/telemetry/otlp_receiver.h/.cpp`: listener on `rw::os` sockets, limits, routing, allow-list.
- `test/verify_otlp_receiver.cpp`: sample payloads to expected events; privacy test; oversize, malformed and protobuf rejection.
- `test/fixtures/otlp/claude_code_logs.json`: captured from a real session.

Changed:
- `src/telemetry/events.h`, `collector.cpp`: new fields and serialization.
- `src/host_observer.h`: dedupe rule.
- `src/main.cpp`: start the receiver when enabled.
- `src/ui/server.cpp`: health fields `otlp_receiver: {listening, port, last_event_ms, events_received}`.
- Installer: `--otel` option.
- `CMakeLists.txt`: register the test.

## 9. Dashboard

- Tokens & cost: "Reported cost" (exact, via OTel) with the list-price estimate as a smaller line; per-prompt rows grouped by `prompt.id` with Duration and Requests columns; live refresh.
- Get started: item "Live usage (OTel)" with the install command.
- Health: "OTel receiver" card (listening, port, last event, count).

## 10. Plan

| Phase | Deliverable | Needs |
|---|---|---|
| 0 | Capture real Claude Code payloads into `test/fixtures/otlp/` | 15 min on a dev laptop |
| 1 | Receiver, mapping, dedupe, installer option, tests | C++ build and release |
| 2 | Dashboard changes (section 9) | UI sync |
| 3 (optional) | Protobuf support | Small decoder |

## 11. Risks

| Risk | Mitigation |
|---|---|
| Real payload differs from the docs | Phase 0 capture before coding |
| Content captured by mistake | Allow-list plus failing test |
| Port 4318 busy | Stay off, report it, allow another port |
| Owner process exits | 30 s rebind retry |
| Double counting with transcript reader | Dedupe by session and request id |
| Existing company OTel setup | Installer leaves it untouched |
| Reported cost read as a bill | Label "reported by Claude Code" |
| Windows firewall prompt | Loopback bind; confirm on a Genpact laptop |

## 12. Decisions for Saurabh

1. Local only, no export or forwarding. (Agreed in principle.)
2. Port 4318, first process wins, 30 s retry.
3. Privacy rules in section 5, no exceptions.
4. Add `costUsdMicros`, `requestId`, `promptId`.
5. Hafeez builds, Saurabh reviews.
