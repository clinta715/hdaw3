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
in `src/ui/DebugLog.h`. It writes NDJSON to
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
