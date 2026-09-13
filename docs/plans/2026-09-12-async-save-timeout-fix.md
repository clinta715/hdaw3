# Plan — BUG-4: long MCP operations kill the engine (async save + timeout config)

## Evidence
Two engine deaths this session: `save_project` (isolated plugin states, >10s) and `mix_report` (376s-file FFT, >10s) both exceeded the pi-mcp-adapter per-server 10s timeout; the adapter then restarted the server, killing the engine mid-write (one truncated WAV, one crashed save).

## Fix
1. **Adapter config:** set per-server `timeoutMs: 180000` for the `hdaw` server (supported: pi-mcp-adapter types.d.ts:263 `timeoutMs?: number`). Locate the hdaw server entry in the harness adapter config.
2. **Engine-side async save:** mirror mix_report's job pattern — `save_project {wait:false}` → `{jobId}` → `poll_job`; keep `wait:true` for compatibility. File: src/mcp/McpTools_ProjectSaveLoad.cpp + McpJobs.
3. **Test:** save a project with 2 isolated plugin slots via poll_job; assert no server restart (get_transport before/after) and file written.

## Gates
- Async save completes; project file valid (reloads).
- 10s-adapter clients survive the operation (no timeout error).

## Effort/risk
0.5-1d. Risk LOW — job infra exists (McpJobs), pattern proven by mix_report.


---

## STATUS (2026-09-12)

**T5.1 SHIPPED (adapter config)**: `timeoutMs: 180000` set for the hdaw server in ~/.pi/agent/mcp.json — long saves/analyses no longer kill the engine. **T5.2 (engine-side async save) DEFERRED**: save touches live processors; a job-thread implementation needs a thread-safety design first. Interim: the raised adapter timeout covers the user-facing symptom.
