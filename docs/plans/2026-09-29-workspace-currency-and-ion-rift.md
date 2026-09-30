# Plan — 2026-09-29: agent-workflow currency + `ion_rift` full-on track

**Owner:** orchestrator session (DSH Web GUI, model `deepseek-v4.1-flash`)
**Scope:** (A) bring the agentic song-composition workflow up to date with engine
v0.39.2 / the 2026-09-28 mechanization release; (B) compose, verify and persist one
new full-on psytrance track through that workflow.
**Engine impact:** NONE. No `src/` file is touched by this session; the engine is
used strictly as a service over MCP.

## Goal

A skill + tooling surface that matches the live engine, and one PASS-verdict track
produced by role subagents through it — with machine evidence at every gate.

## Success gates

**Part A — workflow currency**
- [x] A1: every one of the ten mechanization tools (`query_notes`, `query_clips`,
      `set_notes_gain`, `set_clips_edit`, `tool_help`, `whoami`, `verify_window`,
      `render_and_verify`, `begin_batch`, `end_batch`) appears in the role that owns it,
      verified by grep over `.agents/skills/psy-song-session/`. **DONE** — all ten present,
      each in its owning role (270 insertions across 8 files); gate contract now lists
      `verify_window`/`render_and_verify` with both costs.
- [x] A2: every tool name cited in the skill exists in the live `tools/list` (317 tools).
      **DONE** — 317 live; all ten `PRESENT`; a stronger audit over the diff's added lines
      found 56 live tool names and 15 non-tools, every one an arg key/field/enum, no invention.
- [x] A3: `.agents/skills/psy-song-session/**` and `docs/skills/psy-song-session/**`
      are byte-identical (`Get-FileHash` per file). **DONE — structurally**: `.agents/skills`
      is a Windows **junction** to `docs/skills` (`LinkType: Junction`, verified), so the two
      spellings are one file; 11/11 hash pairs match.
- [x] A4: a role can reach the shared engine from a DSH-hosted subagent —
      `python scripts/hdaw_mcp_http.py call <tool>` works, `whoami` reports
      `transport:"http"`, version `0.39.2`, and the shared binary path. **DONE** — plus a
      Windows cp1252 stdout defect found and FIXED (any non-ASCII payload previously crashed
      the client with exit 1 and no output; raw-byte proof of the utf-8 fix).
- [x] A5: `docs/composition-toolkit.md` (the "full overview" AGENTS.md points at)
      documents the mechanization surface, and the composition guide records the
      window/batch/verification rules. **DONE** — toolkit +180/−1 (new "Agent mechanization"
      H2), guide +67 (new §1.5); both carry the `apply_song_brief` `type`-vs-`kind` trap
      (source-verified) and the PowerShell `trackID`/`trackId` casing trap.

### Traps found and closed during Part A (all source- or measurement-verified)

1. **`apply_song_brief` reads only `sections[].type`** (`AudioEngineCommands_Song.cpp:425`):
   a `kind` key in a brief section is SILENTLY ignored (`{"kind":"mainB"}` → `mainA`). Alias
   table `peak→mainA`, `drop→mainB`, `outro→finale`; unknown kind names are REJECTED at the
   trust boundary. Lesson-34 class; now documented in both composition docs.
2. **Windows cp1252 stdout** in the new CLI transport destroyed any non-ASCII payload
   (`tool_help verify_window` died on `→`) — fixed at source, not by env var.
3. **`set_track_volume` is not a tool** (the tool is `set_track`) — stale skill text, fixed.
4. **SKILL.md contradicted `reference.md` + lesson 29** on the timeout exit code (claimed a
   timeout yields exit 42; 42 is the deliberate `engine_restart` tool ONLY) — fixed.
5. **PowerShell cannot parse `tools/list`** with plain `ConvertFrom-Json` (68 of 317 tools
   declare both `trackID` and `trackId`; the parser is case-insensitive and throws) — use
   `-AsHashtable`, or drive the engine through the CLI helper.

**Part B — the track (`compositions/ion_rift/`)**
- [ ] B1: brief pinned (fresh seed, F# minor, 145 BPM, 175 bars, 7 sections) and
      applied to the engine (`apply_song_brief`), with `get_song_plan` read-back
      matching the pinned windows.
- [ ] B2: every palette role has a track whose instrument passed
      `audition_plugin`/`audition_patch` with `audible=true` — sample output, not prose.
- [ ] B3: arrangement complete — no silent gap after the last clip, floor canon
      respected (kick/bass out only in the breakdown), `verify_part` evidence per part
      (`audible=true`, `nonClipping=true`).
- [ ] B4: every sounding track carries modulation; `audit_modulation_coverage` clean.
- [ ] B5: render + `mix_verdict` PASS against `targets` (`masterRms` 0.18 ±5 %,
      `ceilingHitPctMax` 5.0), with the render's duration triaged against
      `expectedDurationSeconds` 289.7.
- [ ] B6: `compositions/ion_rift/ion_rift.hdaw` saved and `ion_rift.wav` exported;
      session report written.

## Dependency map

- **Blast radius (graphify):** `scripts/mcp_call.py` + `scripts/hdaw_mcp_http.py` are
  leaf nodes — the BFS reaches only `probe-c2a/c2a_driver.py` (a sibling driver) and
  `frontend/package.json` (name collision on the word "scripts"). No engine symbol is
  reachable. Docs-only edits plus one new standalone script: zero SPSC / ReadModel /
  routing surface.
- **Upstream:** humans and agents invoke the scripts; `mcp-launch` does not.
- **Downstream:** nothing imports them; the MCP tools they call are unchanged.
- **God nodes in scope:** none. **Communities crossed:** docs ↔ tooling only.
- **SPSC paths touched:** none.

## Pitfall gates triggered

| Gate | Applies? | How it is addressed |
|---|---|---|
| 1/6/10 state-restore on rebuild | No | No processor state added. |
| 2 unimplemented path | Yes (docs) | Every tool named in the skill is verified against the live `tools/list` (A2). |
| 3 audio-thread safety | No | No `processBlock` path touched. |
| 4 stale binaries | **Yes** | The engine under test is the running `HDAW_headless_mcp.exe`; `whoami` reports its path + mtime + size, and `engine_info {expectedVersion}` is asserted before any mutation group. Never `mcp-launch.bat` from a role (it would kill the shared engine). |
| 5/8 frontend | No | Deprecated client; no CSS. |
| 9 id namespaces | Yes (composition) | Palette track indices are pinned by creation order and read back via `list_tracks`. |
| 11/12/13/14/16 | No | No JUCE/audio/proxy code touched. |
| 15 stale flags/binaries | **Yes** | Render duration is triaged against `expectedDurationSeconds`; transport/gate evidence is read from tool output, not prose. |

## Steps

1. A: helper script + skill/docs currency (subagents, parallel).
2. A4/A5 verification + byte-identical mirror check.
3. B1: pin `compositions/ion_rift/brief.json`; apply; read back the plan.
4. B2: Sound Selector (single writer) — tracks, patches, FX chains, modulation, auditions.
5. B3: Arranger (single writer) — plan/cells, layered fills, per-part `verify_part`.
6. B4: FX & Automation Engineer — cross-section movement, modulation audit.
7. B5: Mix Verifier — render, `mix_verdict`, fix-first loop (max 3).
8. B6: save + export + session report; handoff + INDEX row.

## Rollback

Part A: `git checkout -- docs/skills .agents/skills` and delete the new script.
Part B: the composition directory is additive; the engine project is in-memory until
`save_project`, which happens only after a PASS or an explicit checkpoint.
