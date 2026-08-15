# Codebase Hardening & Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate the bugs, duplication, and performance hazards surfaced in the 2026-06-30 quick review (realtime-safety violations, an MCP/HTTP shutdown leak, undo correctness, redundant scene rebuilds, and oversized files), without changing user-visible features.

**Architecture:** Six phases executed in dependency order. Phases 1–2 are critical correctness fixes (audio reliability + shutdown). Phase 3 deduplicates model helpers that later phases and future work build on. Phase 4 removes hot-path waste. Phase 5 is behavior-preserving file splits. Every phase leaves the app building, tests green, and the app runnable.

**Tech Stack:** Qt 6 / JUCE 8, CMake, gtest. Build: `cmake --build build --config Debug`. Tests: `build/Debug/hdaw_tests.exe` (filter via `--gtest_filter=`). Smoke run: `build/Debug/HDAW.exe`. Commit style: `<Area>: <imperative-summary>` (see `AGENTS.md`).

**Conventions for every task:**
- Read the current state of each file before editing (`AGENTS.md` rule).
- Never run `build/Release/HDAW.exe` — it is stale.
- Realtime rule: no `new`/`malloc`/`String`/locks/UI calls on the audio thread.
- Add any new `.cpp` to `add_executable` / library sources in `CMakeLists.txt`.
- Verify after each task: build + `hdaw_tests.exe`.

**Phases at a glance**

| Phase | Theme | Tasks | Risk | Parallelizable |
|-------|-------|-------|------|----------------|
| 1 | Critical correctness | 1–3 | Med | No (do first) |
| 2 | Functional bug batch | 4–8 | Low | Yes (independent files) |
| 3 | Shared model helpers | 9–13 | Low | Yes |
| 4 | Optimizations | 14–18 | Low–Med | Mostly |
| 5 | Refactoring (splits) | 19–22 | Med | Sequentially |
| 6 | Final verification | 23 | — | — |

---

## File Structure

**Modified (bug fixes):**
- `src/engine/ClipSourceProcessor.h` — preload audio in `prepareToPlay`, remove audio-thread disk I/O.
- `src/engine/AudioEngine.cpp` — forward `isMuted` + clip `startTime`/`duration`/`offset`/`looping` to `SPSCBridge`.
- `src/engine/RoutingManager.{h,cpp}` — extend `updateClipParam` for clip-position paramIDs.
- `src/engine/MidiClipProcessor.h` — `ensureSize` + drop per-block CC7 spam.
- `src/engine/AutomationManager.h` — read `enabled` under lock; O(log n) lookup.
- `src/engine/MasterBusProcessor.h`, `src/engine/FxBusProcessor.h` — delete dead locals.
- `src/ui/MainWindow.cpp` — stop/delete `mcpHttp_`; remove duplicate shortcuts.
- `src/ui/AudioClipEditorWidget.cpp` — pass UndoManager on gain writes.
- `src/ui/TimelineInteraction.cpp`, `src/ui/NoteGridWidget.cpp`, `src/ui/TrackHeaderWidget.cpp` — coalesce undo during drags.
- `src/ui/NoteGridWidget.cpp` — wheel clamp + paint cull + scale cache + selection scan.
- `src/ui/TimelineToolbar.cpp` — `blockSignals` in `setPlaying`.
- `src/ui/AutomationLaneWidget.{h,cpp}` — remove dead `automation` member.
- `src/ui/MainWindow.cpp`, `src/ui/TimelineView.cpp`, `src/ui/AudioClipEditorWidget.cpp` — `unique_ptr` for `AudioFormatReader`.
- `src/ui/TrackHeaderWidget.cpp`, `src/ui/MixerStripWidget.cpp` — move state mutation out of `paintEvent`.
- `src/ui/Theme.h` — `static const QColor` accessors.
- `src/ui/TimelineInteraction.cpp`, `src/ui/TimelineView.cpp` — drop redundant `rebuildFromValueTree()`.

**New shared helpers / extracted units:**
- `src/model/ProjectModel.{h,cpp}` — public `createAudioClip`/`createMidiClip`/`createMidiNote`, `getTrackOfClip`, `addFxSlot`, `trackIndexAtY` (or `TimelineScene`).
- `src/engine/MidiImport.{h,cpp}` — `importMidiFile(...)` extracted from `MainWindow`.
- `src/engine/AudioImport.{h,cpp}` — `importAudioFile(...)` extracted from `MainWindow`.
- `src/mcp/McpExportTool.{h,cpp}` — export handler extracted from `McpTools`.
- `src/mcp/McpTools.cpp` — split `registerAllTools` into per-domain `register*Tools` aggregators.

---

## Phase 1 — Critical Correctness

### Task 1: Eliminate audio-thread disk I/O in `ClipSourceProcessor`

**Why first:** this is the single highest-impact bug. Every playing audio clip reads from a `FileInputStream` on the audio thread every block (page-cache misses → xruns/dropouts).

**Files:**
- Modify: `src/engine/ClipSourceProcessor.h` (whole class: members, `prepareToPlay`, `releaseResources`, `processBlock`)

**Approach:** preload the entire file into two `juce::HeapBlock<int>` buffers in `prepareToPlay` (reusing the exact `AudioFormatReader::read(int* const*, ...)` path the code already trusts), then drop the `reader` so `processBlock` only does an in-memory copy + int→float divide. Memory cost ≈ file size per clip (acceptable for v0.3; streaming is a documented follow-up).

- [ ] **Step 1: Replace the members** — remove `reader`, add preloaded storage.

In `src/engine/ClipSourceProcessor.h`, delete:
```cpp
std::unique_ptr<juce::AudioFormatReader> reader;
```
and add in the private section (next to `tempChannels`):
```cpp
juce::HeapBlock<int> preloadedData[2];
int preloadedChannels = 0;
int64_t preloadedLength = 0;
```
Also delete `tempChannels[2]` and its uses (the int scratch buffers are no longer needed in `processBlock`; the preload holds the data). Keep `formatManager` (used in `prepareToPlay`).

- [ ] **Step 2: Rewrite `prepareToPlay`** to preload and release the reader.
```cpp
void prepareToPlay(double sampleRate, int samplesPerBlock) override
{
    sr = sampleRate;

    preloadedData[0].free();
    preloadedData[1].free();
    preloadedChannels = 0;
    preloadedLength = 0;

    if (sourceFile.isNotEmpty())
    {
        std::unique_ptr<juce::AudioFormatReader> r(
            formatManager.createReaderFor(juce::File(sourceFile)));
        if (r != nullptr)
        {
            preloadedChannels = juce::jmin(static_cast<int>(r->numChannels), 2);
            const int total = static_cast<int>(r->lengthInSamples);
            if (total > 0)
            {
                preloadedData[0].malloc(total);
                preloadedData[1].malloc(total);
                int* const ptrs[2] = { preloadedData[0], preloadedData[1] };
                r->read(ptrs, preloadedChannels, 0, total, true);
                preloadedLength = static_cast<int64_t>(total);
            }
        }
    }

    gainSmooth.reset(sampleRate, 0.02);
    gainSmooth.setCurrentAndTargetValue(gain.load());
}
```

- [ ] **Step 3: Rewrite `releaseResources`.**
```cpp
void releaseResources() override
{
    preloadedData[0].free();
    preloadedData[1].free();
    preloadedChannels = 0;
    preloadedLength = 0;
}
```

- [ ] **Step 4: Rewrite the read section of `processBlock`** (replace the `if (reader != nullptr …)` block and the `else` branch, lines ~109–132) to copy from preloaded memory:
```cpp
// Read from preloaded in-memory buffer (RT-safe: no disk I/O)
if (preloadedLength > 0 && sourceSample < preloadedLength)
{
    int numToRead = (std::min)(numSamples, static_cast<int>(preloadedLength - sourceSample));

    buffer.clear();
    for (int ch = 0; ch < numChannels; ++ch)
    {
        int srcCh = (preloadedChannels > 1) ? (std::min)(ch, preloadedChannels - 1) : 0;
        const int* src = preloadedData[srcCh];
        float* dest = buffer.getWritePointer(ch);          // hoisted out of sample loop
        for (int s = 0; s < numToRead; ++s)
            dest[s] = static_cast<float>(src[sourceSample + s]) / 32768.0f;
        for (int s = numToRead; s < numSamples; ++s)
            dest[s] = 0.0f;                                 // tail beyond file
    }
}
else
{
    buffer.clear();
    return;
}
```
Note: this also folds in the M1 micro-opt (hoisted `getWritePointer`) and bounds the audible work.

- [ ] **Step 5: Build.**
Run: `cmake --build build --config Debug`
Expected: clean build (no references to `reader`/`tempChannels` remain).

- [ ] **Step 6: Smoke test playback.**
Run: `build/Debug/HDAW.exe`
Drag-drop a real `.wav` onto an audio track, press Space, confirm playback with no glitches. Repeat with 3 clips playing simultaneously.

- [ ] **Step 7: Commit.**
```bash
git add src/engine/ClipSourceProcessor.h
git commit -m "ClipSourceProcessor: preload audio in prepareToPlay to fix audio-thread disk I/O"
```

---

### Task 2: Stop and delete `mcpHttp_` on shutdown

**Files:**
- Modify: `src/ui/MainWindow.cpp` (destructor ~line 89; `mcpHttp_` declared in `MainWindow.h`)

- [ ] **Step 1: Read `MainWindow.h` and locate `mcpHttp_`** and confirm whether it is a raw `mcp::TransportHttp*` or owned pointer.

- [ ] **Step 2: Ensure clean stop in the destructor.** In `MainWindow::~MainWindow()` (currently only removes a transport listener), add the stop *before* child destruction. Insert as the first statements:
```cpp
MainWindow::~MainWindow()
{
    if (mcpHttp_ != nullptr)
    {
        mcpHttp_->stop();          // joins the HTTP server + reader thread
        delete mcpHttp_;
        mcpHttp_ = nullptr;
    }

    auto transportTree = engine.getProjectModel().getTransportTree();
    if (transportTree.isValid())
        transportTree.removeListener(this);
}
```
Rationale: `mcpServer_` is parented to `this`, so the HTTP transport (which references it) must be torn down first.

- [ ] **Step 3: Build + run.**
Run: `cmake --build build --config Debug` then launch, toggle the MCP HTTP server on (menu), close the window. Confirm the process exits cleanly (no hang, no crash). Re-open and confirm it still starts.

- [ ] **Step 4: Commit.**
```bash
git add src/ui/MainWindow.cpp
git commit -m "MainWindow: stop and delete MCP HTTP transport in destructor"
```

---

### Task 3: Forward mute + clip-position changes to the audio thread

**Why:** `AudioEngine::valueTreePropertyChanged` only pushes `volume`/`pan` (TRACK) and `gain`/`fadeIn`/`fadeOut` (CLIP). MCP/GUI mute writes never reach `Track::setMuted`, and clip move/resize writes never reach `ClipSourceProcessor` atomics until a full graph rebuild. Note `MainAudioProcessor::processBlock` already dispatches paramID `3` → `track->setMuted` (lines 67–68), so the mute path is half-wired.

**ParamID scheme (existing):** 1=volume, 2=pan, 3=mute, 10=gain, 11=fadeIn, 12=fadeOut. **New:** 13=startTime, 14=duration, 15=offset, 16=looping.

**Files:**
- Modify: `src/engine/AudioEngine.cpp` (TRACK and CLIP branches of `valueTreePropertyChanged`)
- Modify: `src/engine/RoutingManager.{h,cpp}` (`updateClipParam`)

- [ ] **Step 1: Add the `isMuted` branch in the TRACK section.** In `AudioEngine.cpp`, change:
```cpp
if (property == IDs::volume || property == IDs::pan)
{
    float value = treeWhosePropertyHasChanged.getProperty(property);
    int paramID = (property == IDs::volume) ? 1 : 2;
    ...
}
```
to:
```cpp
if (property == IDs::volume || property == IDs::pan || property == IDs::isMuted)
{
    float value = treeWhosePropertyHasChanged.getProperty(property);
    int paramID = (property == IDs::volume) ? 1
                : (property == IDs::pan)     ? 2
                                             : 3;   // isMuted
    auto trackList = projectModel.getTrackListTree();
    for (int i = 0; i < trackList.getNumChildren(); ++i)
    {
        if (trackList.getChild(i) == treeWhosePropertyHasChanged)
        {
            spscBridge.pushUpdate({ i, paramID, value });
            break;
        }
    }
}
```

- [ ] **Step 2: Extend the CLIP section** to forward position properties. Change the guard:
```cpp
if (property == IDs::gain || property == IDs::fadeIn || property == IDs::fadeOut)
```
to:
```cpp
if (property == IDs::gain    || property == IDs::fadeIn  || property == IDs::fadeOut ||
    property == IDs::startTime || property == IDs::duration ||
    property == IDs::offset   || property == IDs::looping)
```
and extend the `paramID` lookup to a small map:
```cpp
int paramID;
if      (property == IDs::gain)      paramID = 10;
else if (property == IDs::fadeIn)    paramID = 11;
else if (property == IDs::fadeOut)   paramID = 12;
else if (property == IDs::startTime) paramID = 13;
else if (property == IDs::duration)  paramID = 14;
else if (property == IDs::offset)    paramID = 15;
else                                  paramID = 16; // looping
float value = treeWhosePropertyHasChanged.getProperty(property);
```
(The existing track+clip index search loop below stays unchanged.)

- [ ] **Step 3: Read `RoutingManager.{h,cpp}`** and locate `updateClipParam(int trackIndex, int clipIndex, int paramID, float value)`.

- [ ] **Step 4: Handle the new paramIDs in `updateClipParam`.** It already handles 10/11/12 by fetching the `ClipSourceProcessor*` and calling `setGain`/`setFadeIn`/`setFadeOut`. Add:
```cpp
switch (paramID)
{
    case 10: clip->setGain(value);    break;
    case 11: clip->setFadeIn(value);  break;
    case 12: clip->setFadeOut(value); break;
    case 13: clip->setStartTime(static_cast<double>(value)); break;
    case 14: clip->setDuration(static_cast<double>(value));  break;
    case 15: clip->setOffset(static_cast<double>(value));    break;
    case 16: clip->setLooping(value > 0.5f);                 break;
    default: break;
}
```
(Adapt to the existing function's structure — match its current style; keep the null-check it already performs on `clip`.)

- [ ] **Step 5: Build + run unit tests.**
Run: `cmake --build build --config Debug`
Run: `build/Debug/hdaw_tests.exe`
Expected: all green.

- [ ] **Step 6: Smoke test.** Launch the app, mute a track during playback (track header M button), confirm it actually silences. Drag a clip to a new position during playback, confirm it plays from the new spot.

- [ ] **Step 7: Commit.**
```bash
git add src/engine/AudioEngine.cpp src/engine/RoutingManager.h src/engine/RoutingManager.cpp
git commit -m "AudioEngine: forward mute and clip-position changes to the audio thread via SPSCBridge"
```

---

## Phase 2 — Functional Bug Batch

> These tasks touch independent files; they may be executed in parallel by separate subagents. Each is one commit.

### Task 4: Remove duplicate Space/R shortcuts

**Files:** Modify `src/ui/MainWindow.cpp` (~lines 537–541)

- [ ] **Step 1:** Delete these lines (the menu `QAction`s at ~172/179 already carry the same shortcuts):
```cpp
auto* recordShortcut = new QShortcut(QKeySequence(Qt::Key_R), this);
connect(recordShortcut, &QShortcut::activated, this, &MainWindow::onRecordToggle);

auto* playShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
connect(playShortcut, &QShortcut::activated, this, &MainWindow::onPlayToggle);
```
- [ ] **Step 2: Build + smoke test** (Space = play/stop once, R = record once). `cmake --build build --config Debug`.
- [ ] **Step 3: Commit.** `git commit -am "MainWindow: remove duplicate play/record keyboard shortcuts"`

---

### Task 5: Make gain edits undoable

**Files:** Modify `src/ui/AudioClipEditorWidget.cpp:161`

- [ ] **Step 1:** Change `currentClip.setProperty(IDs::gain, linear, nullptr);` to:
```cpp
currentClip.setProperty(IDs::gain, linear, &engine.getProjectModel().getUndoManager());
```
(Confirm `engine` is the member in scope; sibling handlers at 174/180/etc. already use this exact form.)
- [ ] **Step 2: Build + smoke** (open an audio clip, move gain slider, Ctrl+Z should revert). 
- [ ] **Step 3: Commit.** `git commit -am "AudioClipEditorWidget: route gain slider through UndoManager"`

---

### Task 6: Coalesce undo transactions during drags

**Why:** Each `mouseMove` pushes a separate `setProperty(..., undoManager)` → one drag = dozens of undo steps.

**Files:** Modify `src/ui/TimelineInteraction.cpp`, `src/ui/NoteGridWidget.cpp`, `src/ui/TrackHeaderWidget.cpp`

**Pattern (apply to every drag site):** on press, call `undoManager->beginNewTransaction("<label>");` and write with `nullptr`; on release, do one final `setProperty(..., undoManager)` so the whole drag is one undo step. Alternative (simpler, lower-risk): on press call `beginNewTransaction`, and in `mouseMove` write with `nullptr`; on release re-write with `undoManager`.

- [ ] **Step 1: TimelineInteraction.cpp** — in `handleMousePress` (drag start) for move/trim/fade, call `engine.getProjectModel().getUndoManager().beginNewTransaction("Move clip");` (or "Trim clip"/"Fade"). In `handleMouseMove` change the `setProperty(..., undoManager)` at ~133/153/154/161/181/187 to pass `nullptr`. In `handleMouseRelease` do a single final `setProperty(..., undoManager)` for the moved property.
- [ ] **Step 2: NoteGridWidget.cpp** — same pattern for note drags at ~318/319/335/364/365: `beginNewTransaction("Move note")` on press, `nullptr` during move, commit on release.
- [ ] **Step 3: TrackHeaderWidget.cpp** — volume/pan fader drags at ~136/150: `beginNewTransaction("Adjust volume")`, `nullptr` during move, commit on release.
- [ ] **Step 4: Build + smoke.** Drag a clip, press Ctrl+Z once → whole drag reverts. Same for a note and a fader.
- [ ] **Step 5: Commit.** `git commit -am "ui: coalesce UndoManager transactions during continuous drags"`

---

### Task 7: Clamp NoteGrid wheel scroll to the piano range

**Files:** Modify `src/ui/NoteGridWidget.cpp` (~lines 534–538)

- [ ] **Step 1:** Replace the vertical-scroll branch:
```cpp
else {
    scrollY = (std::max)(0, scrollY - event->angleDelta().y());
    update();
}
```
with a call through the clamping setter:
```cpp
else {
    setScrollOffset(scrollX, scrollY - event->angleDelta().y());
}
```
(`setScrollOffset` already clamps to `[0, 128*keyHeight - height()]` and calls `update()`.)
- [ ] **Step 2: Build + smoke** (scroll the piano roll to the top/bottom; confirm it stops at note 0 and note 127).
- [ ] **Step 3: Commit.** `git commit -am "NoteGridWidget: route wheel scroll through clamping setScrollOffset"`

---

### Task 8: Housekeeping batch (one-line cleanups)

Bundle these low-risk fixes into a single commit; they are mechanical.

- [ ] **Step 1 — Delete dead locals.** Remove `int totalInputs = getTotalNumInputChannels();` from `src/engine/MasterBusProcessor.h:35` and `src/engine/FxBusProcessor.h:51`.
- [ ] **Step 2 — Remove dead member.** Delete `automation` raw pointer from `src/ui/AutomationLaneWidget.h` and its assignments at `AutomationLaneWidget.cpp:108,117`.
- [ ] **Step 3 — `blockSignals` in `setPlaying`.** In `src/ui/TimelineToolbar.cpp:214-218`, wrap the body:
```cpp
void TimelineToolbar::setPlaying(bool playing) {
    const QSignalBlocker blocker(playBtn);
    playBtn->setChecked(playing);
    playBtn->setText(playing ? "\xE2\x8F\xB8" : "\xE2\x96\xB6");
}
```
- [ ] **Step 4 — `unique_ptr` for AudioFormatReader** (3 sites): replace
```cpp
auto* reader = pool.getFormatManager().createReaderFor(f);
if (reader != nullptr) { ...; delete reader; }
```
with
```cpp
auto reader = std::unique_ptr<juce::AudioFormatReader>(pool.getFormatManager().createReaderFor(f));
if (reader) { ...; }
```
Sites: `src/ui/MainWindow.cpp:1090-1095`, `src/ui/TimelineView.cpp:506-511`, `src/ui/AudioClipEditorWidget.cpp:228-237`.
- [ ] **Step 5 — `AutomationManager::enabled` data race.** In `src/engine/AutomationManager.h:31`, move the `if (!enabled) return -1.0;` check *inside* the `cacheLock.tryEnter()` block (read `enabled` there, before reading `points`).
- [ ] **Step 6 — Drop per-block CC7 spam.** In `src/engine/MidiClipProcessor.h` add a `float lastCcValue = -1.0f;` member and change the CC block (~117-119) to only `addEvent` when `ccVal != lastCcValue` (then store it). Also `ensureSize` the MidiBuffer in `prepareToPlay` (see Task 18 step on `MidiClipProcessor` for the exact addition, or do it now): in `prepareToPlay` there is no MidiBuffer to size; instead cap in-flight events — simplest is the caching above.
- [ ] **Step 7 — `AudioRecorder` latent overflow.** In `src/engine/AudioRecorder.cpp` `processBlock`, clamp the copy length to the buffer size: `const int n = juce::jmin(samples, channelBuffer.getNumSamples());` and `copyFrom(ch, 0, buffer, ch, 0, n)`. (The buffer is sized to 16384 in `startRecording`; a larger block would currently overflow.)
- [ ] **Step 8 — MCP start/stop signal blocker.** In `src/ui/MainWindow.cpp` `startMcpHttpServer` (~293-294) and `stopMcpHttpServer` (~304-305), wrap each `mcpHttpAction->setChecked(...)` with `const QSignalBlocker b(mcpHttpAction);` so the toggled handler doesn't re-enter synchronously.
- [ ] **Step 9 — Build + tests.** `cmake --build build --config Debug` then `build/Debug/hdaw_tests.exe`.
- [ ] **Step 10 — Commit.** `git commit -am "cleanup: remove dead state, harden signal blockers, RAII readers, bounds-check recorder"`

---

## Phase 3 — Shared Model Helpers (dedup)

> These make the Phase 5 refactors cleaner and remove the highest-frequency duplication. Each is independent.

### Task 9: Public clip/note factories in `ProjectModel`

**Why:** 8 sites hand-write the 12–13-property clip ValueTree. `ProjectModel.cpp:187-241` already has these as file-local statics.

**Files:** Modify `src/model/ProjectModel.{h,cpp}`; then 8 call sites.

- [ ] **Step 1:** Read `ProjectModel.cpp:187-241` and promote `createAudioClip`/`createMidiClip`/`createMidiNote` to `public static` in `ProjectModel.h` (keep their signatures).
- [ ] **Step 2:** Replace each hand-written block with a call:
  - `src/ui/MainWindow.cpp:1106-1119` → `createAudioClip(...)`
  - `src/ui/MainWindow.cpp:1230-1242` → `createMidiClip(...)`
  - `src/ui/TimelineView.cpp:444-458` and `521-533`
  - `src/ui/TimelineInteraction.cpp:267-281`
  - `src/ui/PhraseGeneratorDialog.cpp:479-490`
  - `src/mcp/McpTools.cpp:386-399`, `417-430`, `662-674` (and the note factory at `677-683`)
  - `src/ui/PhraseGeneratorDialog.cpp:495-501` (note)
- [ ] **Step 3:** Build + run `build/Debug/hdaw_tests.exe` + smoke (add audio + MIDI clips via GUI and MCP `add_clip`).
- [ ] **Step 4:** Commit. `git commit -am "ProjectModel: expose createAudioClip/createMidiClip/createMidiNote and dedupe 8 call sites"`

---

### Task 10: `TimelineScene::trackIndexAtY`

**Files:** Modify `src/ui/TimelineScene.{h,cpp}`; 3 call sites.

- [ ] **Step 1:** Add (mirror the existing `getTrackY` summing logic at `TimelineScene.cpp:106-113`):
```cpp
// TimelineScene.h (public)
int trackIndexAtY(double y) const;
// TimelineScene.cpp
int TimelineScene::trackIndexAtY(double y) const
{
    double acc = 0.0;
    for (int i = 0; i < getNumTracks(); ++i)
    {
        double h = getTrackHeight(i);
        if (y >= acc && y < acc + h) return i;
        acc += h;
    }
    return -1;
}
```
- [ ] **Step 2:** Replace the three hand-written loops: `src/ui/TimelineInteraction.cpp:235-250`, `src/ui/TimelineView.cpp:421-432`, `src/ui/TimelineView.cpp:488-499`.
- [ ] **Step 3:** Build + smoke (double-click empty timeline to create a clip on the right track; drop a file).
- [ ] **Step 4:** Commit. `git commit -am "TimelineScene: add trackIndexAtY and dedupe hit-test loops"`

---

### Task 11: `ProjectModel::getTrackOfClip`

**Files:** Modify `src/model/ProjectModel.{h,cpp}`; call sites.

- [ ] **Step 1:** Add:
```cpp
// ProjectModel.h (public)
static juce::ValueTree getTrackOfClip(const juce::ValueTree& clip);
// ProjectModel.cpp
juce::ValueTree ProjectModel::getTrackOfClip(const juce::ValueTree& clip)
{
    if (!clip.isValid() || clip.hasType(IDs::CLIP)) return {};
    auto clipList = clip.getParent();
    if (!clipList.isValid()) return {};
    return clipList.getParent();
}
```
- [ ] **Step 2:** Replace `clipTree.getParent().getParent()` at `src/ui/MainWindow.cpp:420` and `src/ui/ClipItem.cpp:21` (and anywhere else grep finds it).
- [ ] **Step 3:** Build + tests + commit. `git commit -am "ProjectModel: add getTrackOfClip helper"`

---

### Task 12: `PreferencesDialog::settings()` accessor

**Files:** Modify `src/ui/PreferencesDialog.{h,cpp}`; ~26 call sites (13 in MainWindow).

- [ ] **Step 1:** Add a function-local-static accessor:
```cpp
// PreferencesDialog.h (public)
static QSettings& settings();
// PreferencesDialog.cpp
QSettings& PreferencesDialog::settings()
{
    static QSettings s(kSettingsOrg, kSettingsApp);
    return s;
}
```
- [ ] **Step 2:** Grep for `QSettings settings(PreferencesDialog::kSettingsOrg` and replace each `QSettings settings(...)` with `auto& settings = PreferencesDialog::settings();`. (Note: this makes all settings share one cached backend — desirable.)
- [ ] **Step 3:** Build + smoke (open/save project, toggle prefs). Commit. `git commit -am "PreferencesDialog: add cached settings() accessor and dedupe 26 sites"`

---

### Task 13: Centralize FX-slot creation

**Files:** Modify `src/model/ProjectModel.{h,cpp}`; `src/ui/TrackHeaderWidget.cpp:729-777`; `src/ui/MainWindow.cpp:943-995`.

- [ ] **Step 1:** Add `ProjectModel::addFxSlot(int trackIdx, const juce::String& type, ...)` and `addPluginToTrack(...)` that get-or-create the `FX_CHAIN` / `FX_SLOT` and populate it (mirror the bodies at `TrackHeaderWidget.cpp:729-777`).
- [ ] **Step 2:** Replace the four duplicated bodies (`TrackHeaderWidget` x2, `MainWindow::onAddTrackWithFX`, `onAddTrackWithPlugin`).
- [ ] **Step 3:** Build + smoke (add FX and plugins via track-header context menu and the add-track menu). Commit. `git commit -am "ProjectModel: centralize addFxSlot/addPluginToTrack creation"`

---

## Phase 4 — Optimizations

### Task 14: Drop redundant scene rebuilds

**Why:** `TimelineScene` already adds/removes clip items incrementally via its ValueTree listeners; mutation sites also call `rebuildFromValueTree()` immediately after, discarding the incremental work.

**Files:** Modify `src/ui/TimelineInteraction.cpp:283-285`, `src/ui/TimelineView.cpp:460-462`, `src/ui/TimelineView.cpp:534-537`.

- [ ] **Step 1:** Remove the `scene->rebuildFromValueTree()` call at each of the three sites (keep the `addChild`).
- [ ] **Step 2:** Separately, audit `MainWindow::rebuildAllUI()` callers (13 sites) and remove the ones that follow a single localized mutation: `onUndo`, `onRedo`, `onAddTrack`×2, `onDeleteTrack`, `onRenameTrack`, `onDuplicateTrack`, `onImportAudio`, `onImportMIDI`, `onRecordToggle`. Keep `rebuildAllUI()` only on `onNew`/`onOpen` (whole-tree swap). For the removed cases, the per-widget ValueTree listeners already refresh the affected views; verify each by smoke test.
- [ ] **Step 3:** Build + thorough smoke (add/delete/rename/duplicate/undo/import each must still update the scene, mixer, FX chain, piano roll correctly).
- [ ] **Step 4:** Commit. `git commit -am "ui: rely on incremental ValueTree listeners instead of full rebuilds"`

---

### Task 15: Speed up `NoteGridWidget::paintEvent`

**Files:** Modify `src/ui/NoteGridWidget.cpp` and `src/ui/PianoRollModel.{h,cpp}`.

- [ ] **Step 1 — Cache scale pitches.** Add members `int cachedScaleRoot = -1; int cachedScaleMode = -1; std::bitset<128> inScale;`. In `paintEvent` (top), if `root != cachedScaleRoot || mode != cachedScaleMode`, rebuild `inScale` from `PhraseGenerator::buildScalePitches(...)` once and cache. Replace the per-note scale lookup with `inScale.test(note)`.
- [ ] **Step 2 — Fix O(n²) selection.** Add `PianoRollModel::isSelected(int noteIndex)` (check membership cheaply) and rewrite the selection loop (`NoteGridWidget.cpp:196-210`) to a single pass:
```cpp
for (int i = 0; i < model.getNumNotes(); ++i)
    if (model.isSelected(i)) { /* draw outline */ }
```
- [ ] **Step 3 — Add vertical paint cull** (`NoteGridWidget.cpp:182`, documented gap):
```cpp
if (r.right() < 0 || r.left() > w || r.bottom() < 0 || r.top() > h) continue;
```
- [ ] **Step 4:** Build + smoke (open a 100+ note MIDI clip; scroll/zoom should stay smooth). Commit. `git commit -am "NoteGridWidget: cache scale pitches, linear selection scan, vertical paint cull"`

---

### Task 16: Scope TrackHeader VU repaints

**Why:** `updateVU()` runs at ~60 Hz and calls `update()`, repainting *every* track row even though only the tiny VU bars change.

**Files:** Modify `src/ui/TrackHeaderWidget.cpp` (the `updateVU` slot ~81-94 and the per-row `vuRect`).

- [ ] **Step 1:** In `updateVU`, instead of `update()` call `update(header.vuRect)` for each track (ensure `header.vuRect` is computed before the timer fires — see Task 17 which moves rect computation out of `paintEvent`; if not yet done, compute it in a small `layoutRects()` here). If per-rect updates are awkward, raise the interval to ~30 Hz (`vuUpdateInterval = 33`) as a fallback.
- [ ] **Step 2:** Build + smoke (play a project; confirm VUs still animate and CPU dropped).
- [ ] **Step 3:** Commit. `git commit -am "TrackHeaderWidget: scope VU repaints to vuRect"`

---

### Task 17: Cache Theme colors + move state out of `paintEvent`

**Files:** Modify `src/ui/Theme.h`, `src/ui/TrackHeaderWidget.cpp`, `src/ui/MixerStripWidget.cpp`.

- [ ] **Step 1 — Theme statics.** In `src/ui/Theme.h`, convert each accessor to a function-local static, e.g.:
```cpp
inline QColor bgWindow() { static const QColor c{0x12,0x12,0x14}; return c; }
```
Apply to all ~30 accessors (mechanical).
- [ ] **Step 2 — Move rect computation out of paint.** Add `TrackHeaderWidget::layoutRects()` that fills each `TrackHeader::bounds`/`vuRect`/`muteRect`/etc. Call it from `rebuild()` and `resizeEvent`, *not* `paintEvent`. Remove the `count != tracks.size()` `rebuild()` call and the `header.bounds = row;` writes from `paintEvent`. Do the analogous change in `MixerStripWidget.cpp:62-71`.
- [ ] **Step 3 — Cache QFonts.** Add three `QFont` members (`nameFont`, `toggleFont`, `smallFont`) to `TrackHeaderWidget`, configure them once in the ctor/rebuild, and `painter.setFont(...)` them in the loop instead of mutating.
- [ ] **Step 4:** Build + smoke (track headers render identically; resize the window; add/remove tracks). Commit. `git commit -am "ui: cache Theme QColors and QFonts, keep paintEvent side-effect-free"`

---

### Task 18: Hot-path micro-optimizations

**Files:** `src/engine/AutomationManager.h`, `src/engine/MidiClipProcessor.h`, `src/engine/ClipSourceProcessor.h`.

- [ ] **Step 1 — O(log n) automation lookup.** In `AutomationManager.h:45-53`, replace the linear scan with `std::lower_bound` over the sorted `points` (they are sorted in `rebuildCache:122`) by the sample/time key. Add `#include <algorithm>`.
- [ ] **Step 2 — Bound the gain/fade loop.** In `ClipSourceProcessor.h:143`, change `for (int s = 0; s < numSamples; ++s)` to iterate only `numToRead` samples (the audible portion) when `numToRead < numSamples` (the tail is already zeroed).
- [ ] **Step 3 — `MidiClipProcessor` `ensureSize`.** If a per-node `prepareToPlay`-equivalent exists where a MidiBuffer can be pre-sized, add `midiMessages.ensureSize(1024);` once; otherwise rely on the CC caching from Task 8 Step 6 (which removes the main growth driver).
- [ ] **Step 4:** Build + tests + commit. `git commit -am "engine: lower_bound automation lookup, bound clip gain loop"`

---

## Phase 5 — Refactoring (behavior-preserving)

> Run the full test suite and a broad smoke pass after *each* task in this phase; these are structural moves.

### Task 19: Split `MainWindow::setupLayout`

**Files:** Modify `src/ui/MainWindow.{h,cpp}`.

- [ ] **Step 1:** Extract from `setupLayout` (~309–595):
  - `setupBottomPanel()` (bottom tab bar + `QStackedWidget` + tab sync, ~318–398)
  - `connectTimelineSignals()` and `connectBottomPanelSignals()` (~413–558)
  - `restoreWindowGeometry()` (~560–594)
- [ ] **Step 2:** Move the `clipSelected` lambda body (~415–463) into a named slot `MainWindow::onClipSelected(const juce::ValueTree&)` and connect to it. Update `MainWindow.h` declarations.
- [ ] **Step 3:** Build + full smoke + tests. Commit. `git commit -am "MainWindow: split setupLayout into focused setup/connect methods"`

---

### Task 20: Extract MIDI/Audio import

**Files:** Create `src/engine/MidiImport.{h,cpp}`, `src/engine/AudioImport.{h,cpp}`; modify `src/ui/MainWindow.cpp`, `CMakeLists.txt`.

- [ ] **Step 1:** Move the bodies of `onImportMIDI` (1125–1290) and `onImportAudio` (1048–1123) into free functions:
```cpp
namespace HDAW { bool importMidiFile(AudioEngine& engine, const QString& path); }
namespace HDAW { bool importAudioFile(AudioEngine& engine, const QString& path); }
```
Each returns success; `MainWindow` keeps only the file-dialog + track-picker UI and calls `rebuildAllUI()` on success.
- [ ] **Step 2:** Add the two `.cpp` files to `CMakeLists.txt` (`hdaw_lib` sources and the `HDAW` target as appropriate — match how other `src/engine/*.cpp` are listed).
- [ ] **Step 3:** Build + smoke (import a `.mid` and a `.wav`). Commit. `git commit -am "engine: extract MidiImport/AudioImport from MainWindow"`

---

### Task 21: Split `McpTools::registerAllTools`

**Files:** Modify `src/mcp/McpTools.{h,cpp}`; create `src/mcp/McpExportTool.{h,cpp}`; `CMakeLists.txt`.

- [ ] **Step 1:** Split `registerAllTools` into per-domain free functions in `McpTools.cpp` (group by the existing `// ---` comment separators): `registerReadTools`, `registerTransportTools`, `registerTrackTools`, `registerClipTools`, `registerNoteTools`, `registerCompositionTools`, `registerAutomationTools`, `registerFxTools`. Keep `registerAllTools(McpServer&)` as a 9-line aggregator calling them in order. The `findClip`/`findNote`/`findLane` lambdas become file-scope helpers taking `AudioEngine&`.
- [ ] **Step 2:** Move the ~168-line `export_audio` handler into `McpExportTool.{h,cpp}` (`void registerExportTool(McpServer&);`), called from `registerAllTools`. Add the `.cpp` to `CMakeLists.txt`.
- [ ] **Step 3:** Build + run `build/Debug/hdaw_tests.exe --gtest_filter=McpServer.*` (and the rest). Commit. `git commit -am "mcp: split registerAllTools into per-domain registrars and a dedicated export tool"`

---

### Task 22: Split context-menu builders

**Files:** Modify `src/ui/TrackHeaderWidget.{h,cpp}`, `src/ui/TimelineView.cpp`.

- [x] **Step 1 — TrackHeaderWidget:** extract `buildEmptyAreaMenu(const QPoint&)` and `buildTrackMenu(int trackIdx, const QPoint&)` from the 140-line `contextMenuEvent` (~588–727). The handler becomes a dispatcher that picks one and calls `menu.exec()`.
- [x] **Step 2 — TimelineView:** split `eventFilter` (~266–472) into `handleContextMenu(QContextMenuEvent*)`, `handleKeyPress(QKeyEvent*)`, `handleDrop(QDropEvent*)`. Reuse the shared clip factory (Task 9) in the "Add MIDI Clip" branch.
- [x] **Step 3:** Build + smoke (right-click empty timeline, a clip, a track header; keyboard shortcuts; drag-drop). Commit. `git commit -am "ui: extract context-menu builders in TrackHeaderWidget and TimelineView"`

---

## Phase 6 — Final Verification

### Task 23: Whole-project verification

- [ ] **Step 1 — Clean build.** `cmake --build build --config Debug` → 0 warnings, 0 errors.
- [ ] **Step 2 — Full test suite.** `build/Debug/hdaw_tests.exe` → all green. (Note the known `McpServer.HttpRoundTrip` ordering caveat from `AGENTS.md`; a single run is stable.)
- [ ] **Step 3 — Realtime check.** With playback running (3 audio clips + 1 MIDI clip), watch `%TEMP%/hdaw_debug.log` and the host; confirm no new xrun/dropout symptoms and that the audio-thread fixes hold.
- [ ] **Step 4 — Feature smoke matrix:** play/stop/loop; record; add/delete/rename/duplicate track; add/move/trim/split clip; MIDI note add/move/delete; FX add; automation draw; export audio; undo/redo after a clip drag (single step); MCP `set_track` mute; MCP `move_clip` while playing.
- [ ] **Step 5 — Update `AGENTS.md`** "Out-of-scope" section: move the resolved items (per-clip editor still pending) and add any new lessons learned (e.g. the audio-preload tradeoff, the paramID 13–16 scheme).
- [ ] **Step 6 — Final commit.** `git commit -am "docs: update AGENTS.md after codebase hardening"`

---

## Notes & Tradeoffs

- **Audio preload memory cost (Task 1):** each clip now holds its whole file in RAM as int samples. Acceptable for v0.3; a background-streaming ring buffer is the documented v0.4 follow-up (add to `AGENTS.md` out-of-scope).
- **Undo coalescing (Task 6):** the chosen pattern (`beginNewTransaction` + `nullptr` during move + commit on release) is the minimal-risk option. A future improvement is `UndoManager::push` with a single combined action, but that is a larger change.
- **Incremental rebuilds (Task 14):** removing `rebuildAllUI()` calls relies on per-widget ValueTree listeners being complete. If a listener gap is found during smoke, restore that specific call rather than reverting the whole task.
- **Out of scope for this plan:** per-clip audio editor (v0.4 feature), MCP HTTP auth, MCP `resources/*`/`prompts/*`, async export worker — all tracked in `AGENTS.md`.
