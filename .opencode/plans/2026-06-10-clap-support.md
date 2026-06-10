# CLAP Plugin Support for HDAW

## Current Status (2026-06-10)

**Infrastructure in place.** The low-level CLAP C API and `clap-helpers` C++ wrappers are
fetched and compiled as dependencies (`clap-core`, `clap-helpers`, `clap_juce_extensions`).
The `clap_juce_extensions` library is linked into HDAW.

**The host-side CLAP plugin format is NOT yet implemented.** `clap-juce-extensions` is
plugin-side only (wraps JUCE processors INTO CLAP format). For hosting CLAP plugins in a
JUCE DAW, we need to write a custom `juce::AudioPluginFormat` subclass.

## What's done

### CMake — `JUCEHelper.cmake` + `CMakeLists.txt`

- `FetchContent_Declare` + `FetchContent_MakeAvailable` for `clap-juce-extensions` repo,
  which brings in `clap`, `clap-helpers`, and `clap_juce_extensions` targets.
- `target_link_libraries(HDAW PRIVATE clap_juce_extensions)` — links all three
  transitive dependencies via the PUBLIC chain.

### Plugin scanner — CLAP search paths

`PluginManager::scanAll()` now adds platform-specific CLAP directories:
- Windows: `%ProgramFiles%\Common Files\CLAP`
- macOS: `/Library/Audio/Plug-Ins/CLAP`, `~/Library/Audio/Plug-Ins/CLAP`
- Linux: `/usr/lib/clap`, `/usr/local/lib/clap`, `~/.clap`

These dirs are iterated per-format; once a `CLAPAudioPluginFormat` exists, scanning
will pick up `.clap` files.

## Next: Implement `CLAPAudioPluginFormat`

This is a new class: `CLAPAudioPluginFormat : public juce::AudioPluginFormat`.

### Key files to create
- `src/engine/CLAPPluginFormat.h`
- `src/engine/CLAPPluginFormat.cpp`

### What it needs to implement

| `juce::AudioPluginFormat` method | CLAP equivalent / notes |
|----------------------------------|------------------------|
| `findAllTypesForFile(file)` | Load `.clap` via `dlopen`/`LoadLibrary`, get `clap_plugin_entry` via `clap_entry` symbol, enumerate factory descriptors |
| `createInstanceFromDescription(desc, ...)` | Call `factory->create_plugin()`, wrap in a `CLAPPluginInstance` |
| `fileMightContainThisPluginType(name)` | Check for `.clap` extension |
| `getName()` | Return `"CLAP"` |
| `searchPathsForPlugins(...)` | Standard CLAP dirs (already added to scanner) |
| `pluginNeedsRescanning(...)` | Check file modification time |
| `canScanForPlugins()` | `true` |
| `getDefaultLocationsToSearch()` | Platform CLAP paths |

### `CLAPPluginInstance` (inner class)

`CLAPPluginInstance : public juce::AudioPluginInstance`, wrapping a `clap_plugin*`:

- `processBlock()`: Copy JUCE buffers to CLAP audio buffers, convert
  `juce::MidiBuffer` → `clap_event_list`, call `plugin->process()`, copy back.
- `getParameters()`: Iterate `plugin->params_info()` to create JUCE
  `AudioProcessorParameter` wrappers.
- `prepareToPlay()`/`releaseResources()`: Call `plugin->activate()` / `deactivate()`.
- Editor: Bridge CLAP `clap_plugin_gui` extension to a `juce::AudioProcessorEditor`.

### CLAP helpers to use

```cpp
#include <clap/helpers/host.hh>
#include <clap/all.h>
```

- `clap::helpers::Host` base class for the host-side callbacks
- Raw `clap_plugin` and `clap_host` structs for the plugin call interface
- `clap_plugin_entry` for loading plugin .dll/.dylib/.so files

### Build configuration

The `clap` and `clap-helpers` targets already exist in the build tree via the
`clap-juce-extensions` dependency. New files just need `#include <clap/all.h>`
and linking against `clap-core` and `clap-helpers`.

Add to `CMakeLists.txt`:
```cmake
target_link_libraries(HDAW PRIVATE
    clap_juce_extensions
    clap-core
    clap-helpers
)
```
