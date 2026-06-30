# Qt MCP UI Inspector — Roadmap

> **Date:** 2026-06-30
> **Status:** Future (post-v0.3.x)
> **Depends on:** Nothing blocking — can be built incrementally alongside other work

## Goal

Allow an AI agent (Claude, opencode, etc.) to observe and interact with HDAW's Qt UI
for automated testing, debugging, and smoke verification.

## Inspiration

- [0xCarbon/qt-mcp](https://github.com/0xCarbon/qt-mcp) — MCP server for inspecting Qt desktop apps
- [ssss2art/qtPilot](https://github.com/ssss2art/QtMcp) — 53-tool Qt introspection server
- [logos-co/logos-qt-mcp](https://github.com/logos-co/logos-qt-mcp) — QML inspector + test framework
- egui's MCP extension pattern — AI can see/click/interact with the running UI

## Architecture

Optional embedded probe — no injection required since we control the source.

```
AI Agent (Claude/opencode)
    │ stdio MCP
    ▼
MCP Bridge (optional, bridges stdio ↔ TCP)
    │ TCP localhost:9142
    ▼
HDAW App (probe linked at build time)
  ┌──────────────────────────┐
  │  InspectorServer         │
  │  • QObject tree walker   │
  │  • Property read/write   │
  │  • Screenshot capture    │
  │  • Event injection       │
  │  • QTimer-based polling  │
  └──────────────────────────┘
```

Build flag: `-DHDAW_MCP_INSPECT=ON` (default OFF). When enabled, HDAW starts a
TCP listener on localhost:9142 at construction time. When OFF, zero overhead.

## Tools to Expose

| Tool | Description |
|------|-------------|
| `qt_snapshot` | Full widget tree with type, objectName, geometry, visibility |
| `qt_screenshot` | PNG screenshot of a widget or full window |
| `qt_widget_details` | Properties, geometry, children of a specific widget |
| `qt_click` | Click a widget by objectName or coordinates |
| `qt_type` | Type text into a focused widget |
| `qt_key_press` | Send keyboard shortcut (Ctrl+Z, Space, etc.) |
| `qt_set_property` | Set a Qt property on any QObject |
| `qt_invoke_slot` | Call any slot by name (e.g. `onPlayToggle()`) |
| `qt_list_windows` | List all top-level windows |
| `qt_find_by_type` | Find widgets by class name (e.g. `TrackHeaderWidget`) |
| `qt_find_by_property` | Find widgets by property value |

## Phases

### Phase 1: Read-only inspection (1–2 days)

- InspectorServer class (TCP listener on `QT_MCP_PORT`, default 9142)
- `qt_snapshot` — recursive QObject tree walk, return JSON
- `qt_screenshot` — `QWidget::grab()` → PNG
- `qt_widget_details` — property list via `QMetaObject`
- Build flag integration in CMakeLists.txt
- No UI changes — probe is invisible when disabled

### Phase 2: Interaction (1–2 days)

- `qt_click` — translate objectName to QWidget, post `QMouseEvent`
- `qt_key_press` — `QKeyEvent` to focused widget or specific target
- `qt_type` — key-by-key text input simulation
- `qt_invoke_slot` — `QMetaObject::invokeMethod` by name
- `qt_set_property` — `QObject::setProperty` by name

### Phase 3: HDAW-specific tools (1 day)

- `hdaw_get_project` — structured project state (tracks, clips, params)
- `hdaw_get_transport` — play/stop/loop/position
- `hdaw_invoke_action` — call MainWindow slots (onPlayToggle, onAddTrack, etc.)
- These wrap the existing MCP tools but are accessible via the UI inspector path

### Phase 4: Testing framework (future)

- `hdaw_smoke_test` — run a scripted sequence (add track → add clip → play → verify)
- Screenshot comparison against baselines
- CI integration — run smoke tests headlessly

## Integration with Existing MCP Server

HDAW already has `src/mcp/McpServer` (stdio + HTTP transports, 36 tools). The
UI inspector would be a **second MCP server** on a different port (9142), or
merged into the existing server as a new tool domain (`registerInspectTools`).

Recommended: keep them separate. The existing MCP server is for DAW control
(transport, tracks, clips). The inspector is for UI introspection. Different
audiences (LLM driving the DAW vs. LLM debugging the UI).

## Files (estimated)

```
src/inspect/
├── InspectorServer.h/cpp      # TCP listener + tool dispatch
├── InspectorTools.h/cpp       # Tool implementations (snapshot, click, etc.)
├── InspectorBridge.cpp         # Optional stdio↔TCP bridge (for AI agents)
```

~400–600 lines total. No new dependencies beyond Qt Network (already linked).

## Risks

- **Security:** TCP listener is localhost-only, no auth. Acceptable for dev
  tooling; must not be exposed to network in release builds.
- **Performance:** QObject tree walk can be slow for deep hierarchies. Cache
  snapshots, invalidate on `childAdded`/`childRemoved` signals.
- **Thread safety:** All inspector calls must run on the GUI thread (use
  `QMetaObject::invokeMethod` with `Qt::BlockingQueuedConnection` from the
  TCP listener thread).
