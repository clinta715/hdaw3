# Handoff: 7 test failures + 3 engine bugs fixed (2026-08-23)

## Context

Continued from `2026-08-23-voltage-dnb-bugs-streamlining-shipped.md`. Fixed all 7
failing tests in the full suite (1055 tests), discovered and fixed 3 engine bugs
in the process. Pure engine + test work — no frontend changes.

## What shipped

### Engine bugs fixed (3)

| # | Bug | Root Cause | Fix | Files |
|---|-----|------------|-----|-------|
| 1 | `getEgLevel` always ~0 | `Exp2::lookup` returns Q24 but divided by `0x7fffffff` (Q31) → ~2⁻⁷ of true value | Divide by `1<<24`, clamp to [0,1] | `msfa/dx7note.cc:389` |
| 2 | Clip-id collision after offline render | `nextClipID` was a process-global static; `ExportManager`'s `ProjectModel localModel` ctor resets it to 1 on every render → next `addMidiClip` reuses id 1 → notes land on wrong clip | Per-instance `std::atomic<int>` members in `ProjectModel`; ~15 caller sites updated | `ProjectModel.{h,cpp}`, `AudioEngineCommands_{Clips,Composition,GhostPaint,Envelope,Midi}.cpp`, `McpTools_Project.cpp`, `note_id_test.cpp` |
| 3 | `addAutomationLane` errors on idempotent "ensure exists" | Handoff fix #4 made ANY same-name lane error; every track ships Volume/Pan/Mute lanes by default | Same-name + no paramID conflict → idempotent success (return true); genuine conflicts → false | `AudioEngineCommands_Automation.cpp` |

### Test failures fixed (7/7)

| # | Test | Root Cause | Fix |
|---|------|------------|-----|
| 1 | `FmSynthTest.AnalysisCapturePopulatesOpLevels` | Bug #1 (Q24/Q31 divisor) | Engine fix |
| 2 | `GlobalScale.ClippedMixScaledDown` | v0.24.0 gain restructure lowered single-part peak from ~1.13 to ~0.28 | Stack 4 Lead parts (mirror `McpServer.AutoGainGlobalScale`) |
| 3 | `VerifyPart.ComposedPartPasses` | Default FM patch has 0.0154% HF energy (88% low, 11.5% mid) — true positive of band check | Assert low+mid present, bandHigh false; add deterministic HF-present test |
| 4 | `FrontendServer.AutoGainGlobalScaleRpc` | Same as #2 | Stack 4 Lead parts |
| 5 | `EnvelopeGenerationRpc.G5_GenerateAutomationEnvelope_HappyPath` | Bug #3 (addAutomationLane collision) | Engine fix |
| 6 | `EnvelopeGenerationRpc.G5_InvalidShape_ReturnsError` | Same as #5 | Engine fix |
| 7 | `EnvelopeGenerationRpc.G5_DefaultsApplied` | Same as #5 | Engine fix |

### New tests added (2)

| Test | File | Purpose |
|------|------|---------|
| `VerifyPart.AudioClipWithHfContentHasBandsPresent` | `verify_part_test.cpp` | Deterministic HF-present path via generated WAV (100 Hz + 5 kHz) |
| `VerifyPart.OfflineRenderDoesNotClobberClipIds` | `verify_part_test.cpp` | Regression: compose → verifyPart (offline render) → compose → assert unique clip ids and notes on correct clip |

### Test results

1046 passed, 1 failed (pre-existing `PluginIsolation.LiveDropDrainsStaleOutput` — unrelated isolation test), 8 skipped (real plugin tests), 6 disabled.

## Key findings

### The clip-id bug (Bug #2) was systemic

`ProjectModel::allocateClipID()` used a process-global `static std::atomic<int>`.
Every `ProjectModel` construction (including `ExportManager.cpp:206`'s render-thread
`localModel`) called `createDefaultProject()` → `resetClipIDCounter()` → reset the
global to 1. This meant ANY offline render (verifyPart, autoGainToTarget, audition,
export) clobbered the live project's id space. The next `addMidiClip` would reuse
id 1 → `addNote`'s `getChildWithProperty(clipID)` lookup silently targeted the
WRONG (older) clip → notes vanished from new parts.

This bug affected the Voltage DnB composition session's compose→verify→compose
workflow and was likely the root cause of several "silent note" bugs reported there.

### The getEgLevel bug (Bug #1) was masked by the old gain structure

`Exp2::lookup` returns Q24 (documented in `exp2.h`). The old op output level 99
pushed the envelope above unity (2.0 linear) → `2.0 / 2^31 = 0.00093` — barely
above the test's 0.01 threshold. When v0.24.0 lowered output to 60, the reading
dropped to 0.00053 → below threshold → exposed the latent bug. The frontend's
FM op-level display was always showing ~0 (dead meters).

### The bandHigh threshold is correct

Measured: Standard seed 7 FM part = 88% low, 11.5% mid, 0.0154% high. The 0.5%
threshold correctly identifies the part as lacking HF content. The default FM patch
(all ops ratio 0.5, output level 60) is fundamentally dark — carriers at/below mid
frequency, weak modulator sidebands. This is a true positive, not a miscalibration.

## Files changed

### Engine (src/)
- `src/engine/msfa/dx7note.cc` — getEgLevel Q24 divisor fix
- `src/model/ProjectModel.h` — per-instance ID counters (static → instance members)
- `src/model/ProjectModel.cpp` — deleted static atomics, updated allocate/reset/scanAndSync
- `src/engine/AudioEngineCommands_Automation.cpp` — addAutomationLane idempotent semantics
- `src/engine/AudioEngineCommands_Clips.cpp` — allocateClipID caller sweep
- `src/engine/AudioEngineCommands_Composition.cpp` — allocateClipID/noteID caller sweep
- `src/engine/AudioEngineCommands_GhostPaint.cpp` — allocateNoteID caller sweep
- `src/engine/AudioEngineCommands_Envelope.cpp` — allocateCcID caller sweep
- `src/engine/AudioEngineCommands_Midi.cpp` — allocateCcID caller sweep
- `src/engine/AudioEngineCommands_Session.cpp` — allocateClipID caller sweep
- `src/engine/AudioEngineCommands_Slicing.cpp` — allocateClipID caller sweep
- `src/engine/MidiImport.cpp` — allocateNoteID caller sweep
- `src/engine/AudioImport.cpp` — allocateClipID caller sweep
- `src/mcp/McpTools_Project.cpp` — allocateNoteID/CcID caller sweep

### Tests (tests/)
- `tests/unit/engine/instrument_part_test.cpp` — GlobalScale.ClippedMixScaledDown 4-stack
- `tests/unit/frontend/frontend_server_test.cpp` — AutoGainGlobalScaleRpc 4-stack
- `tests/unit/engine/verify_part_test.cpp` — ComposedPartPasses assertions + 2 new tests
- `tests/unit/mcp/note_id_test.cpp` — instance-style allocateNoteID calls
- `tests/unit/engine/automation_test.cpp` — AddLaneParamIdCollisionReturnsError (from previous session)

### Previous session's changes (also in this commit)
All changes from `2026-08-23-voltage-dnb-bugs-streamlining-shipped.md`:
- 6 MCP bugs fixed (velocity scaling, targetTrackIds, arpeggiator, addAutomationLane, audition_plugin, generate_arrangement)
- 3 pre-existing test fixes (AutoGainGlobalScale, VerifyPart heap corruption, Audition routingManager null)
- 4 streamlining features (bulk add_notes, paramID in list_fx_params, velocity range, rhythm bars > 16)
- FM synth sysex cartridge import (Dx7SysexImport.cpp)
- MCP server starts before engine init (timeout fix)
- PresetBrowser + SessionView frontend updates
