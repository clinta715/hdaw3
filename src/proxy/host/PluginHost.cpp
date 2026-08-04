#include "PluginHost.h"
#include "engine/CLAPPluginFormat.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include <cstdlib>
#include <cstring>

namespace {

class PassthroughProcessor : public juce::AudioPluginInstance
{
public:
    PassthroughProcessor()
        : AudioPluginInstance(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

    const juce::String getName() const override { return "Passthrough"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        (void)buffer;
    }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& dest) override
    {
        const char marker[] = "PASSTHROUGH";
        dest.setSize(sizeof(marker));
        std::memcpy(dest.getData(), marker, sizeof(marker));
    }
    void setStateInformation(const void*, int) override {}
    void fillInPluginDescription(juce::PluginDescription& d) const override
    {
        d.name = "Passthrough";
        d.pluginFormatName = "Internal";
        d.fileOrIdentifier = "__passthrough__";
    }
};

class CrashingProcessor : public juce::AudioPluginInstance
{
public:
    CrashingProcessor()
        : AudioPluginInstance(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

    const juce::String getName() const override { return "CrashTest"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override
    {
        std::_Exit(3);
    }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
    void fillInPluginDescription(juce::PluginDescription& d) const override
    {
        d.name = "CrashTest";
        d.pluginFormatName = "Internal";
        d.fileOrIdentifier = "__crash__";
    }
};

class BlockSizeProbeProcessor : public juce::AudioPluginInstance
{
public:
    BlockSizeProbeProcessor()
        : AudioPluginInstance(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

    const juce::String getName() const override { return "BlockSizeProbe"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int s = 0; s < buffer.getNumSamples(); ++s)
                buffer.setSample(ch, s, static_cast<float>(buffer.getNumSamples()));
    }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
    void fillInPluginDescription(juce::PluginDescription& d) const override
    {
        d.name = "BlockSizeProbe";
        d.pluginFormatName = "Internal";
        d.fileOrIdentifier = "__blocksize__";
    }
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// EditorWindow — a DocumentWindow that hosts the plugin's native GUI.
// Listens for close button and notifies PluginHost so EDITOR_CLOSED can be
// sent back to the parent process.
// ---------------------------------------------------------------------------
class PluginHost::EditorWindow : public juce::DocumentWindow
{
public:
    EditorWindow(PluginHost& h, juce::AudioProcessorEditor* editor)
        : DocumentWindow("Plugin Editor",
                         juce::Colours::darkgrey,
                         DocumentWindow::closeButton |
                         DocumentWindow::minimiseButton |
                         DocumentWindow::maximiseButton),
          host(h)
    {
        setContentOwned(editor, true);
        setResizable(true, true);
        setResizeLimits(100, 60, 4000, 3000);

        auto border = getBorderThickness();
        int titleH = getTitleBarHeight();
        centreWithSize(editor->getWidth() + border.getLeftAndRight(),
                       editor->getHeight() + titleH + border.getTopAndBottom());

        setVisible(true);
        setAlwaysOnTop(true);
        toFront(true);
    }

    ~EditorWindow() override
    {
        // Detach content without deleting it (the plugin owns the editor)
        setContentOwned(nullptr, false);
    }

    void closeButtonPressed() override
    {
        host.onEditorWindowClosed(false);
    }

private:
    PluginHost& host;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EditorWindow)
};

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------
PluginHost::PluginHost(uint32_t id, const std::string& pipe,
                       const std::string& shm, const std::string& plugin)
    : slotId(id), pipeName(pipe), shmName(shm), pluginPath(plugin),
      pipe(pipeName)
{
    formatManager.addFormat(new juce::VST3PluginFormat());
    formatManager.addFormat(new CLAPPluginFormat());
}

PluginHost::~PluginHost()
{
    running.store(false);

    if (controlThread.joinable()) controlThread.join();
    if (audioThread.joinable()) audioThread.join();

    destroyEditorWindow();
    plugin.reset();
}

// ---------------------------------------------------------------------------
// Main entry point — runs on the main thread.
//
// Architecture:
//   main thread   → JUCE message dispatch loop (GUI events, OS messages)
//   controlThread → pipe receive loop (IPC with parent)
//   audioThread   → shared-memory audio processing
//
// The pipe thread receives SHOW_EDITOR / CLOSE_EDITOR messages and queues
// them to the GUI thread via MessageManager::callAsync().
// ---------------------------------------------------------------------------
int PluginHost::run()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (!pipe.connect()) return 1;
    if (!shm.open(shmName)) return 1;

    proxy::ProxyResponse readyResp{};
    readyResp.type = proxy::MessageType::READY;
    readyResp.result = 1;
    if (!pipe.sendResp(readyResp)) return 1;

    if (!loadPlugin()) return 1;

    controlThread = std::thread(&PluginHost::controlLoop, this);

    audioThread = std::thread(&PluginHost::audioLoop, this);

    {
        std::lock_guard<std::mutex> lock(guiMutex);
        guiQueueReady = true;
    }

    // Main thread: run JUCE's message dispatch loop.  This pumps the Win32
    // message queue and runs all callbacks posted via MessageManager.
    juce::MessageManager::getInstance()->runDispatchLoopUntil(-1);

    std::_Exit(proxy::GRACEFUL_EXIT_CODE);
}

// ---------------------------------------------------------------------------
// controlLoop — runs on a background thread, receives pipe messages.
// ---------------------------------------------------------------------------
void PluginHost::controlLoop()
{
    while (running.load()) {
        proxy::ProxyMessage msg{};
        if (!pipe.receiveMsg(msg)) {
            running.store(false);
            break;
        }

        switch (msg.type) {
            case proxy::MessageType::SHUTDOWN:
                running.store(false);
                break;

            case proxy::MessageType::PREPARE: {
                if (msg.dataSize >= 16) {
                    struct PrepareData {
                        double sampleRate;
                        int32_t blockSize;
                        int32_t numChannels;
                    };
                    PrepareData data{};
                    std::memcpy(&data, msg.data, sizeof(data));
                    preparedSampleRate = data.sampleRate;
                    preparedBlockSize = data.blockSize;
                    preparedNumChannels = data.numChannels;

                    if (plugin) {
                        plugin->prepareToPlay(preparedSampleRate, preparedBlockSize);
                        pluginLoaded.store(true);
                    }
                }
                proxy::ProxyResponse r{};
                r.type = proxy::MessageType::PREPARE_RESULT;
                r.result = 1;
                pipe.sendResp(r);
                break;
            }

            case proxy::MessageType::SET_STATE: {
                if (plugin && msg.dataSize > 0) {
                    plugin->setStateInformation(msg.data, static_cast<int>(msg.dataSize));
                }
                proxy::ProxyResponse resp{};
                resp.type = proxy::MessageType::SET_STATE;
                resp.result = 1;
                pipe.sendResp(resp);
                break;
            }

            case proxy::MessageType::GET_STATE: {
                proxy::ProxyResponse resp{};
                resp.type = proxy::MessageType::GET_STATE_RESULT;
                if (plugin) {
                    juce::MemoryBlock block;
                    plugin->getStateInformation(block);
                    resp.dataSize = static_cast<uint32_t>(
                        std::min(block.getSize(), sizeof(resp.data)));
                    std::memcpy(resp.data, block.getData(), resp.dataSize);
                    resp.result = 1;
                } else {
                    resp.result = 0;
                }
                pipe.sendResp(resp);
                break;
            }

            case proxy::MessageType::SET_PARAM: {
                proxy::ProxyResponse resp{};
                resp.type = proxy::MessageType::SET_PARAM;
                resp.result = 1;
                pipe.sendResp(resp);
                break;
            }

            case proxy::MessageType::GET_PARAM: {
                proxy::ProxyResponse resp{};
                resp.type = proxy::MessageType::GET_PARAM_RESULT;
                resp.result = plugin ? 1 : 0;
                pipe.sendResp(resp);
                break;
            }

            case proxy::MessageType::GET_PARAM_COUNT: {
                proxy::ProxyResponse resp{};
                resp.type = proxy::MessageType::GET_PARAM_COUNT_RESULT;
                if (plugin) {
                    uint32_t count = static_cast<uint32_t>(plugin->getNumParameters());
                    std::memcpy(resp.data, &count, sizeof(count));
                    resp.dataSize = sizeof(count);
                    resp.result = 1;
                } else {
                    resp.result = 0;
                }
                pipe.sendResp(resp);
                break;
            }

            case proxy::MessageType::GET_PARAM_INFO: {
                proxy::ProxyResponse resp{};
                resp.type = proxy::MessageType::GET_PARAM_INFO_RESULT;
                resp.result = plugin ? 1 : 0;
                pipe.sendResp(resp);
                break;
            }

            case proxy::MessageType::SHOW_EDITOR: {
                editorVisible.store(true);
                juce::MessageManager::callAsync([this] { openEditorOnGUIThread(); });
                proxy::ProxyResponse r{};
                r.type = proxy::MessageType::SHOW_EDITOR;
                r.result = 1;
                pipe.sendResp(r);
                break;
            }

            case proxy::MessageType::CLOSE_EDITOR: {
                editorVisible.store(false);
                parentInitiatedClose = true;
                juce::MessageManager::callAsync([this] { closeEditorOnGUIThread(); });
                proxy::ProxyResponse r{};
                r.type = proxy::MessageType::CLOSE_EDITOR;
                r.result = 1;
                pipe.sendResp(r);
                break;
            }

            case proxy::MessageType::HEARTBEAT: {
                auto* hdr = shm.getHeader();
                if (hdr) hdr->childAlive.store(static_cast<uint32_t>(
                    std::chrono::steady_clock::now().time_since_epoch().count()));
                proxy::ProxyResponse r{};
                r.type = proxy::MessageType::HEARTBEAT;
                r.result = 1;
                pipe.sendResp(r);
                break;
            }

            default:
                break;
        }
    }

    // Tell the GUI thread to shut down
    juce::MessageManager::getInstance()->stopDispatchLoop();
}

// ---------------------------------------------------------------------------
// audioLoop — runs on a dedicated thread, reads/writes shared-memory rings.
// ---------------------------------------------------------------------------
void PluginHost::audioLoop()
{
    auto* hdr = shm.getHeader();
    if (!hdr || !plugin) return;

    hdr->numChannels = static_cast<uint32_t>(preparedNumChannels);
    hdr->blockSize = static_cast<uint32_t>(preparedBlockSize);
    hdr->sampleRate = static_cast<uint32_t>(preparedSampleRate);

    if (hdr->capacity == 0) {
        uint32_t cap = 1;
        while (cap < static_cast<uint32_t>(preparedBlockSize * preparedNumChannels)) cap <<= 1;
        hdr->capacity = cap;
    }

    juce::AudioBuffer<float> inputBuffer(preparedNumChannels, preparedBlockSize);
    juce::AudioBuffer<float> outputBuffer(preparedNumChannels, preparedBlockSize);
    juce::MidiBuffer midiBuffer;

    while (running.load()) {
        // The PREPARE message can arrive after this thread started (when
        // preparedBlockSize was still the constructor default). Ensure the
        // scratch buffers match the prepared width so processBlock runs on the
        // same block size the transport and plugin->prepareToPlay use. Without
        // this, a synth configured for 441-sample blocks gets a 512-sample
        // buffer — pitch-up + stale tail + wrong MIDI offsets (audible squeak).
        if (inputBuffer.getNumSamples() != preparedBlockSize) {
            inputBuffer.setSize(preparedNumChannels, preparedBlockSize);
            outputBuffer.setSize(preparedNumChannels, preparedBlockSize);
            // Keep the shared-memory header in sync with the width the child
            // actually processes (it is informational to the parent; the ring
            // math uses hdr->capacity, not hdr->blockSize).
            hdr->blockSize = static_cast<uint32_t>(preparedBlockSize);
        }
        uint32_t cap = hdr->capacity;
        uint32_t r = hdr->inputReadPos.load(std::memory_order_relaxed);
        uint32_t w = hdr->inputWritePos.load(std::memory_order_acquire);

        if (w - r >= static_cast<uint32_t>(preparedBlockSize * preparedNumChannels)) {
            float* inRing = shm.getInputRing();
            float* outRing = shm.getOutputRing();

            for (int ch = 0; ch < preparedNumChannels; ++ch) {
                for (int s = 0; s < preparedBlockSize; ++s)
                    inputBuffer.setSample(ch, s, inRing[(r + ch * preparedBlockSize + s) & (cap - 1)]);
            }
            hdr->inputReadPos.store(r + static_cast<uint32_t>(preparedBlockSize * preparedNumChannels), std::memory_order_release);

            midiBuffer.clear();
            proxy::MidiEvent* midiIn = shm.getMidiInRing();
            if (midiIn) {
                constexpr uint32_t midiCap = 256;
                uint32_t mw = hdr->midiInWritePos.load(std::memory_order_relaxed);
                uint32_t mr = hdr->midiInReadPos.load(std::memory_order_acquire);
                uint32_t avail = (mw >= mr) ? (mw - mr) : 0;
                uint32_t toRead = (std::min)(avail, midiCap);
                for (uint32_t i = 0; i < toRead; ++i) {
                    const proxy::MidiEvent& evt = midiIn[(mr + i) & 0xFF];
                    midiBuffer.addEvent(
                        juce::MidiMessage(evt.data[0], evt.data[1], evt.data[2]),
                        static_cast<int>(evt.sampleOffset));
                }
                hdr->midiInReadPos.store(mr + toRead, std::memory_order_release);
            }

            plugin->processBlock(inputBuffer, midiBuffer);

            hdr->audioFramesProduced.fetch_add(preparedBlockSize, std::memory_order_relaxed);
            hdr->audioBlocksProcessed.fetch_add(1, std::memory_order_relaxed);

            proxy::MidiEvent* midiOut = shm.getMidiOutRing();
            if (midiOut) {
                constexpr uint32_t midiCap = 256;
                uint32_t mw = hdr->midiOutWritePos.load(std::memory_order_relaxed);
                uint32_t mr = hdr->midiOutReadPos.load(std::memory_order_acquire);
                uint32_t space = midiCap - ((mw >= mr) ? (mw - mr) : 0);
                uint32_t written = 0;
                for (const auto metadata : midiBuffer) {
                    if (written >= space) break;
                    const auto msg = metadata.getMessage();
                    proxy::MidiEvent& evt = midiOut[(mw + written) & 0xFF];
                    evt.sampleOffset = static_cast<uint32_t>(metadata.samplePosition);
                    const auto* bytes = msg.getRawData();
                    evt.data[0] = bytes[0];
                    evt.data[1] = bytes[1];
                    evt.data[2] = bytes[2];
                    evt._pad = 0;
                    ++written;
                }
                hdr->midiOutWritePos.store(mw + written, std::memory_order_release);
            }

            uint32_t ow = hdr->outputWritePos.load(std::memory_order_relaxed);
            for (int ch = 0; ch < preparedNumChannels; ++ch) {
                for (int s = 0; s < preparedBlockSize; ++s)
                    outRing[(ow + ch * preparedBlockSize + s) & (cap - 1)] = inputBuffer.getSample(ch, s);
            }
            hdr->outputWritePos.store(ow + static_cast<uint32_t>(preparedBlockSize * preparedNumChannels), std::memory_order_release);
        } else {
            static thread_local int spinCount = 0;
            if ((++spinCount & 63) == 0)
                Sleep(0);
            else
                std::this_thread::yield();
        }
    }
}

// ---------------------------------------------------------------------------
// Plugin loading
// ---------------------------------------------------------------------------
bool PluginHost::loadPlugin() {
    return loadPluginByPath(juce::String(pluginPath));
}

bool PluginHost::loadPluginByPath(const juce::String& path) {
    if (path == "__passthrough__") {
        plugin = std::make_unique<PassthroughProcessor>();
        pluginLoaded.store(true);
        return true;
    }

    if (path == "__crash__") {
        plugin = std::make_unique<CrashingProcessor>();
        pluginLoaded.store(true);
        return true;
    }

    if (path == "__blocksize__") {
        plugin = std::make_unique<BlockSizeProbeProcessor>();
        pluginLoaded.store(true);
        return true;
    }

    juce::String error;

    for (auto* fmt : formatManager.getFormats()) {
        if (!fmt->fileMightContainThisPluginType(path))
            continue;

        juce::OwnedArray<juce::PluginDescription> types;
        fmt->findAllTypesForFile(types, path);

        for (auto* desc : types) {
            plugin = formatManager.createPluginInstance(*desc, 44100.0, 512, error);
            if (plugin) {
                pluginLoaded.store(true);
                return true;
            }
        }
    }

    return false;
}

// ---------------------------------------------------------------------------
// Editor window management — all called on the GUI (main) thread.
// ---------------------------------------------------------------------------
void PluginHost::openEditorOnGUIThread()
{
    std::lock_guard<std::mutex> lock(editorMutex);

    if (editorWindow != nullptr || plugin == nullptr)
        return;

    auto* ed = plugin->createEditor();
    if (ed == nullptr)
        return;

    editorWindow = std::make_unique<EditorWindow>(*this, ed);
}

void PluginHost::closeEditorOnGUIThread()
{
    destroyEditorWindow();
}

void PluginHost::destroyEditorWindow()
{
    std::lock_guard<std::mutex> lock(editorMutex);
    editorWindow.reset();
}

// Called by EditorWindow::closeButtonPressed() on the GUI thread.
void PluginHost::onEditorWindowClosed(bool wasParentInitiated)
{
    editorVisible.store(false);

    {
        std::lock_guard<std::mutex> lock(editorMutex);
        editorWindow.reset();
    }

    if (!wasParentInitiated) {
        // User clicked the close button — notify the parent process
        proxy::ProxyResponse r{};
        r.type = proxy::MessageType::EDITOR_CLOSED;
        r.result = 1;
        pipe.sendResp(r);
    }
}
