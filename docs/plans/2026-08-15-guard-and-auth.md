# Lesson-20 Guard + MCP HTTP Auth

Date: 2026-08-15. Items from handoff §3 standing follow-ups.

## Task A: Lesson-20 Permanent Guard

### Goal

Each HDAW instance gets a unique pipe/shm namespace prefix so concurrent
instances (or stale orphans from previous sessions) never collide on
`\\.\pipe\hdaw_plugin_<n>` / `hdaw_plugin_shm_<n>`.

### Design

The `namePrefix` infrastructure already exists:
- `ProxyProcessManager::setNamePrefix()` (ProxyProcessManager.h:99)
- `PluginManager::setProxyNamespacePrefix()` (PluginManager.h:51)
- Export already uses `"export_"` prefix (ExportManager.cpp:105)

Fix: in `AudioEngine::initialize()`, set a unique prefix on the live
PluginManager using the process ID:
```cpp
pluginManager.setProxyNamespacePrefix(
    juce::String::formatted("%x_", juce::Process::getCurrentProcessId()));
```

This produces pipe names like `\\.\pipe\hdaw_plugin_1a2b_1` — unique per
process, deterministic within a session, debuggable (PID visible).

### Success Gates

- G1: Build succeeds.
- G2: Existing proxy/isolation tests pass (no regression).
- G3: The prefix is set (verified by grep/log).

### Changes

- `src/engine/AudioEngine.cpp`: one line after line 56.

---

## Task B: MCP HTTP Auth

### Goal

Add optional token-based authentication to the UiHttpServer so it can be
safely exposed beyond loopback. Currently hardcoded to `QHostAddress::LocalHost`
(UiHttpServer.cpp:70) with no auth.

### Design

1. **Token generation:** at UiHttpServer startup, generate a random 32-char
   hex token (via `std::random_device` + `std::mt19937`). Store it in memory;
   expose via a `getAuthToken()` accessor.

2. **Bind address option:** add `start(quint16 port, const QHostAddress& addr = QHostAddress::LocalHost)`
   — default preserves current behavior. When `HDAW_BIND_ADDRESS=0.0.0.0` env
   var is set, bind to `QHostAddress::Any`.

3. **Auth middleware:** add a `beforeRequest` callback to QHttpServer that
   checks `Authorization: Bearer <token>` header on every request. If the
   token is set and the header doesn't match, return 401. The token is
   injected into the HTML response as `window.__HDAW_AUTH_TOKEN__` so the
   React frontend can pass it to the WebSocket connection.

4. **Token exposure:** the token is logged to `HDAW_LOG` at startup so the
   user can configure their MCP client. For headless/MCP mode, the token is
   passed via `--auth-token=<token>` CLI arg or auto-generated.

5. **Config:** `HDAW_AUTH_TOKEN` env var overrides the auto-generated token.
   If unset, auto-generate. If set to empty, disable auth (loopback-only
   default).

### Success Gates

- G1: Build succeeds.
- G2: Existing FrontendServer tests pass (they use loopback + no auth).
- G3: A request without auth to a token-protected server returns 401.
- G4: A request with correct auth passes through.

### Changes

- `src/frontend/UiHttpServer.{h,cpp}`: bind address param, token generation,
  auth middleware, accessor.
- No frontend changes needed (the token is injected into the HTML; the
  frontend doesn't need to change for the basic flow).
