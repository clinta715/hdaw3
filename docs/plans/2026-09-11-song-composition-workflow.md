# Plan: Song Composition Workflow — Plan/Cell Model for MCP + UI

Status: COMPLETE (2026-09-11) — Phases A-D shipped with green gates.
Phase D: Song Plan panel (Compose tab mode 6) — plan editor, section templates,
cell matrix (add/lock/reroll/provenance/fill); mix_report fromPlan; sketch-only
labeling on the four song-level generators; seed echoes on generate_phrase/chord/
progression (additive — "clipId=" text shape preserved); "Phrase Generator"
renamed "Compose" across button/tests/e2e; guide §3 tool-map rows + §4 plan/cell
workflow; psy-song-session SKILL + Arranger role updated; AGENTS.md generative
section + tab list updated. Deferred by design: full JSON-envelope conversion of
FULLY DONE — all composition tools (generate_phrase/chord/progression/
rhythm now return the unified {clipId, noteCount, seedUsed?} JSON envelope;
coverage/server assertions updated in the same change; parseClipId already
handled JSON). DONE (follow-up 2): energy arc shipped — export.temporaryRender +
audio.mixReport {fromPlan} RPCs (both reuse existing pipelines: dispatch-
forwarded export, engine MixReportAnalyzer) + panel "Check energy" with one
RMS bar per section (peak >= 0.99 -> red) and peak/kick/pump readouts. No
deferrals remain in Phases A-D. DONE (follow-up session): Song Plan panel style/pattern pickers
(source-aware, patch the params JSON; pattern list from composition.listPatterns).
Phase A: Compose tab shipped (modal retired, auto-close gone; 67/67 vitest, 15/15 e2e).
Phase B: SONG_PLAN + section-typed regions + templates + brief apply/export shipped (MCP + RPC; 7/7 focused, brief-apply = full-replacement semantics).
Phase C: cells (phrase/rhythm/break/pattern/harvest) + fillCells/reroll/locks + clip provenance + composition.generateChoppedBreak RPC parity route shipped; derived seeds are 53-bit (exact through the double/JSON transport). generate_psytrance echoes seedUsed; remaining generate_* envelope retrofit deferred to Phase D (avoids breaking existing tool response shapes mid-stream).
Scope: command/model/MCP/UI layers. NO processBlock / DSP / routing / plugin-contract changes.

## Goal
Make the established composition workflow — deterministic section skeleton,
seeded probabilistic per-section content, provenance, verification — a
first-class state model shared by MCP tools and a new Compose tab, replacing
the ad-hoc per-call generator workflow and the self-closing PhraseGenerator
modal.

## Verified background (evidence)
- PhraseGeneratorDialog is a floating overlay (pgd-overlay/pgd-dialog, App-idiom
  violation) that auto-closes 400ms after generate (FXChain.tsx sibling file,
  frontend/src/components/PhraseGeneratorDialog.tsx:389-393,313).
- Bottom tabs are a plain array in App.tsx (~L217-233); BottomTabs takes
  {id,label,content}. Adding a tab is trivial; active tab is internal state
  (needs lifting to uiStore for shortcut-driven switching).
- No song-level UI state exists (uiStore has only showPhraseGenerator).
- Song Brief schema (docs/skills/psy-song-session/brief.schema.json) already
  models sections/seed/palette/targets — as an agent-side file only.
- Canonical section kinds: PsytranceSectionKind (PsytranceGenerator.h:21-30:
  Intro, Build, MainA, Mini, MainB, Breakdown, Finale, Other) with
  case-insensitive kindFromName. Brief's 5-name enum maps onto it
  (intro→Intro, build→Build, peak→MainA, breakdown→Breakdown, outro→Finale).
- Arranger regions exist (store + UI + project.* RPC) with name/bounds but no
  section type; generate_psytrance already parses a sections[] table (beats).
- Result contracts are mixed (26 McpToolResult::text vs 11 QJsonDocument in
  McpTools_CompositionGenerate.cpp); mix_report wants SECONDS while plans are
  in BEATS (units trap); no tool echoes seedUsed; no re-roll verb; no
  section-template library; generate_chopped_break is MCP-only (no RPC).

## Design decisions (pinned — implementers do not redesign)
1. Canonical section type = PsytranceSectionKind. All surfaces (brief, plan,
   regions, UI, MCP) speak these names via kindFromName.
2. Single source of truth: a SONG_PLAN root ValueTree node (bpm, keyRoot,
   scaleMode, style, masterSeed, totalBars, targets) + section TYPE stored on
   arranger regions (new property). set_song_plan writes/updates regions via
   the existing arranger commands (never a parallel model).
3. Cells (recipes) are SONG_PLAN children: {sectionName, role, sourceKind:
   phrase|rhythm|pattern|harvest|break, params (JSON, existing generator param
   structs), seed, locked}. Recipes only — generated content stays normal clips.
4. Provenance = properties on generated clips (genTool, genSeed, genParams
   base64 JSON, genSource). Written by the command layer at generation time.
5. Section templates = JSON files following the fx-chain preset / userLibrary
   file convention (reuse that storage helper; do not invent a new store).
6. New commands are ADDITIVE virtuals on ProjectCommands (AudioEngineCommands
   is the sole implementor — the compile proves it). RPC namespace
   composition.*. MCP tools mirror 1:1 (snake_case) in a NEW
   src/mcp/McpTools_SongPlan.cpp which MUST be added to the CMake source list
   and registerAllTools.
7. Uniform result envelope for every new/retrofitted composition tool:
   {ok, clipIds[], section?, role?, seedUsed, provenance, warnings[]} as
   compact JSON. Text responses are retired in these tools only.
8. Undo: each set_song_plan / fill_cells / reroll is ONE undo unit
   (beginTransaction/endTransaction, loadVirusPatch precedent).
9. Plan is BEATS everywhere; mix_report seconds conversion happens internally.

## Phases (in execution order) — each is one coherent dispatchable task

### Phase A — Compose tab (UI only; no engine changes) — DO FIRST
Extract the 4 dialog modes into frontend/src/components/compose/ panels; add
"Compose" entry to bottomTabs in App.tsx; lift active bottom-tab id into
uiStore (additive) so TransportBar 🎵 / Ctrl+Shift+G opens the tab; delete the
overlay dialog + CSS; migrate PhraseGeneratorDialog.test.tsx to ComposeTab
tests + update affected E2E specs.
Gates: vitest + affected e2e pass; no pgd-overlay in DOM; shortcut opens tab;
all four modes send byte-identical RPC payloads as before (payload parity).

### Phase B — Plan state + templates + brief (engine + RPC + MCP) — KEYSTONE
Engine: SONG_PLAN node + sectionKind property on regions + ProjectCommands
virtuals: setSongPlan, getSongPlan, saveSectionTemplate, loadSectionTemplate,
listSectionTemplates, applySongBrief, exportSongBrief (AudioEngineCommands
impl in a new AudioEngineCommands_Song.cpp added to CMake). ReadModel: plan
snapshot (sections with beat ranges + kinds + cells). Router_Composition
dispatch. McpTools_SongPlan.cpp with set/get_song_plan, section-template
CRUD, apply/export_song_brief. Tests: engine plan persistence (save/load +
rebuild keeps regions/kinds), template round-trip, brief apply/export round-
trip; mcp_server_test coverage; region sync = regions match plan exactly.
Gates: mechanical grep proves all layers; focused gtests pass; one undo unit
per set; fullSync on plan changes confirmed correct.

### Phase C — Cells + envelope + provenance (engine + MCP)
Commands: setCellRecipe, getCells, fillCells (mode all/unfilled/unlocked; ONE
transaction; windows derived from plan sections), rerollCell(s) (seed+delta),
getClipProvenance; generateChoppedBreak RPC route (closes the parity gap).
Clip provenance properties written by the generation command path. Retrofit
the generate_* MCP tools in McpTools_CompositionGenerate.cpp to the uniform
envelope (psytrance gains seedUsed + plan echo). McpTools_SongPlan.cpp gains
set_cell, fill_cells, reroll, get_cells, get_clip_provenance. Tests: window
math per section (beats), lock respected, reroll changes seed deterministically,
provenance present on clips + ReadModel, envelope shape on every composition
tool (assert in mcp tests).
Gates: focused gtests pass; fill of the guide v4 plan produces sections at the
exact beat ranges; no clip created outside a cell window.

### Phase D — UI matrix + verification + consolidation/docs
ComposeTab gains the matrix (sections × palette roles) driven by the cell RPCs
(Generate / Re-roll / Lock / Clear per cell; "Generate all unlocked" = one
fill_cells call; sketch button = generate_psytrance respecting plan). Inline
per-section energy arc: mix_report gains sections:"from_plan" (beats→seconds
internally) or thin verify_song_plan; arc rendered vs plan targets. Tool
re-descriptions: song-level generators marked sketch-only, descriptions route
to the plan/cell workflow. Docs: guide §4 rewrite around the workflow,
psy-song-session role updates (Song Brief ↔ plan), AGENTS.md pointer.
Gates: matrix E2E (fill a 3-section plan, reroll one cell, verify arc);
vitest; docs updated; graphify refresh.

## Dependency map (graphify + codebase-memory, verified this session)
- Communities crossed: UI (App.tsx/FXChain communities 15/35), command layer
  (AudioEngineCommands 0/18, God nodes AudioEngineCommands ~248 / ProjectCommands
  ~201 edges — ADDITIVE ONLY), ValueTree model, MCP, ReadModel.
- Projections: ValueTree (truth) → ReadModel snapshots (plan/cells/provenance)
  → arranger store sync → frontend stores. Plan/cell mutations are non-clip/
  track entities → existing fullSync path (correct, no delta work).
- SPSC/audio-thread: untouched. No plugin isolation impact.

## Pitfall gates (16-scan)
- G1/6/10: plan/cells/provenance persist in ValueTree; save/load + rebuild
  tests assert via ReadModel + regions (no DSP state involved — live-processor
  assertions not applicable; state the distinction in test comments).
- G2: trace each new RPC/tool to ValueTree effect; mechanical greps per phase.
- G3/13: n/a (no audio-thread or DSP writes).
- G4/15: build via build-fast.bat (+ time-sync), binary string checks per fix.
- G5: ComposeTab — complete hook deps; after await read uiStore.getState()
  (selected track/section may have changed).
- G8: design tokens only; no raw hex.
- G9: validate section names against the plan; sanitize template file names;
  clamp recipe params through existing def clamps.
- G14/16/11/12: n/a (no proxy, no plugin lifecycle, no new entry points, no
  graph mutation off-thread).
- Anti-patterns: fill_cells is one batched call (never N RPCs from the UI or
  agent loops); one undo unit per plan/fill; no full-tree scans (indexed
  access); no new raw CSS.

## Completion contract
Every phase: gates pass with evidence (test output, greps), affected suites
run (mcp_server/mcp_coverage + vitest + e2e for A/D), build verified via
binary, docs + MCP parity table updated (each UI verb ↔ tool), graphify
refreshed. Phases ship in order A → B → C → D; B+C may share one dispatch if
kept coherent, otherwise B lands before C starts.
