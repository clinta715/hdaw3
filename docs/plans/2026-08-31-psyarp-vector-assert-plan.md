
## Goal
Fix psyarp engine crash: MSVC debug vector assertion (vector line 1939, subscript out of range) during offline export when psyarp tracks play held chords (reproduced 2x on Ion Storm: ArpMain UpDown/Oct2 + ArpAlt Random/Oct2 with held bar-chords, hold=8.0 abutting). Hypatia (Asym332/Oct1 + UpDown/Oct1 held chords) rendered fine.

## Success Gates
- [ ] Gate 1: Identify exact OOB site (audit arp_.sequence[arp_.seqIndex], seqIndex staleness across rebuildArpSequence, Random branch, oscVoices, delay/reverb buffers) and fix with clamps/rebuild-on-change
- [ ] Gate 2: Regression gtest added: psyarp held-chord stress — Random/Asym332/UpDown x OctaveRange 1-2, abutting note boundaries (off==on same tick), empty-held interleaves; render N thousand blocks, no crash/assert
- [ ] Gate 3: cmake --build build --config Debug --target hdaw_tests HDAW_headless succeeds (Embedded PDB config; use vcvars64 bat; see /mnt/c/Users/hapbt/AppData/Local/Temp/build_final2.bat pattern)
- [ ] Gate 4: new test passes; ExportBakeTimeout.* + ExportAutomation.* still pass
- [ ] Gate 5: Report files changed + test output

## Dependency Map
- src/engine/PsyArpEngine.cpp/.h (ArpState{sequence, seqIndex, heldNotes, currentStepBeat, currentNote, noteOffBeat, lastBeat}, rebuildArpSequence, processBlock step advance)
- Called from TrackFXSlot (slot DSP recreate on prepare), no ValueTree changes, no projections
- SPSC/audio thread: processBlock runs on render thread — fixes must be audio-thread-safe (no locks/allocs beyond existing pattern)

## Suspects (verify each)
1. rebuildArpSequence only called when sequence empty — held-chord SET changes leave seqIndex/sequence stale/inconsistent; if a rebuild happens (empty moment between held chords) while seqIndex large, modulo protects, but CHECK currentNote/noteOffBeat vs shrunk sequence
2. Random branch: rng()%base.size() is safe if base non-empty (guarded) — verify no path with empty base
3. seqIndex advance BEFORE %: `arp_.seqIndex = (arp_.seqIndex + 1) % size` then `sequence[seqIndex]` — safe if size>0; check all paths guarantee non-empty (the else-if guard)
4. heldNotes erase of a pitch NOT in set / insert during iteration — check container type in header (set vs vector)
5. Any vector in TrackFXSlot prepare path racing the render (stateLock idiom) — only if 1-4 don't explain

## Pitfall Gates
- Gate 3 (audio-thread safety): no locks/allocs in processBlock path beyond current patterns
- Gate 11/12: export render context unchanged
- Gate 4: fresh binary timestamps

## Steps
1. Read src/engine/PsyArpEngine.{h,cpp} fully; find the exact OOB; add clamps + rebuild-on-held-change (cheap: compare a copy/hash of heldNotes each block or rebuild on every add/erase)
2. Add tests/unit/engine psyarp regression test (follow existing engine test style; construct engine directly, feed MIDI, call processBlock in a loop)
3. Build + run tests (vcvars64 + cmake --build build --config Debug --target hdaw_tests -- -j1 if needed; CMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded already in cache)
4. Report diff + outputs
