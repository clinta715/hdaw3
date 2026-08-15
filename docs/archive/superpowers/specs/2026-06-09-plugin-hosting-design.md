# Plugin Hosting — Phase Design

## Overview

Add basic VST3/CLAP plugin hosting to HDAW. Users can load external plugins onto track FX slots alongside built-in DSP effects. Plugin scanning, instantiation, parameter control, and state persistence are included. Native plugin editor windows and plugin parameter automation are deferred.

## Execution Order

1. PluginManager (scanning & discovery)
2. Extend TrackFXSlot for external plugins
3. ValueTree schema + state persistence
4. UI integration (FX slot combo + parameter sliders)

---

## 1. PluginManager

### Responsibility
Own the `juce::AudioPluginFormatManager` and `juce::KnownPluginList` for scanning, caching, and instantiating plugins.

### New class: `HDAW::PluginManager`
```
src/engine/PluginManager.h
src/engine/PluginManager.cpp
```

### Details
- Constructor registers `VST3Format` and `CLAPFormat` with the format manager
- `scanAll()` — triggers background scan of default VST3/CLAP locations using JUCE's `KnownPluginList::scanAndAddFile` or `scanAllTypesInDirectory` for known folders
- `getPlugins()` — returns `const std::vector<juce::PluginDescription>&`
- `createPluginInstance(const PluginDescription& desc)` — returns `std::unique_ptr<juce::AudioPluginInstance>` via `formatManager.createPluginInstance()`, called on the message thread
- Cache file at `~/.hdaw/plugins/` (JSON) via `KnownPluginList::createFromXml()/recreateFromXml()`
- First launch scans, subsequent launches load from cache + incremental scan for new files

### Integration
- `AudioEngine` owns a `PluginManager` instance
- Exposed via `AudioEngine::getPluginManager()`
- Initialized during `AudioEngine::initialize()` — loads plugin cache and starts a background scan

### Files
- **New:** `src/engine/PluginManager.h`, `src/engine/PluginManager.cpp`
- **Changed:** `src/engine/AudioEngine.h`, `src/engine/AudioEngine.cpp`

---

## 2. Extend TrackFXSlot for External Plugins

### Goal
`TrackFXSlot` can hold either a built-in DSP processor or an external `juce::AudioPluginInstance`. Processing unifies through a single interface.

### TrackFXSlot changes
- New member: `std::unique_ptr<juce::AudioPluginInstance> pluginInstance`
- New flag: `bool isExternal`
- Alternative constructor: `TrackFXSlot(std::unique_ptr<juce::AudioPluginInstance> plugin, const juce::String& pluginID)`
- `prepare()` extended: for plugin slots, call `pluginInstance->prepareToPlay(sampleRate, blockSize)`
- New unified method `process(AudioBuffer<float>&, MidiBuffer&)`:
  - DSP slots: create `dsp::AudioBlock` from buffer, process existing FX
  - Plugin slots: call `pluginInstance->processBlock(buffer, midiMessages)`
- `getPluginParameters()` — returns `AudioProcessor::getParameters()` list for UI
- `setPluginParameter(int idx, float value)` — calls `pluginInstance->setParameterNotifyingHost()`

### Track changes
- `Track::rebuildFXChain()` extended to detect `fxType == "plugin"` and create plugin-wrapping slots via `PluginManager`
- `Track::processBlock()` updated to use unified `slot->process(buffer, midiMessages)` instead of `slot->process(dsp::AudioBlock)`
- `Track::prepareToPlay()` calls `slot->prepare(playbackConfig)` for both DSP and plugin slots

### Files changed
- `src/engine/TrackFXSlot.h` — add plugin instance, unified process
- `src/engine/Track.h` — minor (optional: update processBlock signature)
- `src/engine/Track.cpp` — update processBlock loop

---

## 3. ValueTree Schema & State Persistence

### New IDs (in ProjectModel.h)
```
DECLARE_ID(pluginID)
DECLARE_ID(pluginFormat)
DECLARE_ID(pluginState)
DECLARE_ID(pluginPath)
```

### FX_SLOT node structure
```xml
<FX_SLOT fxType="plugin" bypassed="false"
         pluginID="com.u-he.Diva" pluginFormat="VST3"
         pluginPath="C:/VST3/Diva.vst3">
  <pluginState binary="base64encoded..." />
</FX_SLOT>
```

### State lifecycle
1. **Loading:** User selects a plugin in FX slot → `fxType` set to `"plugin"`, `pluginID`/`pluginFormat`/`pluginPath` set from `PluginDescription`
2. **Instantiation:** `Track::rebuildFXChain()` detects `fxType == "plugin"`, calls `PluginManager::createPluginInstance()` with the stored description
3. **State restore:** After instantiation, checks for `pluginState` property on the node and calls `pluginInstance->setStateInformation()` if present
4. **State save (deferred to save/load phase):** `getStateInformation()` called on the plugin and stored to the node as base64 binary
5. **UI persistence:** Currently the plugin state stays in memory — when the FX chain is rebuilt (slot add/remove/reorder), state is preserved because we store it to the tree before destroying the slot

### Persistence strategy for basic scope
Since we don't have project save/load yet, plugin state is preserved within a session via ValueTree in-memory. When the user reorders or modifies slots, the `rebuildFXChain` flow saves state to the tree before destroying old slots and restores it when creating new ones. This is added to `Track::rebuildFXChain()` — before clearing the chain, iterate existing slots and write `pluginInstance->getStateInformation()` to the ValueTree property if the slot is a plugin.

### Files changed
- `src/model/ProjectModel.h` — add new IDs
- `src/engine/Track.cpp` — preserve state before rebuild

---

## 4. UI Integration

### FXSlotRow changes
- Combo box extended with a separator and available plugins from `PluginManager::getPlugins()`
- Plugin names displayed as `"[VST3] Diva"` or just `"Diva"` for brevity
- When a plugin is selected, the combo emits `slotChanged()` which triggers `chainChanged()` → `rebuildFXChain()`

### Plugin Parameter Panel
- When an FX slot contains a plugin, a collapsible parameter section appears below the row
- Implementation: `FXSlotRow` gets a new `QWidget* paramContainer` that shows/hides on toggle
- Each parameter: `QLabel` (name) + `QSlider` (horizontal, 0-1000 mapped to 0-1) + `QLabel` (value text)
- Sliders are populated from `AudioProcessor::getParameters()` — iterate, get name, range
- `sliderValueChanged` → call `TrackFXSlot::setPluginParameter(idx, value)` → sends through the plugin's param system (direct call since UI thread is also the message thread)
- Parameters are NOT yet automated (deferred)

### Plugin browser dialog (future)
- For basic scope, external plugins appear directly in the FX slot combo box
- A dedicated plugin browser dialog is deferred

### Files changed
- `src/ui/FXSlotRow.h` — add param container, plugin data
- `src/ui/FXSlotRow.cpp` — populate combo with plugins, render param sliders
- `src/ui/FXChainWidget.h` — pass engine/plugin manager ref
- `src/ui/FXChainWidget.cpp` — minor wiring

---

## Testing (Verification)

1. **Plugin scanning:** Launch app, open FX chain on a track, verify external plugins appear in the type combo
2. **Plugin loading:** Select a VST3/CLAP plugin in the combo, verify it instantiates and processes audio
3. **Bypass:** Toggle bypass on a plugin slot, verify audio passes through unaffected
4. **Parameters:** Verify parameter sliders appear and values change the sound
5. **Chain reorder:** Move plugin slots up/down, verify state is preserved
6. **Chain delete:** Remove a plugin slot, verify no dangling references
