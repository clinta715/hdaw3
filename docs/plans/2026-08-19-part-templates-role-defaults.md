# Plan: Part templates / typed track presets — `role` → defaults map in `addInstrumentPart`

## Goal

Ship handoff agenda item #2: a `role` parameter on `composition.addInstrumentPart`
(and MCP `add_instrument_part`) that turns a **single word into a full typed
preset** — `role: "Bass"` → a bass part (BassLine style, low range, tight
noteDuration, ~-18 dB target RMS) without the caller hand-specifying every
phrase parameter. The engine command holds the `role → defaults` map (scriptable
+ testable, same pattern as agenda #1); RPC + MCP pass `role` through and set an
**explicit-params bitmask** so *explicit params always win* over role defaults.

## Success Gates (all must pass with evidence)

- [ ] G1: New gtest suite `InstrumentPartRole.*` passes:
      (a) `role:"Bass"` with no explicit params produces the **same effective
      configuration** as a hand-configured bass part (BassLine, low range,
      density, velocities, targetRms) — assert on the resulting notes/track, not
      internals;
      (b) **explicit params override role defaults** — `role:"Bass"` + explicit
      `density`/`lowNote`/`highNote`/`style` → those win;
      (c) unknown role (`"Electric"`) → clean validation error, **project
      untouched** (track count unchanged);
      (d) case-insensitive role accepted (`"bass"`/`"BASS"`);
      (e) empty role = legacy behavior (existing `InstrumentPart.*`,
      `VerifyPart.*`, `MasterGain.*` suites still pass unchanged).
- [ ] G2: FrontendServer RPC test: `composition.addInstrumentPart` accepts
      `role` with **no `style`** and round-trips; explicit-wins still enforced
      over the wire. Missing both `style` and `role` → JSON-RPC error
      `-32602 "style required (or provide role)"`.
- [ ] G3: MCP server suite: `add_instrument_part` with `role` (no `style`)
      succeeds; tool schema `required` shrinks to `{"trackName"}`; unknown role
      → clean error result.
- [ ] G4: Full engine suite passes (exit 0; count grows).
- [ ] G5: Diff scan — no anti-patterns: RPC+MCP share ONE engine command path;
      role resolution adds **no processor/DSP state** (Gate 1/10 N/A by
      construction — it only fills `InstrumentPartParams` fields); the role
      table is a file-local static in `AudioEngineCommands_Composition.cpp`
      (no new `.cpp`, no CMake change); no raw `DBG`; existing callers with no
      `role` are behaviorally identical.

## Design

### `InstrumentPartParams` (src/common/ProjectCommands.h, struct at :326)

Two additive fields with defaults (all existing construction sites compile
unchanged):

```cpp
std::string role;            // "" = none; else "bass"|"lead"|"chords"|"drums" (case-insensitive)
uint32_t explicitMask = 0;   // bits set = caller explicitly provided the field
```

Bit constants (exposed in `ProjectCommands.h` for RPC/MCP/test use):

```cpp
enum InstrumentPartRoleBit : uint32_t {
    kRoleBitStyle            = 1u << 0,
    kRoleBitLowNote          = 1u << 1,
    kRoleBitHighNote         = 1u << 2,
    kRoleBitDensity          = 1u << 3,
    kRoleBitNoteDuration     = 1u << 4,
    kRoleBitMinVelocity      = 1u << 5,
    kRoleBitMaxVelocity      = 1u << 6,
    kRoleBitTargetRms        = 1u << 7,
    kRoleBitAllowGlobalScale = 1u << 8,
};
```

Only the 9 role-defaultable fields carry bits. The other params
(`trackName`, `pluginId`, `programIndex`, `lengthBeats`, `placement`,
`startBeat`, `count`, `scaleRoot/Mode`, `seed`, `windowSeconds`, `verify`)
are never touched by a role.

### Role defaults table (file-local static in AudioEngineCommands_Composition.cpp)

| Field | Bass | Lead | Chords | Drums |
|-------|------|------|--------|-------|
| style | `BassLine` | `Lead` | `ChordStab` | `Euclidean` |
| lowNote | 36 | 60 | 48 | 36 |
| highNote | 48 | 76 | 72 | 60 |
| density | 10 | 6 | 5 | 12 |
| noteDuration | 0.5 | 0.25 | 2.0 | 0.25 |
| minVelocity | 70 | 70 | 60 | 90 |
| maxVelocity | 110 | 110 | 100 | 120 |
| targetRms | 0.126f (≈ -18 dB) | 0.0f | 0.0f | 0.0f |
| allowGlobalScale | true | false | false | false |

Engine `addInstrumentPart` (AudioEngineCommands_Composition.cpp:417) gains,
**before** any mutation (Gate 9 discipline):

1. Normalize `role` to lowercase; if non-empty and not in
   `{bass, lead, chords, drums}` → `result.error = "unknown role: " + role;`
   (returns before `addTrack`).
2. If `role` empty AND `style` empty → `"style or role required"` (clean error
   for the relaxed-MCP path; `styleFromName("")` alone would read
   `"unknown style: "`).
3. If `role` non-empty: for each of the 9 role-defaultable fields, **if its
   explicit bit is NOT set**, assign the role default. Style is guaranteed
   non-empty afterwards (every role supplies a style).
4. Continue the existing pipeline unchanged (style validation, placement,
   bounds, one transaction, one `rebuildRoutingGraph()` — single undo unit
   preserved).

`targetRms` note: `role:"Bass"` defaults to gain-staging (~4 s window render).
Callers who want the part without the render pass explicit `targetRms: 0`
(explicit bit wins). This is the documented cost of a "finished" bass preset.

### RPC — `composition.addInstrumentPart` (Router_Composition.cpp:267)

- `style` changes from `requireString` → `optString(o, "style", "")`.
- New `role = optString(o, "role", "")`.
- Guard: `if (p.style.empty() && p.role.empty())` → `makeError(-32602,
  "style required (or provide role)")` (preserves the old missing-style
  contract shape).
- Set explicit bits: for each of the 9 fields,
  `if (o.contains("density")) p.explicitMask |= kRoleBitDensity;` etc.
- Response unchanged (role is request-side only; no new response field).

### MCP — `add_instrument_part` (McpTools_Project.cpp:1045)

- Schema: add `{"role", {"type","string", "description": ...}}` (plain string;
  engine is the single validator — tolerates case, documents accepted values);
  `required` array shrinks from `{"trackName","style"}` to `{"trackName"}`.
- Handler: read `role`; set explicit bits from `a.contains(...)` for the 9
  fields (match the existing `a.contains("pluginId")` presence pattern).
- `style` stays a valid optional field when provided.

### Why a bitmask instead of "compare against struct defaults"

Presence tracking is the honest "explicit wins". Comparing field values against
the struct default is ambiguous: `density: 8` explicitly passed equals the
default and would be silently overridden by a role's density. The mask makes
the caller's intent explicit. RPC/MCP set bits from key presence
(`o.contains` / `a.contains`); engine unit tests set bits directly; existing
callers that never set `role` are unaffected (mask ignored when `role` empty).

## Blast radius (verified via grep + codebase-memory graph)

Additive-only struct change; behavior changes **only** when `role` non-empty.

| Site | Change |
|------|--------|
| `src/common/ProjectCommands.h:326` | `+role`, `+explicitMask`, `+enum InstrumentPartRoleBit` |
| `src/engine/AudioEngineCommands_Composition.cpp:417` | role normalization + defaults application + `style or role required` guard |
| `src/frontend/router/Router_Composition.cpp:267` | `role` optString, style→optional, explicit bits, -32602 guard |
| `src/mcp/McpTools_Project.cpp:1045` | `role` schema+handler, `required`→`{trackName}`, explicit bits |
| `tests/unit/engine/instrument_part_test.cpp` | new `InstrumentPartRole.*` suite |
| `tests/integration/mcp/mcp_server_test.cpp:770` | role parity test |
| `tests/unit/frontend/frontend_server_test.cpp:654` | RPC role round-trip test |
| `verify_part_test.cpp`, `master_gain_test.cpp` | compile-only (struct additive) — no edit expected |

No new files → no CMake change. No audio-thread / DSP / plugin code touched →
latency & fidelity evaluation (lessons 7, 8) N/A; Gate 1/10 N/A by construction.

## Anti-pattern scan

- No per-clip rebuild — role fills params before the existing single
  `rebuildRoutingGraph()`.
- No new processor state lacking a restore path (none added).
- No duplicated command path — RPC/MCP both call `addInstrumentPart`.
- No raw `DBG`. Style-name mapping reuses `styleFromName` (line 22).
- `role` name has no collision (grep: only `ArrangementGenerator`'s unrelated
  `TrackRole`).

## Implementation tasks (subagents, sequential)

1. **Task A (engine + unit tests)** — `ProjectCommands.h` struct+enum;
   `AudioEngineCommands_Composition.cpp` role table + normalization +
   application; `instrument_part_test.cpp` `InstrumentPartRole.*` suite
   (G1 a–e). Verify: `cmake --build build --config Debug` then
   `build/Debug/hdaw_tests.exe --gtest_filter=InstrumentPartRole.*` and the
   existing `InstrumentPart.*` / `VerifyPart.*` / `MasterGain.*`.
2. **Task B (RPC + MCP + tests; depends on A's struct)** —
   `Router_Composition.cpp`, `McpTools_Project.cpp`,
   `mcp_server_test.cpp`, `frontend_server_test.cpp` (G2, G3). Verify:
   build + `--gtest_filter=McpServerTest.*AddInstrumentPart*` + the
   FrontendServer addInstrumentPart RPC test.

Orchestrator (this session) runs Gate G4 (full suite) and G5 (diff scan) after
both tasks. No frontend UI change (grep: no frontend caller of
`addInstrumentPart`).