# CLAP Host Plugin Format for HDAW

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    CLAPPluginFormat                             │
│  (juce::AudioPluginFormat subclass)                             │
│                                                                 │
│  - findAllTypesForFile()       → DL .clap, query factory        │
│  - createPluginInstance()      → wrap clap_plugin               │
│  - searchPathsForPlugins()     → standard CLAP dirs             │
│  - getName()                   → "CLAP"                         │
│  + fileMightContainThisType() / needsRescanning / etc.          │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    CLAPPluginInstance                            │
│  (juce::AudioPluginInstance subclass)                           │
│                                                                 │
│  Owns: clap_plugin*, HMODULE, clap_plugin_entry, CLAPHost*     │
│                                                                 │
│  prepareToPlay() → activate + start_processing                  │
│  processBlock()  → bridge audio/MIDI → clap_process_t           │
│  createEditor()  → CLAPPluginEditor                             │
│  getState/setState → clap_plugin_state                          │
│  fillInPluginDescription()                                      │
└─────────────────────────────────────────────────────────────────┘
                              │
          ┌───────────────────┼───────────────────┐
          ▼                   ▼                   ▼
┌─────────────────┐ ┌─────────────────┐ ┌─────────────────────┐
│   CLAPHost      │ │  CLAPParameter  │ │  CLAPPluginEditor   │
│ (clap::helpers  │ │ (HostedAudio    │ │ (AudioProcessor     │
│ ::Host subclass)│ │ ProcessorParam) │ │  Editor subclass)   │
│                 │ │                 │ │                     │
│ Thread checks   │ │ Contains:       │ │ Embeds HWND/NSView  │
│ Gui resize req  │ │ clap_param_info │ │ Handles resize      │
│ Params rescan   │ │ value cache     │ │ Timer for UI update │
│ Timer support   │ │ getValue/set    │ │                     │
│ State mark_dirty│ │ Value → flush() │ │                     │
└─────────────────┘ └─────────────────┘ └─────────────────────┘
```

## New Files

### 1. `src/engine/CLAPPluginFormat.h`
### 2. `src/engine/CLAPPluginFormat.cpp`
### 3. `src/engine/CLAPPluginInstance.h`
### 4. `src/engine/CLAPPluginInstance.cpp`
### 5. `src/engine/CLAPPluginEditor.h`
### 6. `src/engine/CLAPPluginEditor.cpp`

## Detailed Design

### CLAPPluginFormat

```cpp
class CLAPPluginFormat : public juce::AudioPluginFormat
{
    juce::String getName() const override { return "CLAP"; }
    bool canScanForPlugins() const override { return true; }
    bool isTrivialToScan() const override { return false; }

    void findAllTypesForFile(juce::OwnedArray<juce::PluginDescription>& results,
                             const juce::String& fileOrIdentifier) override;
    // 1. DL: LoadLibrary(fileOrIdentifier) → HMODULE
    // 2. GetProcAddress("clap_entry") → clap_plugin_entry_t
    // 3. entry->init(filePath)
    // 4. entry->get_factory(CLAP_PLUGIN_FACTORY_ID) → clap_plugin_factory_t
    // 5. For i in 0..factory->get_plugin_count()-1:
    //      Get descriptor, create PluginDescription
    //      Populate: name, manufacturerName, version, fileOrIdentifier,
    //                pluginFormatName="CLAP", numInputChannels=0/2,
    //                numOutputChannels=0/2, isInstrument, category
    // 6. entry->deinit()
    // 7. FreeLibrary(module)

    bool fileMightContainThisPluginType(const juce::String& fileOrIdentifier) override;
    // Check for ".clap" extension

    juce::String getNameOfPluginFromIdentifier(const juce::String& fileOrIdentifier) override;
    // Return file name without extension

    bool pluginNeedsRescanning(const juce::PluginDescription& desc) override;
    // Compare desc.lastFileModTime with file's actual mtime

    bool doesPluginStillExist(const juce::PluginDescription& desc) override;
    // File::exists()

    juce::StringArray searchPathsForPlugins(const juce::FileSearchPath& directoriesToSearch,
                                            bool recursive, bool) override;
    // Search for *.clap files in given dirs

    juce::FileSearchPath getDefaultLocationsToSearch() override;
    // Platform standard CLAP dirs

protected:
    void createPluginInstance(const juce::PluginDescription& desc,
                              double sampleRate, int blockSize,
                              PluginCreationCallback callback) override;
    // 1. LoadLibrary(desc.fileOrIdentifier)
    // 2. entry->init(path)
    // 3. factory->create_plugin(host, desc.name/uid)
    // 4. clap_plugin->init()
    // 5. Query extensions (params, audio-ports, note-ports, state, gui)
    // 6. Create CLAPPluginInstance(clap_plugin, module, entry, host)
    // 7. Callback with instance

    bool requiresUnblockedMessageThreadDuringCreation(const juce::PluginDescription&) const override
    { return false; }  // CLAP init is blocking but main-thread safe
};
```

#### CLAP module loading details

```cpp
struct CLAPModule {
    void* handle = nullptr;               // HMODULE on Windows, void* on Linux/macOS
    clap_plugin_entry_t entry;            // The clap_entry struct
    clap_plugin_factory_t* factory = nullptr;
    bool initialized = false;
    juce::String error;

    ~CLAPModule() { unload(); }

    bool load(const juce::String& path);
    void unload();
};
```

- `load()`: `LoadLibraryEx(path, 0, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)` on Windows, `dlopen(path, RTLD_NOW | RTLD_LOCAL)` on other platforms
- `unload()`: call `entry.deinit()` if initialized, then `FreeLibrary`/`dlclose`
- The module is kept alive as long as any plugin instance exists (reference counting via shared_ptr or similar)

### CLAPHost

```cpp
class CLAPHost : public clap::helpers::Host<clap::helpers::MisbehaviourHandler::Terminate,
                                             clap::helpers::CheckingLevel::Maximal>
{
public:
    CLAPHost(CLAPPluginInstance& instance);

    // Thread check
    bool threadCheckIsMainThread() const noexcept override;
    bool threadCheckIsAudioThread() const noexcept override;

    // Required host callbacks
    void requestRestart() noexcept override;
    void requestProcess() noexcept override;
    void requestCallback() noexcept override;

    // Extensions
    const void* getExtension(const char* id) const noexcept override;

    // Extension overrides
    bool implementsParams() const noexcept override { return true; }
    void paramsRescan(clap_param_rescan_flags flags) noexcept override;
    void paramsClear(clap_id paramId, clap_param_clear_flags flags) noexcept override;
    void paramsRequestFlush() noexcept override;

    bool implementsState() const noexcept override { return true; }
    void stateMarkDirty() noexcept override;

    bool implementsGui() const noexcept override { return true; }
    void guiClosed(bool wasDestroyed) noexcept override;
    bool guiRequestResize(uint32_t w, uint32_t h) noexcept override;

    // Accessor
    const clap_host* getClapHost() const { return clapHost(); }

private:
    CLAPPluginInstance& instance;
    std::atomic<bool> needsRestart{false};
    std::atomic<bool> needsFlush{false};
};
```

### CLAPPluginInstance

```cpp
class CLAPPluginInstance : public juce::AudioPluginInstance
{
public:
    CLAPPluginInstance(std::shared_ptr<CLAPModule> module,
                       const clap_plugin_t* plugin,
                       std::unique_ptr<CLAPHost> host);
    ~CLAPPluginInstance() override;

    // AudioProcessor required
    const juce::String getName() const override;
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
    double getTailLengthSeconds() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    // AudioPluginInstance required
    void fillInPluginDescription(juce::PluginDescription& desc) const override;

    // Program (likely 1, no-op)
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    // State via CLAP state extension
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Expose the clap_plugin* via getExtensions
    void getExtensions(ExtensionsVisitor& visitor) const override;

    // Accessors used by CLAPHost
    CLAPHost& getHost() const { return *host; }
    const clap_plugin_t* getClapPlugin() const { return plugin; }
    clap_plugin_gui_t* getGuiExtension() const { return guiExt; }

private:
    std::shared_ptr<CLAPModule> module;     // keeps module loaded
    const clap_plugin_t* plugin;            // from factory
    std::unique_ptr<CLAPHost> host;         // host callbacks

    // Cached extensions
    const clap_plugin_params_t* paramsExt = nullptr;
    const clap_plugin_state_t* stateExt = nullptr;
    const clap_plugin_gui_t* guiExt = nullptr;
    const clap_plugin_audio_ports_t* audioPortsExt = nullptr;
    const clap_plugin_note_ports_t* notePortsExt = nullptr;

    // Audio configuration
    juce::AudioBuffer<float> scratchBuffer;
    int numInputChannels = 0;
    int numOutputChannels = 0;

    // Parameter cache
    std::vector<std::unique_ptr<CLAPParameter>> parameters;

    bool isActive = false;     // clap_plugin->activate() called
    bool isProcessing = false; // clap_plugin->start_processing() called

    void buildParameters();
    void buildBuses();
    void processEvents(const juce::MidiBuffer& midi,
                       clap_input_events_t& inEvents,
                       clap_output_events_t& outEvents);
};
```

### CLAPParameter

```cpp
class CLAPParameter : public juce::AudioProcessorParameter
{
public:
    CLAPParameter(const clap_plugin_t* plugin,
                  const clap_plugin_params_t* paramsExt,
                  const clap_param_info_t& info);

    float getValue() const override;
    void setValue(float normalized) override;
    float getDefaultValue() const override;
    juce::String getName(int maxLen) const override;
    juce::String getLabel() const override;
    juce::String getText(float value, int maxLen) const override;
    float getValueForText(const juce::String& text) const override;

    clap_id getParamID() const { return info.id; }
    const clap_param_info_t& getInfo() const { return info; }

    // Called during parameter flush in processBlock
    void setTargetValue(double newValue) { targetValue = newValue; }

private:
    const clap_plugin_t* plugin;
    const clap_plugin_params_t* params;
    clap_param_info_t info;
    std::atomic<double> currentValue{0.0};
    std::atomic<double> targetValue{0.0};
};
```

### Audio → CLAP Bridge (`processBlock` flow)

```cpp
void CLAPPluginInstance::processBlock(juce::AudioBuffer<float>& buffer,
                                      juce::MidiBuffer& midiMessages)
{
    if (!isProcessing)
        return;

    // 1. Build clap_process_t
    clap_process_t process;
    std::memset(&process, 0, sizeof(process));

    process.steady_time = getPlayHead() ? ... : -1;  // sample position
    process.frames_count = buffer.getNumSamples();

    // 2. Audio buffers
    clap_audio_buffer_t audioInputs[1];
    clap_audio_buffer_t audioOutputs[1];

    if (numInputChannels > 0) {
        audioInputs[0].data32 = buffer.getArrayOfReadPointers();
        audioInputs[0].channel_count = numInputChannels;
        audioInputs[0].latency = 0;
        audioInputs[0].constant_mask = 0;
        process.audio_inputs = audioInputs;
        process.audio_inputs_count = 1;
    }

    if (numOutputChannels > 0) {
        audioOutputs[0].data32 = buffer.getArrayOfWritePointers();
        audioOutputs[0].channel_count = numOutputChannels;
        audioOutputs[0].latency = 0;
        audioOutputs[0].constant_mask = 0;
        process.audio_outputs = audioOutputs;
        process.audio_outputs_count = 1;
    }

    // 3. Input events
    std::vector<uint8_t> eventStorage;  // backing storage for in_events
    CLAPInputEvents inEvents(eventStorage);
    convertMIDIToCLAP(midiMessages, inEvents, process.frames_count);
    process.in_events = &inEvents;

    // 4. Output events
    std::vector<clap_event_header_t> outEventStorage;
    CLAPOutputEvents outEvents(outEventStorage);
    process.out_events = &outEvents;

    // 5. Flush pending parameter changes
    for (auto& p : parameters)
        p->applyPendingChange(inEvents);

    // 6. Transport
    clap_event_transport_t transport{};
    fillTransport(transport);
    if (transport.flags != 0)
        ...  // add transport event to in_events at time 0

    // 7. Process
    auto status = plugin->process(plugin, &process);

    // 8. Read output events → MIDI (if plugin produces MIDI)
    // (Read from outEventStorage and convert to midiMessages)

    // 9. Handle status
    if (status == CLAP_PROCESS_ERROR)
        buffer.clear();

    // 10. Enqueue parameter flush if requested
    if (host->needsFlush.load()) {
        host->needsFlush = false;
        // Handle pending param changes
    }
}
```

### Event List Wrappers

```cpp
class CLAPInputEvents : public clap_input_events
{
public:
    CLAPInputEvents(std::vector<uint8_t>& storage)
        : storage(storage)
    {
        ctx = this;
        size = [](const clap_input_events* list) -> uint32_t {
            auto& s = *reinterpret_cast<const std::vector<uint8_t>*>(
                static_cast<const CLAPInputEvents*>(list)->storage);
            return ...;
        };
        get = [](const clap_input_events* list, uint32_t index) -> const clap_event_header_t* {
            ...
        };
    }

    void push(const clap_event_header_t& event);
    const clap_event_header_t* at(uint32_t index) const;

private:
    std::vector<uint8_t>& storage;  // owns the event data
};

class CLAPOutputEvents : public clap_output_events
{
public:
    CLAPOutputEvents(std::vector<clap_event_header_t>& storage)
        : storage(storage)
    {
        ctx = this;
        try_push = [](const clap_output_events* list,
                      const clap_event_header_t* event) -> bool {
            auto& s = static_cast<const CLAPOutputEvents*>(list)->storage;
            s.push_back(*event);
            return true;
        };
    }

private:
    std::vector<clap_event_header_t>& storage;
};
```

Wait — there's a problem with the event list design. The `clap_input_events_t` requires events to be sorted by sample time, and `get()` returns a pointer to memory that must remain valid until `process()` returns. The storage vectors must live on the stack (or be member buffers pre-allocated).

A better approach: pre-allocate a fixed-size event array as a class member, and fill it during processBlock.

Let me revise: instead of vectors in the event list wrappers, use a pre-allocated ring buffer or fixed array. Actually, the simplest approach is to allocate on the stack during processBlock — events are small and there are typically few per block.

### CLAPPluginEditor

```cpp
class CLAPPluginEditor : public juce::AudioProcessorEditor
{
public:
    CLAPPluginEditor(CLAPPluginInstance& instance);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void createUI();
    void destroyUI();

    CLAPPluginInstance& clapInstance;
    const clap_plugin_gui_t* guiExt;
    bool uiCreated = false;
    juce::Component* embeddedComponent = nullptr;

    // Windows: HWND for the plugin window
    // macOS: NSView*
};
```

The editor lifecycle:
1. `create()`: Call `guiExt->create(plugin, api, isFloating)`
2. Get the plugin's preferred size: `guiExt->get_size(plugin, &w, &h)`
3. Create a native child window/component
4. `guiExt->set_parent(plugin, &window)` to embed it
5. `guiExt->show(plugin)` or `guiExt->hide(plugin)`
6. On resize: `guiExt->set_size(plugin, w, h)` 
7. On destroy: `guiExt->destroy(plugin)`

For the window API:
- Windows: `CLAP_WINDOW_API_WIN32` → provide HWND
- macOS: `CLAP_WINDOW_API_COCOA` → provide NSView*
- Linux: `CLAP_WINDOW_API_X11` → provide X11 window

JUCE provides `juce::Component::getHWND()` on Windows and `juce::Component::getView()->view` on macOS to get the native window handles.

## CMake Changes

New source files need to be added to `add_executable(HDAW ...)` in `CMakeLists.txt`:

```cmake
add_executable(HDAW
    ...
    src/engine/CLAPPluginFormat.h
    src/engine/CLAPPluginFormat.cpp
    src/engine/CLAPPluginInstance.h
    src/engine/CLAPPluginInstance.cpp
    src/engine/CLAPPluginEditor.h
    src/engine/CLAPPluginEditor.cpp
    ...
)
```

No new link dependencies needed — `clap_juce_extensions` already brings in `clap-core` and `clap-helpers` transitively.

## PluginManager integration

In `PluginManager::PluginManager()`:
```cpp
formatManager.addFormat(new juce::VST3PluginFormat());
formatManager.addFormat(new CLAPPluginFormat());  // <-- add this
```

The CLAP search paths already exist from the previous implementation phase. `scanAll()` will pick up `.clap` files via the CLAP format's `findAllTypesForFile`.

## Estimated size

| File | Lines |
|------|-------|
| `CLAPPluginFormat.h` | ~60 |
| `CLAPPluginFormat.cpp` | ~150 (module loading + scanning) |
| `CLAPPluginInstance.h` | ~120 |
| `CLAPPluginInstance.cpp` | ~500 (process, params, state, buses) |
| `CLAPPluginEditor.h` | ~40 |
| `CLAPPluginEditor.cpp` | ~200 (UI create/embed/resize) |
| **Total** | **~1070** |

## Thread safety summary

| JUCE method | Thread | CLAP method called | Thread requirement |
|---|---|---|---|
| `prepareToPlay` | message | `plugin->activate()` | [main-thread & !active] |
| `releaseResources` | message | `plugin->deactivate()` | [main-thread & active] |
| `processBlock` | audio | `plugin->process()` | [audio-thread & active & processing] |
| `processBlock` (first call after `prepareToPlay`) | audio | `plugin->start_processing()` | [audio-thread & active & !processing] |
| `processBlock` (last call before `releaseResources`) | audio | `plugin->stop_processing()` | [audio-thread & active & processing] |
| `reset` | audio | `plugin->reset()` | [audio-thread & active] |
| `createEditor` | message | `guiExt->create/set_parent/show` | [main-thread] |
| `getStateInformation` | message | `stateExt->save()` | [main-thread] |
| `setStateInformation` | message | `stateExt->load()` | [main-thread] |

## Key risks and mitigations

1. **Module lifetime**: Multiple PluginDescriptions may reference the same .clap file (shell plugins). Keep a shared_ptr<CLAPModule> per loaded file, reference counted by CLAPPluginInstance.

2. **processBlock memory allocation**: Pre-allocate event arrays as member buffers to avoid vector growth during audio processing. The maximum events per block is bounded (typically < 256).

3. **Parameter flush race**: The `paramsRequestFlush()` callback can be called from any non-audio thread. The CLAPHost::paramsRequestFlush() sets an atomic flag; the next processBlock drains it via the pending parameter event queue.

4. **Editor resize**: `guiRequestResize` is [thread-safe] — may be called from audio thread. Must use `juce::MessageManager::callAsync()` to resize the editor component on the message thread.

5. **GUI floating mode**: Some CLAP plugins prefer floating windows. Our editor should support both embedded (set_parent) and floating (set_transient + suggest_title) modes.
