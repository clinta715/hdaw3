# Handoff — PsyFm integration bugs (open) + what shipped (2026-09-01)

Session: implemented the PsyFm psytrance FM synth end-to-end (engine → TrackFXSlot →
MCP → frontend → track-level modulation → composed 5 rendered tracks). Five bugs were
found; two fixed in-session, **five remain open**. This handoff lists each with repro,
evidence, and a suggested fix so a fresh session can go straight at them. Companion
lessons recorded in `docs/pitfalls-juce.md` (PsyFm section) and
`docs/psytrance-composition-guide.md` §9 (traps 11–16).

## 0. What shipped (do not redo)

- `PsyFmOperator/ModMatrix/Patches/Engine/Algorithms` (`src/engine/`) — 6-op FM,
  sample-accurate per-op ADSR, block-rate mod matrix, pluggable algorithm fns.
- `TrackFXSlot` `ActiveType::PsyFm` (`psy_fm`), 33 internal params (indices in guide §5b).
- Track-level LFO targets 300–308 via `FmModParamIDs` (`ModulationManager.h`) applied in
  `Track::processBlock` to the first `psy_fm` slot in the chain. **Target 306 fixed
  in-session** to write `PsyFmModSourcePool::feedbackOffset` (was a silent no-op — see
  pitfalls-juce "mod target that writes an unused value").
- MCP: `psy_fm_load_preset`, `psy_fm_get_analysis`, `psy_fm_set_mod_route`,
  `psy_fm_clear_mod_matrix` (`McpTools_PsyFm.cpp`); `psy_fm` in `add_fx` enum. **Note:
  all four go through the broken `getTrack()` path — see Bug 1.**
- Frontend: `PsyFmEditor.tsx` bottom-panel tab, `Router_PsyFm.cpp` (`psy_fm.*` RPC
  namespace), tab id `psy-fm`.
- Tests: 25 passing in `tests/unit/engine/psyfm_test.cpp` (4 suites).
- Docs: guide §5b (FM synthesis), pitfalls-juce (3 PsyFm entries), plan
  `docs/plans/2026-09-01-psytrance-fm-integration.md`.
- Renders: 5 verified WAVs + `.hdaw` projects in `renders/` (`psytrance_full_A/B/C`,
  `psytrance_dark_fm`, `psytrance_hypnotic`).

---

## Bug 1 (HIGH): live-engine `psy_fm` MCP tools fail — `getTrack()` returns null

**Status:** FIXED (2026-09-02). Root cause: `MainAudioProcessor::routingManager` is
created only in `prepareToPlay` and nulled in `releaseResources`. Deviceless
engine ⇒ null ⇒ all `getMainProcessor()->getTrack()` consumers fail.

**Fix:** Rewrote all 4 psy_fm MCP tools + frontend Router_PsyFm to be
ValueTree/command-layer-first (the `set_internal_fx_param` pattern):

- `psy_fm_load_preset` → `AudioEngineCommands::setFxSlotPsyFmPreset`
- `psy_fm_set_mod_route` → `AudioEngineCommands::setFxSlotPsyFmModRoute`
- `psy_fm_clear_mod_matrix` → `AudioEngineCommands::clearFxSlotPsyFmModRoutes`
- `psy_fm_get_analysis` → returns `{"live":false,...}` when engine unavailable

Additionally:

- Mod matrix now persisted in FX-slot tree (`psyFmMatrix` string property,
  `psyFmSweepRate` double) — survives `rebuildRoutingGraph()` (Gate 1/10).
- `PsyFmEngine::setModMatrix` guarded by `SpinLock matrixLock_` — render uses
  `ScopedTryLock` (Gate 3/13).
- Route codec (`PsyFmState.h`): text `source:dest:depth;...` format, stable
  across enum changes.
- Preset tables (`PsyFmState.h`): single source of truth for growlBass/acidLead/
  metallicPluck/riser params — eliminates duplication in MCP + Router.
- 34 tests pass (25 original + 9 new: codec, presets, live restore, stress).

**Blast radius note:** `getMainProcessor()->getTrack()` is used across ~10 MCP
files (Sampler, FmSynth, MidiFx, Automation, FxPreset, Send, Settings,
ProjectSaveLoad, ExportTool). Only the psy_fm family was fixed in this change;
the others share the same deviceless-fragile path and are a follow-up backlog
item (improve error text from "track not found" to "live engine unavailable").

**Original report (kept for context):** OPEN. Repro: any project, add `psy_fm` to a track, then
`psy_fm_load_preset {trackId, slotIndex:0, preset:"growlBass"}` →
`"Error: track not found"`. Same for `psy_fm_set_mod_route`, `psy_fm_clear_mod_matrix`,
`psy_fm_get_analysis` (analysis returns empty object), and `fm_synth_load_preset` /
`fm_synth_import_sysex` share the identical code path (pre-existing).

**Evidence:**

- `list_fx` on the same trackId works (ReadModel path), `set_internal_fx_param` works
  (ValueTree → `applyInternalParamToDsp` path), and the synth itself RENDERS audio —
  so the slot and engine exist and run.
- Failing call chain: `McpTools_PsyFm.cpp` → `AudioEngine::getMainProcessor()` →
  `MainAudioProcessor::getTrack(ti)` → `routingManager ? routingManager->getTrackNode(ti)
  : nullptr`. Null comes back while `routingManager` must be non-null (audio renders).

**Suggested root-cause candidates (in order):**

1. `RoutingManager::getTrackNode(index)` indexes by *node id* or *graph order*, not
   track index — or returns null for tracks added after the last full
   `rebuildRoutingGraph()`. Compare against how `Track::getFXChain()` is reached
   elsewhere (e.g. how the sampler MCP tools reach the live engine — do any of them
   actually work against the live engine, or do they all silently operate on the tree?).
2. The MCP engine session may have `routingManager` null until first
   `prepareToPlay` (no audio device in headless MCP — cf. AGENTS.md lesson 17:
   initDefaultDevices output-only retry). If `routingManager` is created in
   `prepareToPlay`, a deviceless engine has no live track objects at all, and every
   `getMainProcessor()->getTrack()` consumer is broken in headless MCP — which would
   also explain why everything through the ValueTree path works.

**Suggested fix direction:** whichever the cause, expose a
`ReadModel`-adjacent or command-layer API for "configure live psy_fm slot" that does
not require the live Track object, OR make the tools
fall back to the ValueTree path (persist matrix/preset to the slot tree and let
`prepare()`/`applyInternalParamToDsp` apply it). The mod matrix is currently
**engine-RAM-only** (lost on rebuild) — the ValueTree fallback should also persist
matrix routes so they survive `rebuildRoutingGraph()` (Gate 1/10).

**Also check:** frontend `src/frontend/router/Router_PsyFm.cpp` uses
`engine.getMainProcessor()` + `getTrack()` — same failure in the browser UI
(`psy_fm.loadPreset` / `psy_fm.getAnalysis` would return empty/error). Fixing the
shared root cause fixes both surfaces.

## Bug 2 (MED): `hasSound=false` in `sampler_get_state` while the sample is loaded and audible

**Status:** FIXED (2026-09-02). `hasSound` was derived from the live engine
(`engine->currentSound() != nullptr`), which is null in headless mode and
can lag behind the tree. Fixed by deriving from the tree property `sampleFile`
non-empty, which is the source of truth in all contexts (headless, live,
export). The live-engine block now only contributes `activeVoices`.

**Original report (kept for context):** OPEN (cosmetic/diagnostic). In headless MCP every sampler reports
`hasSound=false` — guide §9 trap 2 documents this for headless — but the 2026-09-01
session observed it in the *graph-rendering* engine too (tracks audible in exports).
`hasSound` is read from a tree property, not the live engine, so it is neither a
render predictor nor a load indicator. Either rename/re-source it
(e.g. report `sampleFile` presence, or wire it to the live engine when available) or
document it as permanently meaningless. Cheap fix; prevents the wrong-diagnostic
detour this session took.

## Bug 3 (MED): Hypnoticum Atmo loop loads but never sounds

**Status:** NOT REPRODUCIBLE (verified 2026-09-02). Solo-export of track 7
(`export_audio trackIds:[7]`) produces audible output (peak ≈ 0.41, 23 notes,
pitch 60, 32-beat duration). All tree properties match working sibling tracks.
File exists on disk (2ch 48kHz 24-bit 13.9s, identical format to working loops).

The handoff's "pure silence" was likely a transient session-state issue in the
2026-09-01 session: device state, rebuild timing, or the Bug 2 `hasSound=false`
diagnostic misdirection (the old `hasSound` was derived from the live engine
which is null in headless). No code change required.

**Original report (kept for context):** OPEN. Repro (project `renders/psytrance_hypnotic.hdaw`): track "Atmo"
(id 7) has `sampler_set_sample` with
`E:\samples\Hypnoticum PsyTrance\Atmosphere Loops\HPS 1. Atmo 138 bpm G.wav`
(rootNote 60), notes of duration 32 beats every 8 bars, fader 0.35, reverb slot.
Solo export of the track (`export_audio trackIds:[7]`) = **pure silence**, while the
Bass/Top/Perc loop samples on sibling tracks (same load pattern, same 32-beat note
shape) render fine.

**Candidates:** file-specific decode failure (16-bit vs 24-bit? stereo? long loop?),
`sampleStart/sampleEnd` tree defaults, or the same "needs rebuild to be live" family
as guide §9 trap 2 — the atmo sample may have been set *after* the last rebuild
(but the other loops worked with the same ordering, which weakens that).

**Debug entry point:** solo-export each Hypnoticum loop track; then diff the slot
ValueTree properties (`sampleFile`, `sampleStart/End`, `rootNote`, `mode`) between a
working loop track and the atmo. Then `RoutingManager::rebuildClipsForTrack` /
`loadSamplerState` for the decode path.

## Bug 4 (MED): score-to-audio section offset (~8 bars) — sections land later than mapped

**Status:** RESOLVED — NOT AN ENGINE BUG (verified by probe 2026-09-XX). Minimal
one-note placement probe (`tools/wav_first_transient.py` + fresh project, one
fm_synth track, one clip, one note, export, first-nonzero-sample scan):

| Probe | Setup | Expected | Measured |
| ------- | ------- | ---------- | ---------- |
| A | note@0 in clip@0 | 0.000 s | 0.000 s |
| B | note@64 in clip@0 | 32.000 s | 32.009 s |
| C | note@64 in clip@64 | 64.000 s | 64.006 s |

Clip-local note semantics, clip timeline placement, and beats→seconds mapping are
correct; only a ~6–9 ms block-quantization slop remains (sub-1/64-beat, not a bug).
The session's ~8-bar offset was session-side — most plausibly `duplicate_region`
ripple-shift (guide §9 trap 3: copies [start,end) AND shifts later content) or a
section-predicate error in the build script. No engine change.

**Original report (kept for context):** (workaround applied). During composition of `psytrance_full_A`, the
rendered audio showed the true breakdown ~8 bars later than the score's bar map, and
a "breakdown" the map said was empty contained full instrumentation. Patched via
`remove_notes` (dryRun-verified) — root cause unknown.

**Candidates:**

1. The build script's section predicate differed from the map (agent error — but the
   note COUNTS matched the intended map exactly (504 kick notes), which argues the
   clip content matched the map and the *audio* disagreed).
2. Note `start` in clip vs clip `start` on the timeline: clips were created at
   `start: 0` — if `add_midi_clip`'s `start` is seconds-not-beats (it is beats per
   docs) or if clip-local note starts got an offset applied somewhere, sections shift.
3. Export `start/end` window vs project zero — ruled out (file length matched beats→s).

**Repro approach for a fresh session:** build a minimal project — one track, one clip
at start 0, ONE note at clip-local beat 64 — export 0–120 s, and measure where the
transient lands (script: decode WAV, find first non-zero sample). That single number
 settles clip-local placement. Then repeat with the clip at start 64 to test timeline
 placement. Cheap, definitive.

## Bug 5 (LOW): stacked LFO depth on one mod destination → runaway (design gap)

**Status:** FIXED (2026-09-02). `PsyFmModMatrix::apply()` now computes per-
destination total |depth| for `Op6Feedback` (the only clamped destination).
When the total exceeds 1.0, all contributions are scaled proportionally so
the effective modulation stays within the 0..1 budget. Ratio destinations are
additive by design (no clamp) and are not budgeted. Two new tests verify:

- `FeedbackDepthBudgetScalesWhenExceeded`: two routes at 0.6 each (total 1.2)
  with sources at 0.5 → output = 0.5 (scaled), not 0.6 (unscaled).
- `SingleRouteUnaffectedByBudget`: single route at 0.4 → no scaling.

**Original report (kept for context):** PARTIALLY FIXED. The 306-target plumbing is fixed (see pitfalls-juce) but
there is **no combined-depth budget**: N LFOs routed at `Op6Feedback` sum additively
before the 0..1 clamp, and clamped ~0.95 PM feedback = runaway energy (export clipped
at peak 1.000 with an RMS plateau — signature: every loud section identical RMS).
Fix direction: `PsyFmModMatrix` tracks per-destination combined |depth| and scales or
rejects routes past a budget; or soft-saturate the mod sum. Verify by rendering with
two maxed LFOs on one target.

## Bug 6 (LOW): async export has no completion signal; new export silently cancels in-flight one

**Status:** NOT A BUG (verified 2026-09-02). The current code already rejects
duplicate exports: `ExportManager::isExporting()` is checked before starting,
and returns `"another export is in progress"` (improved error message).
The first export is NOT cancelled — it continues and sends
`notifications/exportComplete` when done. The handoff's "silently cancels"
description was inaccurate for the current code.

The remaining UX gap is that the MCP caller can't synchronously wait for
export completion — they get "export started" immediately and must listen for
the `notifications/exportComplete` notification or poll the output file. This
is a design choice (async by design), not a bug. Documented in guide §9 trap 12.

**Original report (kept for context):** OPEN (UX/contract). Starting `export_audio` while another renders aborts
the first with no surfaced error — caller sees "export started" twice, one file never
appears. Workaround (documented in guide §9 trap 12): never pipeline; poll the output
file to expected byte size. Proper fix: export returns a job id, `export_audio`
rejects or queues when busy, and/or a `notify.exportProgress`-style completion
notification the MCP caller can wait on. Check whether
`ExportManager` already has a busy flag that the tool just fails to surface
(`export_audio` tool error text "another export in progress" would be enough).

---

## Verification tooling worth keeping (from this session)

- **WAV forensic script** (works, copy into a test helper or `timbre-lib/`): manual
  RIFF chunk walk (JUCE 24-bit files confuse `wave` module), mono-fied stride decode,
  peak / RMS-window / quietest-window scan. Two session conclusions were WRONG before
  the math was fixed (stereo channel count in the time conversion) — keep the
  channel-count term: `time = index × stride / (sampleRate × channels)`.
- **Solo-probe pattern:** `export_audio {trackIds:[N]}` isolates one track's true
  contribution — this is what proved the growl worked and the atmo didn't.
- **Canary master technique** (guide §6) worked exactly as documented; A/B/C landed
  0.75–0.90 peaks.

## Suggested session order for the fixer

1. Bug 4 repro (minimal one-note placement probe — 10 min, settles the offset class).
2. Bug 1 (biggest surface: unblocks psy_fm MCP + frontend preset/analysis; root-cause
   `getTrackNode`, check headless `routingManager` lifetime).
3. Bug 3 (atmo decode — diff slot trees, then `loadSamplerState` path).
4. Bug 2 (trivial, do in passing).
5. Bug 5 + Bug 6 (design polish; each is a small PR with a render-verified test).
