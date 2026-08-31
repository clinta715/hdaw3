# HDAW JUCE Engine Pitfalls

Domain-specific documentation split from AGENTS.md.
For the original combined file, see `../AGENTS.md`.

Sections covering JUCE engine issues: VST3 scan blacklisting, default project
samples, DBG macro collision, build pipeline (MOC/PDB), AudioProcessorGraph
bus layout propagation.

## VST3 scan failures must be blacklisted — or they repeat every startup

`PluginManager::scanAll` in `src/engine/PluginManager.cpp` finds all `.vst3` and `.clap` files, then iterates. For each file, the isolated scanner (`hdaw_plugin_scanner.exe`) is spawned. If the scanner exits with code 1 (plugin can't be instantiated by the scanner, e.g. missing dependency), the file was **not** blacklisted — so it gets scanned again on the next launch.

The fix (v0.7.0+): also blacklist files when the scanner exits with a non-zero code, using reason `"scan_failure"`. This joins the existing "crash" blacklist and skips re-scanning.

The relevant code path in `scanAll()` (`src/engine/PluginManager.cpp:174-198`):

- **Exit code 0** → success, parsed JSON → `knownPluginList.addType`
- **Exit code 1** → normal failure, pedal deleted → now blacklisted as `"scan_failure"`
- **Exit code ≥2** → crash, pedal preserved → blacklisted as `"crash"` (existing logic)
- **Timeout** → kill child, pedal preserved → blacklisted as `"crash"` (existing logic)

If a user manually fixes the scanner/plugin setup, they can un-blacklist via `PluginManager::unblacklistPlugin` or by editing the `plugin_blacklist.xml` file in the HDAW app data directory.

## Default project should not reference non-existent sample files

`ProjectModel::createDefaultProject` historically created audio clips
on Track 1 and Vocals with `sourceFile` set to `samples/bass.wav`,
`samples/drums.wav`, and `samples/vocals.wav`. None of these files
ship with the project. The clips would silently render a 10% white
tint (`AudioClipItem::paintContent` fallback) and the user would see
"empty audio clips" with no indication that the data was missing.

Audio tracks should be created with an **empty `CLIP_LIST`**. Users
populate them by drag-dropping real audio files. Do not add
fake/sample audio clips back to the default project without
also shipping the actual sample files.

## `DBG` macro collides with JUCE — use `HDAW_LOG`, do not redefine

JUCE defines `DBG(textToWrite)` as a single-argument macro in
`juce_PlatformDefs.h` (used in 100+ places across the project).
Trying to `#define DBG(tag, msg)` to add a two-argument debug
log is wrong on two counts:

1. **Redefinition warning** — the compiler emits `C4005: 'DBG':
   macro redefinition` because JUCE's version is already in scope
   from any TU that includes a JUCE header.
2. **Signature mismatch** — the 8 existing `DBG("TSCtor", ...)`
   call sites in `TimelineScene.cpp` and `MainWindow.cpp` pass
   two arguments (tag + message). JUCE's `DBG` takes one
   argument. Either the build fails outright or the calls bind
   to the wrong macro and silently produce garbage.

The project's own logging facility is `HDAW_LOG(tag, msg)`, defined
in `src/common/DebugLog.h`. It writes NDJSON to
`%TEMP%/hdaw_debug.log`. All TUs that call it must
`#include "DebugLog.h"`.

**Rule**: never use the bare `DBG` identifier in this project. If
you see `DBG(...)` in source, rename it to `HDAW_LOG(...)`. If you
add a new logging macro, pick a name that does not collide with
JUCE — `HDAW_LOG`, `LOG_INFO`, `AppLog`, anything but `DBG`.

## Build pipeline: MOC, autogen, stale PDB, parallel-link

The project uses Qt 6 with `qt_standard_project_setup()` which
enables `CMAKE_AUTOMOC` automatically. MOC processes any header
that contains `Q_OBJECT`. A few things to know:

- **Stale PDB on parallel builds**. The first time
  `cmake --build build --config Debug` is invoked after a large
  edit, MSBuild's parallel-link may fail with
  `C1041: cannot open program database 'vc145.pdb'; if multiple
  CL.EXE write to the same .PDB file, please use /FS`. The fix
  is to kill any orphaned `cl.exe` and `Tracker.exe` processes
  left over from a previous aborted build, then re-run. The
  command:

  ```powershell
  Get-Process cl, Tracker, MSBuild -ErrorAction SilentlyContinue |
      Stop-Process -Force
  cmake --build build --config Debug
  ```

- **Header-only edits are not always detected.** The build
  system uses header mtime to decide what to recompile. If you
  change a `.h` and the build does not pick it up (you see
  unchanged behaviour despite a clear source diff), force a
  recompile by touching the corresponding `.cpp` or by deleting
  the relevant `.obj` files in `build/HDAW.dir/Debug/`.

- **The Release binary is stale.** If the user reports
  "nothing changed visually," check whether they are running
  `build\Debug\HDAW.exe` (29 MB) or `build\Release\HDAW.exe`
  (5 MB). The Release one was built before the bug-fix series
  and is intentionally not maintained. Always run the Debug
  binary.

- **Sources must be added to `add_executable` in `CMakeLists.txt`.**
  Adding a new `.cpp` file without listing it in the CMake
  source list will not produce a build error — the file just
  will not be compiled. Always check the source list when
  adding a new translation unit.

## AudioProcessorGraph bus layout must be propagated — or output is silently zero

**Symptom**: The master VU meter moves during playback, but the speaker
buffer is silent (`peak=0.000000`). Every clip → track → master
connection works (the meter moves!), yet no audio reaches the device.
This looks exactly like a broken plugin or a muted track — it is
neither.

**Root cause** (`MainAudioProcessor`, fixed after v0.4.2):

`juce::AudioProcessorGraph`'s `audioOutputNode` reads its input-channel
count from the graph's *own* output bus, which is set by
`setBusesLayout()` — **not** by `prepareToPlay()`. If the host
processor (`MainAudioProcessor`) never propagates its negotiated bus
layout to the graph, the IO node reports `getTotalNumInputChannels() ==
0` and every `graph.addConnection({ { masterNode, ch }, { ioNode, ch } })`
is **silently rejected** (returns `false`, no error, no log line). The
master bus still processes its inputs (so its meter moves), but its
output has nowhere to go.

`prepareToPlay()` alone does **not** fix this — calling it on the graph
re-negotiates node internals but does not copy the host layout in.

**The fix** (two parts, both required):

```cpp
// 1. Accept the host layout during negotiation. Without this override,
//    JUCE may disable the buses and the graph inherits a disabled layout.
bool MainAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    const auto& mainIn  = layouts.getMainInputChannelSet();
    if (mainOut.isDisabled()) return false;
    if (!mainIn.isDisabled() && mainIn.size() != mainOut.size()) return false;
    return true;
}

// 2. Propagate the host layout to the graph BEFORE building topology.
void MainAudioProcessor::prepareToPlay(double sr, int bs)
{
    ...
    graph.setBusesLayout(getBusesLayout());          // ← the actual fix
    routingManager = std::make_unique<...>(graph, ...);
    routingManager->rebuildFromValueTree();          // adds master→IO etc.
    graph.prepareToPlay(sr, bs);
    routingManager->reconnectMasterToOutput();       // belt-and-suspenders
}
```

`RoutingManager::reconnectMasterToOutput()` re-adds the master→IO
connections after `prepareToPlay` finalizes channel negotiation. With
the layout correctly propagated it's strictly redundant, but it guards
against any future code path that rebuilds the topology without first
calling `setBusesLayout`.

**Diagnostic signature**: `[DIAG] reconnectMasterToOutput:
masterNumOut=2 ioInChannels=0 connecting=2` followed by
`reconnect ch=0 ok=0`. The `ioInChannels=0` and `ok=0` together are
the fingerprint of this bug. After the fix they read `ioInChannels=2`
and `ok=1`.

**Why this is easy to re-introduce**: the symptom (meters move, no
sound) is identical to a dozen other bugs — wrong audio device, muted
master, broken plugin, phase-cancellation. The natural instinct is to
chase the signal path *inside* the graph. The actual cause is one layer
*above* the graph: the bus layout that the graph's IO node derives
from. Nothing in the graph itself is wrong.

## `ValueTree::setProperty` does NOT fire listeners when the value is unchanged

JUCE's `ValueTree::setProperty(id, newValue, undoManager)` only fires
`valueTreePropertyChanged` when `newValue` differs from the current
value. Setting a property to the value it already holds is a silent
no-op — no listener, no undo entry, no notification.

**Why this bit us (v0.13.1):** the transport Rewind / Stop / Play /
`seekToSample` / `seekToSeconds` commands all did
`transport.setProperty(IDs::position, 0.0, &um)` (or the target
position) and relied on the `valueTreePropertyChanged` listener in
`AudioEngine` to push the new position to `TransportManager` and on to
the frontend. When the transport was *already* at that position (e.g.
after auto-stop parked it at 0, or at startup), the property didn't
change, the listener never fired, and the button appeared dead.

**The fix pattern** — update the atomic directly AND force a property
change so the listener chain runs:

```cpp
void AudioEngineCommands::rewind()
{
    auto& tm = engine_.getTransportManager();
    tm.setCurrentSample(0);                       // immediate, always
    double cur = transport.getProperty(IDs::position, 0.0);
    if (cur != 0.0) {
        transport.setProperty(IDs::position, 0.0, &um);
    } else {
        // Nudge to a distinct value then back so the listener fires.
        transport.setProperty(IDs::position, 0.0001, &um);
        transport.setProperty(IDs::position, 0.0, &um);
    }
}
```

**Rule:** any command whose *side effect* depends on a ValueTree listener
firing must not assume `setProperty` fires when the value is unchanged.
Either (a) drive the side effect directly (call the manager method), or
(b) nudge the value. This applies to any "set to a fixed value" command
(rewind-to-zero, stop, reset-to-default), not just transport.

## Frontend transport display only updates via the `notify.transport` push

The React `transportStore` is populated exclusively by the
`notify.transport` WebSocket push (`frontend/src/main.tsx`). That push
is emitted by `FrontendServer::onTransportTimer()` (30 Hz) **and is
deduplicated** — it skips the broadcast when the payload (quantized to
centiseconds) equals the last one sent.

Consequence: if a transport command changes nothing observable (the
`setProperty` no-op above) the timer sees no diff and never pushes, so
the UI stays stale even though the user clicked a button. The two
pitfalls compound: a silent `setProperty` → no `TransportManager`
update → no payload diff → no push → dead button. Fixing the
`setProperty` side (above) restores the whole chain.

## WASAPI device scan returns empty without `CoInitialize` on the caller thread

JUCE 8's Windows WASAPI audio device type **never calls `CoInitialize`
itself**. The first `CoCreateInstance(MMDeviceEnumerator)` from a thread
that hasn't initialized COM fails with `CO_E_NOTINITIALIZED` — the
`jassert` in `juce_core/native/juce_ComSmartPtr_windows.h:133` says
exactly that ("trying to call from a thread which hasn't been
initialised with CoInitialize()") — and the resulting empty device list
is **cached** (`hasScanned = true` in `juce_WASAPI_windows.cpp`), so the
WASAPI type appears broken for the rest of the process. `DirectSound`
needs no COM, so `AudioDeviceManager::initialiseWithDefaultDevices`
silently falls through to it: "only DirectSound devices selectable",
~58 ms emulated latency, jittery callbacks → choppy/stuttering audio.

HDAW hit this after the `QApplication` → `QCoreApplication` refactor
(dd76505): Qt's GUI layer used to `OleInitialize` the main thread;
`QCoreApplication` does not, and the engine had no COM init of its own.

**The fix pattern** — `HDAW::ScopedComInit` (RAII `CoInitializeEx`,
`COINIT_MULTITHREADED`, `src/common/ScopedComInit.h`) is the first
statement of every entry point (`main`, `main_headless`, `test_main`).
It must precede any `AudioEngine` construction because the
`audio.*` RPCs (e.g. `audio.setDeviceType`) also run on the main Qt
event loop and need COM there too.

Second-order bug: the saved-device restore in `AudioEngine::initialize`
used to capture `getAudioDeviceSetup()` **before** switching the driver
type, then applied those stale device names under the new type —
DirectSound-era names ("Primary Sound Driver") don't exist under WASAPI,
`setAudioDeviceSetup` returned "No such device: Primary Sound Driver",
and the fallback re-landed on DirectSound. Restore now switches the type
first, re-fetches the setup after, and validates saved names against the
new type's device list (empty name = JUCE default).

**Rule:** any new Windows entry point that touches `AudioDeviceManager`
(or any JUCE COM path) must construct `ScopedComInit` first; when
diagnosing "only DirectSound devices show up", check COM state before
blaming the device manager. RDP sessions are an expected false alarm:
their WASAPI endpoint set is session-scoped (render-only "Remote
Audio"), which is correct, not a regression.

## PsyFm: a mod target that writes an unused value is a silent no-op — wire the destination, not a knob near it (2026-09-01)

The PsyFm track-level LFO target `306` (`FmModParamIDs::Op6Feedback`, doc'd
as "modulates OP6 feedback amount") originally wrote
`pool.feedbackLFORateHz` — the *rate* of an internal LFO — and with an
empty mod matrix that LFO wasn't routed anywhere, so **target 306 did
nothing at all**. It looked wired (a real pool field was written, no
crash, no warning) and was only exposed when a composed track's growl
showed zero movement. Same family as the `setProperty` unchanged-value
no-op: the write succeeds, the feature doesn't exist.

Fix (2026-09-01): `PsyFmModSourcePool::feedbackOffset` (additive, applied
in `PsyFmModMatrix::apply()` before routes), Track.cpp target 306 writes
the offset, and `PsyFmEngine::prepare()` seeds a default
`FeedbackLFO → Op6Feedback (0.35)` route when the matrix is empty so the
offset always lands on the audio path.

**Rule:** for every modulation/automation target, trace the written value
to the sample it changes before calling it done. A test that wiggles the
input and asserts the DSP state changed (not the pool field) is the
correct gate.

## PsyFm: voice-reclamation checks must run AFTER the render pass, not before

`PsyFmEngine::render()` originally checked `isActive()` on each voice's
envelopes *before* calling the algorithm render. Envelopes that finished
mid-block were only observed one block later — mostly harmless, but
`PsyFmEngineTest.NoteOffEventuallyDeactivatesVoice` exposed the real
problem: with a zero-release envelope the check-before-render ordering
combined with per-block envelope advancement left voices pinned `live`
longer than the test horizon in some orderings. The fix is structural,
not test-tuning: **set block params → render the block → THEN check
`isActive()` and reclaim**. A liveness decision computed from state the
current pass is about to change is always stale.

**Rule:** any per-block "is it done?" gate must read state that reflects
the work of the current block. Run it after the DSP, not before.

## PsyFm: additive mod depth on one destination needs a combined ceiling — stacked LFOs drove feedback to runaway

Two track-level LFOs routed at `Op6Feedback` (saw 1/beat depth 0.5 + sine
slow depth 0.4) summed to ±0.45 of additive offset on a 0.5 base
feedback. `PsyFmModMatrix::apply()` clamps the *combined* value to 0..1,
so the clamp was technically satisfied — at ~0.95 feedback, where the PM
feedback chain generates enormous energy. The export hard-clipped (peak
1.000, RMS pinned flat at the clamp across every loud section — a flat
RMS plateau that looked like a limiter). Each LFO alone was fine.

Current state: per-route depth is unvalidated; the sum is clamped, not
bounded by construction. Consider: per-destination combined-depth budget
in `PsyFmModMatrix`, or a soft-saturation on the mod sum.

**Rule:** when several modulators can target one destination, the failure
mode is the SUM, not the individual depths. Verify with two LFOs maxed,
not one, and read the rendered WAV (peak + RMS plateau) — the flatline is
the signature.

## Internal FX params reach DSP unclamped — one out-of-range value silenced every export at 0.6s (2026-08-31)

`TrackFXSlot` stores `param_N` values from the ValueTree and pushes them
into the internal DSP objects **raw**. A saved project carried reverb
`param_0` (Room Size, def [0,1]) = **900.0** — plausibly a units mix-up
with PsyArp's size-in-seconds param. JUCE Freeverb maps roomSize → comb
feedback ≈ `0.7 + 0.28×roomSize` ⇒ loop gain ≈ **252** ⇒ the export
render diverged exponentially (RMS 0.02 → 1098 → 7e13 → `inf` → `NaN`
within 0.6s of audio) and the WAV writer wrote NaN as **zeros**. Result:
correct-length exports with healthy audio until exactly 0.6s, then hard
silence "regardless of content" — the runaway track poisons the master
sum. Full write-up with the evidence chain:
`docs/handoffs/2026-09-17-export-silence-investigation.md`.

Three sites pushed unclamped values: `prepare()` (tree restore → DSP),
`loadParamsFromTree()`, `setInternalParam()`. `setAutomationParam()` was
already safe (denormalizes through the param defs). The signature to
recognize: an export that cuts at an *exact sample* mid-block with
full-scale clipping right before the silence — check the per-block RMS
trace (`ExportDebug` log) for `inf`/`NaN` before blaming bounds checks or
transport.

**Fix (v0.25.1):** defs-driven `clampToParamDef()` applied in `prepare()`,
`loadParamsFromTree()`, `setInternalParam()`, plus a write-side clamp in
`AudioEngineCommands::setFxSlotParam` before the `param_N` property write
(so RPC `project.setFxSlotParam` and MCP `set_internal_fx_param` can no
longer persist out-of-range values). Regression suite:
`InternalFxParamClamp` in `tests/unit/engine/internal_fx_param_clamp_test.cpp`.

**Rule:** any value that reaches recursive DSP (comb/feedback networks:
reverb, delay/chorus/phaser feedback, FM feedback) must be clamped to its
param def at EVERY entry point — tree restore, live param set, and the
property write. One unclamped path is enough to poison saved projects and
silence exports. And: a root-cause narrative written without rebuilding +
reproducing is speculation — the first "root cause" for this bug (clip
bounds check) was disproven by the project file alone (no 0.6s clips
exist; 301s clips died at 0.6s too).
