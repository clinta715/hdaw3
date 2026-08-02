# Recurring Developmental Pitfalls

Ranked by frequency across the last 10 sessions (2026-07-31).

| Rank | Pitfall | Occurrences | Sessions |
|------|---------|:-----------:|----------|
| 1 | State not restored on rebuild / projection seam | 6 | Muted track, Bug sweep, ValueTree sweep, RPC sweep |
| 2 | Unimplemented code path silently failing | 5 | Plugin GUI, FX rebuild, Solo, Clip mute, RPC sweep |
| 3 | Audio-thread safety violations | 4 | Audio sweep, Bug sweep (pipe write, file I/O, lock, O(n) scan) |
| 4 | Build/packaging stale binaries | 3 | Plugin GUI, VST editor, Bug sweep (build timeouts) |
| 5 | Frontend stale closures / missing hook deps | 3 | Stale closure sweep, Bug sweep, UI audit |
| 6 | Day-one bugs masked by live SPSC path | 2 | Muted track, ValueTree sweep |
| 7 | Window management / Z-order / sizing | 2 | Plugin GUI, VST editor |
| 8 | CSS design-token violations | 1 (many instances) | UI aesthetic audit |
| 9 | ID namespace collisions / missing validation | 2 | RPC sweep, Bug sweep |

---

## 1. State not restored on rebuild / projection seam (6 occurrences)

The ValueTree is the source of truth with two projections (ReadModel, audio
graph). Both rebuild wholesale from the tree, but state written via the live
SPSC path is silently lost when `rebuildRoutingGraph()` recreates processors.

**Instances:**
- Track volume/pan/mute never restored in `RoutingManager::addTrack` (day-one)
- Audio FX add/remove/bypass mutates ValueTree but never calls `rebuildTrackFX`
- Solo property set but no audio-path wiring (never silences tracks)
- Clip `muted` property written but processors never read it
- Track deltas clobber `effectiveMuted`/`effectiveSoloed` to `false`
- `parentId`/`childIds` changes slip through delta instead of escalating to fullSync

**Rule:** Any state that reaches a processor via SPSC must also be restored in
the rebuild path. Cover with state-preservation tests against the live processor
(`getMainProcessor()->getTrack(idx)`), not no-crash smoke tests.

---

## 2. Unimplemented code path silently failing (5 occurrences)

A property is written or a message is sent, but the receiving side has no
implementation — no error, no log, just silence.

**Instances:**
- `SHOW_EDITOR` sent to child process → sets `editorVisible = true`, never creates a window
- Audio FX commands mutate `FX_CHAIN` ValueTree → no listener rebuilds the live chain
- `isSoloed` property written → `valueTreePropertyChanged` doesn't handle it
- `setClipMuted` sets `IDs::muted` → clip processors don't read it
- Frontend "Edit" button → opens ProxyEditor placeholder, not the actual plugin GUI

**Rule:** When wiring a new command/property, trace the full path from RPC →
ValueTree → listener → processor. If any link is a no-op, the feature is
unimplemented. Add an integration test that asserts on the live processor output.

---

## 3. Audio-thread safety violations (4 occurrences)

Blocking or allocating operations on the realtime audio thread.

**Instances:**
- Blocking `WriteFile` to `PIPE_WAIT` named pipe every block (PluginProxySlot)
- `beginActualRecording()` does directory create, file open, WAV writer construction, heap allocs at count-in completion
- Blocking `CriticalSection` (`midiLock`) contending with MIDI-input thread
- O(total-clips) scan on audio thread when clip timing changes

**Rule:** The audio thread must never allocate, lock a mutex, do I/O, or call
`String` formatting. Use atomic flags serviced by the message thread, lock-free
structures (SPSC ring, SpinLock with `tryEnter`), and pre-allocated buffers.

---

## 4. Build/packaging stale binaries (3 occurrences)

Changes don't appear in the running app because the build pipeline doesn't
rebuild or copy the right artifacts.

**Instances:**
- `hdaw_plugin_host.exe` missing from `electron-builder.yml` extraResources
- `build-fast.bat package` didn't rebuild C++ before repackaging
- Full C++ builds timing out during iterative development

**Rule:** After any C++ change, verify the binary you're testing is the one you
just built. `frontend\build.bat` is the canonical full pipeline. Check
`electron-builder.yml` when adding new executables.

---

## 5. Frontend stale closures / missing hook deps (3 occurrences)

React `useMemo`/`useCallback` dependency arrays omit a variable that affects the
computation, causing frozen/stale UI.

**Instances:**
- `NoteGrid` `rects` useMemo omits `pixelsPerBeat` → notes freeze on zoom
- `handleDrop` missing `layout` dep → stale row geometry
- Stale `pps`/`layout` closures in timeline handlers

**Rule:** Any value used inside a memo/callback body must appear in its
dependency array. The classic async-stale-closure (reading `clips` prop after
`await`) has been hardened via `useProjectStore.getState()` — the remaining
pattern is missing deps in pure render memos.

---

## 6. Day-one bugs masked by live SPSC path (2 occurrences)

A feature "works" interactively because the SPSC bridge pushes state to the
current processor, but any rebuild (clip edit, load, tempo change) discards that
state and recreates processors at constructor defaults.

**Instances:**
- Track mute/volume/pan worked live but reset on every rebuild
- Tests only asserted ReadModel (always correct — the property IS written) and
  the only rebuild test checked "doesn't crash"

**Rule:** Interactive correctness ≠ rebuild correctness. Every stateful
processor needs a restore path exercised by a test that: (1) mutates state,
(2) rebuilds, (3) asserts on the live processor.

---

## 7. Window management / Z-order / sizing (2 occurrences)

Native windows spawned by the engine (plugin editors) have OS-level issues.

**Instances:**
- Child-process plugin window opens behind HDAW (Windows doesn't auto-foreground)
- `centreWithSize(editor->getWidth(), editor->getHeight())` sets window size
  including title bar/borders, so content area is smaller than expected

**Rule:** Use `toFront(true)` / `SetForegroundWindow` for child-process windows.
Account for window decorations when sizing — use `getBorderSize()` or
`resizableWindow` content-size APIs.

---

## 8. CSS design-token violations (1 session, many instances)

Hardcoded colors and undefined variables instead of the established token system.

**Instances (from UI audit):**
- Hardcoded hex colors in TransportBar, MixerStrip, AutomationPanel, TimelineMinimal
- Undefined CSS variables in UndoHistory (`--bg-control`, `--border-subtle`)
- Missing `prefers-reduced-motion` media query
- Global toggle where per-track was intended

**Rule:** All colors must reference `--var` tokens from the theme. Run a
periodic grep for raw hex values in CSS. Respect `prefers-reduced-motion`.

---

## 9. ID namespace collisions / missing validation (2 occurrences)

Using the wrong ID allocator or skipping bounds/null checks.

**Instances:**
- Ghost/paint note IDs used `allocateClipID()` instead of `allocateNoteID()` → collisions
- `getMainProcessor()` called without null-guard on 3 paths
- `std::stoi` on malformed `childIds` → crash

**Rule:** Each entity type has its own ID namespace. Guard all pointer access on
optional paths. Validate string→int conversions at trust boundaries.
