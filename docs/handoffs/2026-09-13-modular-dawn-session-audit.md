# Session Handoff: Modular Dawn Composition + Engine/MCP/Workflow Audit

**Date:** 2026-09-13
**Sessions:** psy-song-session v1 (Modular Dawn), zero-track migration tail, psy_fm voice-counter fix
**Binary at handoff:** 0.34.0 (psy_fm fix included, not yet committed)
**Project:** compositions/modular-dawn/ (FINAL-v7 checkpoint, render-final-v7.wav)

---

## 1. Bugs Surfaced

### Fixed this session

| # | Bug | Layer | Root Cause | Fix |
|---|-----|-------|------------|-----|
| B1 | psy_fm voice counter never decrements — gain ladder 1/1→1/5 on repeated notes; multi-note synth parts render at 1/N level | Engine (PsyFmEngine.cpp) | noteOn() starts all 6 operator envelopes; the algorithm functions render a SUBSET (acidLead = ops 0+5); the unrendered operators' ADSR envelopes never advance → isActive() stays true forever → the voice reap check never fires → live voices accumulate to kMaxVoices → the polyphony normalization pins at 1/min(N,6) | Gate the voice reap on the CARRIER (op 0) only — it is rendered by every algorithm; when its env decays the voice is inaudible regardless of the mod ops |
| B2 | Same-block noteOn+noteOff = immortal silent voice | Engine (PsyFmOperator.cpp) | juce::ADSR::noteOff() computes releaseRate = envelopeVal / (release × sampleRate). A note born and killed inside one block has envelopeVal == 0 (the ADSR advances only in renderBlock) → releaseRate = 0 → State::release with a zero rate — an immortal silent voice the engine can never reap | Defer the noteOff: pendingNoteOff_ flag set when the envelope hasn't advanced; applied at the end of the block's render (after the keydown phase sounded) |
| B3 | Polyphony normalization steals headroom from the newest voice in a monophonic riff | Engine (PsyFmEngine.cpp render loop) | The old normalization divided EVERY live voice (including release tails) by the total live count — a monophonic riff's newest note sank under its own stacked tails | Held voices share 1/held; release-tail voices keep the scale captured at note-off (Voice::releaseScale) |
| B4 | CC74=100 capture trigger re-tunes NodalRed2x patches — the dark pad turned bright/harsh seconds into the render | MCP protocol | CC74 = CCCutoff on the NL2x. The "harmless" capture-race trigger was actually opening the filter to 100/127 ≈ maximum on every NodalRed2x track (pad, stab, bass). The CC74 was captured into the tree state (captureToTree=true), persisting across saves/reloads | Re-loaded all three NL2x banks (SysEx dumps reset every param to the patch's designed values); captured with CC125 (undefined on the NL2x, truly harmless). The composition guide's audition-workflow recipe needs updating |
| B5 | Zero-track migration incomplete — 42 more tests failed in the frontend RPC tail | Tests | The mass-seed script (fix_tracks.mjs) covered tests/unit/engine + integration/mcp but MISSED: tests/unit/frontend/ghost_clips_rpc_test.cpp, loop_align_rpc_test.cpp, rhythm_generation_rpc_test.cpp, envelope_generation_rpc_test.cpp, frontend_server_test.cpp, tests/unit/model/clip_slicing_test.cpp, tests/integration/mcp/mcp_server_test.cpp (HttpRoundTrip) | Subagent migrated all 7 files (60/60 tests green). FrontendServer needed a seedTrack helper (no shared fixture) + a finding about the seed→fullSync escalation |
| B6 ✅ FIXED | fill_cells pad recipe writes single-note drones — each pad section had 1-4 sequential single pitches, not chords | Generative recipe (AudioEngineCommands_Composition.cpp or the cell-fill path) | The pad cell recipe writes one note per chord slot instead of a voicing stack | Manually rewrote all 8 pad clips as 3-voice chord stacks (F#m-D-A-E progression). The recipe itself is NOT fixed — the next fill_cells will regress |

### Not fixed (route-back / engine follow-up)

| # | Bug | Layer | Evidence | Suggested Fix |
|---|-----|-------|----------|---------------|
| B7 ✅ FIXED | verify_part solo renders SILENT for sampler tracks (kick track 0: soloRms=0, soloPeak=0, audible=0) while the mix context shows contribution (mixRms=0.038, mixPeak=0.267) | Engine (Track.cpp verify path) | Reproduced on kick AND acidvar; the mix-level contribution is real (sub band dominant, kickProminence 0.71-0.82) | Investigate the solo/render seam — the verify path may bypass the sampler's prepared state or the keyRange→note-pitch mapping |
| B8 ✅ FIXED | verify_part window is playhead-relative, not clip-relative | Engine/MCP | acidvar notes at beats 465+ but verify_part {windowSeconds:16} renders from the playhead (beat 0) → silence | Add start/end (or startBeat/endBeat) params to verify_part so a layer can be solo-verified without moving the playhead |
| B9 📋 DOCUMENTED | Isolated children render non-deterministically | Engine (proxy respawn) | v1-vs-v2 residual: >95% samples mismatch beyond gain scaling; RMS varies ~±2%. Children re-spawn with free-running phase | DOCUMENTED as a known limitation: the emulated DSP56300 firmware has free-running oscillator phase — the host cannot reset it without modifying the ROM-locked boot sequence. Documented in docs/realtime-safety.md, the composition guide, and the psy-song-session skill. Gate margins tolerate ±2%; A/B uses spectral properties |
| B10 ✅ FIXED | Master limiter has no ceiling-below-full-scale parameter | Engine (MasterFxDefs.h limiter defs) | Threshold + Release only; ceiling fixed at 1.0. Engaging it can never yield peak < 1.0 — the ≤0.99 gate is unreachable via the limiter | Add a Ceiling param (0.5..1.0) to the limiter defs; the release stage clamps to it |
| B11 ✅ FIXED | MIDI FX + per-track filter automation are INERT in the offline render | Engine (offline render path) | Layer agent A/B: cutoff 1400→2400→100 Hz and different automation curves produced IDENTICAL renders | The offline render path likely bypasses the MIDI FX chain and the automation reader. Route the offline path through the same per-block MIDI/automation processing the live path uses |
| B12 ✅ FIXED | set_sampler_param silently no-ops on unknown properties | MCP tool | set_sampler_param {trackId, slotIndex, param:'keyRange'} returned ok but changed nothing; the real tool is set_sampler_key_range | Validate the property name against the known set; error on unknown instead of silently accepting |
| B13 ✅ FIXED | Seed→fullSync escalation swallows the first mutation after a seed | Frontend (TreeDeltaAccumulator) | Seeding via project.addTrack escalates the accumulator to fullSync for its 16ms debounce window; processEvents() returns early when no events are pending; the seed's burst was never flushed before the clip-add mutation landed under the escalated accumulator (its upsert was discarded) | The frontend_server_test.py documents the workaround (connect client → seed → consume the seed's treeChanged → then mutate). The accumulator should flush the escalation before accepting new events |

---

## 2. MCP Interface Improvements

| # | Improvement | Why | Effort |
|---|-------------|-----|--------|
| M1 | **Single apply_preset tool** that dispatches by file type | 5 different tools (load_nord_bank, load_virus_preset, load_dexed_cartridge, load_plugin_preset_file, fm_synth_import_sysex) with different shapes. The agent has to know which tool matches which file — error-prone | 0.5d |
| M2 | **load_nord_bank with built-in capture protocol** — one atomic call that does bank load + PC + harmless-CC + wait-for-capture | Currently 3-4 manual calls per patch per track, each a race-condition surface (B4 was caused by the manual CC74 step) | 0.25d |
| M3 | **verify_part with start/end beat window** | The playhead-relative window makes it impossible to solo-verify a layer at beats 465+ without moving the playhead. A start/end param fixes the workflow | 0.1d |
| M4 | **Consistent param naming** — set_midi_fx_param uses paramName, set_internal_fx_param uses paramIndex, set_fx_param uses index, set_sampler_param uses param (string, silently no-ops). Pick ONE convention (paramName: string) across all four | Every tool call requires a describe/list round trip to discover the shape | 0.25d |
| M5 | **Embed param ranges in tool descriptions** — lesson 23 says write in real units, but you have to call list_fx_params to discover that cutoff is Hz not 0-1. The tool description should say "cutoff: Hz [20-20000], drive: dB [-24, 0]" | Saves a round trip per param write; prevents lesson-23 violations | 0.1d |
| M6 | **mix_report bands as a named object** — the result carries bands as a parallel array to bandLabels; the consumer has to zip them. Return {"sub": N, "bass": N, "body": N, "high": N} directly | Cosmetic but removes a parsing step from every mix_report consumer | 0.05d |
| M7 | **Verify_part uses the same solo path as export_audio trackIds** — the solo seam (B7) may be a verify-only path issue; making verify_part route through the same render path as export trackIds would fix the silent-solo and remove the seam class | 0.5d (needs investigation) |
| M8 | **Tool registry version tag** — the running engine can lag the committed code (the 0.33.0 engine served during composition). engine_info reports the version; the orchestrator should assert it matches. A --version flag on the engine + a registry-level version check would make drift visible | 0.05d |

---

## 3. Agentic Workflow Improvements

| # | Improvement | Why | How |
|---|-------------|-----|-----|
| W1 | **The orchestrator should verify engine_info.version BEFORE dispatching any role** | The Modular Dawn session ran on a 0.33.0 engine for the whole composition (the 0.34.0 binary wasn't built until later). The load_nord_bank program arg was missing — required a workaround. The orchestrator should assert the version matches the latest commit | A get_project_summary call that echoes the version; assert before phase 1 |
| M/10 | **The mass-seed script needs a DIRECTORY CHECKLIST** — the fix_tracks.mjs script covered unit/engine + integration/mcp but missed unit/frontend, unit/model, unit/common. A checklist of all test dirs (unit/{engine,mcp,common,model,frontend,proxy}, integration/{mcp,proxy}) would prevent the tail | The 42-test tail was found only by a full-suite run | Script improvement: enumerate all test dirs, diff the patched set |
| W1 | **The layer-agent dispatches need the mcp proxy pre-verified** — one dispatch failed with "Unknown MCP server" after an engine restart (the mcporter connection was stale). The orchestrator should call get_project_summary through the proxy before each dispatch, not just after the engine launches | Prevents a wasted dispatch | 1 line in the dispatch preamble |
| W2 | **The Song Brief palette should be written by the Sound Selector directly** (it knows the track indices and the audition evidence) — currently the selector returns a JSON handoff that the orchestrator transcribes into brief.json. The selector should edit brief.json's palette/paletteTrackMap directly (it already has file tools) | Saves a transcription step; eliminates transcription errors | Update the dispatch prompt: "write the palette into brief.json, return only the summary" |
| W3 | **fill_cells' pad recipe should write chord stacks** (root+fifth+third or root+fifth+octave), not single notes | The Modular Dawn pad was 8 clips of single-note drones — the user heard it as "uninitialized synth" | Fix the pad cell recipe in AudioEngineCommands_Composition.cpp (or wherever the fill path builds pad notes): emit a voicing (root, +7 or +3/+4, +12) per chord |
| W4 | **The capture protocol should be baked into load_nord_bank** (M2 above) — the 4-step manual dance (load → PC → CC → wait) is error-prone; the CC74 bug was exactly this dance going wrong | Eliminates the entire B4 bug class | Same as M2 |
| W5 | **The mix Verifier needs a spectral-diff primitive** — "compare the intro before/after this change" is currently export + analyze_tuning + manual comparison. A mix_diff {filePathA, filePathB, section} tool that reports per-band deltas would make A/B verification one call | The pad fix was verified by a manual before/after band comparison | 0.25d |
| W6 | **The role dispatch prompt should include the exact tool-call surface** (the mcp proxy syntax + the tool names) as a copy-paste block — every dispatch required the agent to discover the proxy shape by trial and error | Saves 5-10 tool calls per agent | Template the dispatch preamble |
| W7 | **The layered mode's fixed layer order (kick → bass → perc bed → stab → pad → lead → riser) should be a config option** — for a variation pass (like the acidvar) the orchestrator dispatches ONE layer post-hoc. The dispatch prompt should make clear it's a REVISION, not a first-pass layer | Prevents the layer agent from re-reading the full playbook and re-measuring gates that don't apply to a post-hoc edit | A layerMode: "post-hoc" flag in the dispatch |
| W8 | **The offline render determinism issue (B9) undermines gate margins** — the ±2% RMS variance means gates must tolerate ±2%; a deterministic render mode would tighten the gates to ±0.1% | Enables tighter mixing gates and reliable A/B | Engine-level: reset children's phase at render start |
| W9 | **Per-role factory FX chains should be applied by the Sound Selector** (playbook step 7) — the dispatch prompt should include it explicitly (it was skipped because "not in this task's job list") | The tracks shipped without role-appropriate processing (Kick Punch, Bass Glue, Acid Lead) — the Mix Verifier should not have to compensate | Add to the dispatch prompt's job list |
| W10 | **The PsyFmModMatrix route budget** — two routes of depth 0.6 each with a base of 0.5 produce the same result as one route of depth 1.2 (the budget scales to 1.0 total). This is correct behavior but the test comments document that the budget only matters when sources aren't at max — a route-budget debug/visualization tool would help tune patches | Patch design quality | Future |

---

## Addendum: the CC74 capture-protocol bug (B4) — propagation needed

The composition guide (docs/psytrance-composition-guide.md) documents the audition workflow as:
1. send_fx_midi (CC0 + PC)
2. save_project
3. export_audio → measure

The load_nord_bank tool docs say "optional program sends the trailing PC." The capture-race protocol (trailing CC to re-arm the deferred state capture) was documented with CC74 as the trigger. **All instances of CC74-as-capture-trigger need to be replaced with CC125** in:
- docs/psytrance-composition-guide.md (the workflow recipe)
- docs/skills/psy-song-session/roles/sound-selector.md (the Hardware VA suite section)
- tests/unit/engine/fx_midi_injection_test.cpp (the NordBankLoadChangesNodalRed2xRender test)
- The load_nord_bank MCP tool itself (if the capture protocol is baked in per M2)

---

## Session artifacts

- compositions/modular-dawn/ — the full project (FINAL-v7 checkpoint, layers.json with 14 entries, brief.json, all renders)
- Engine: PsyFmOperator.cpp, PsyFmEngine.cpp/.h (psy_fm fix, committed-pending)
- Tests: psyfm_test.cpp (repeated-notes regression), 7 frontend-RPC files migrated
- Render: render-final-v7.wav (the deliverable)
