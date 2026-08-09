#include "PluginHost.h"
#include "engine/CLAPPluginFormat.h"
#include "common/DebugLog.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include <cstdlib>
#include <cstring>
#if JUCE_WINDOWS
#include <windows.h>
#include <stdexcept>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

namespace {

// SysEx drops are rare; log the first and every 256th so a stuck lane
// can't spam the log from the audio thread.
void logSysexDrop(const char* reason) {
    static std::atomic<uint32_t> count{0};
    if ((count.fetch_add(1, std::memory_order_relaxed) & 0xFFu) == 0)
        HDAW_LOG("plugin_host", reason);
}

#if JUCE_WINDOWS
// SEH-to-C++ exception translator for plugin processBlock crashes.
// Must NOT return — throws a C++ exception instead.
void __cdecl sehProcessBlockCrashTranslator(unsigned int, struct _EXCEPTION_POINTERS*)
{
    throw std::runtime_error("Plugin crashed during processBlock");
}

// Raw SEH wrapper around plugin->processBlock(). This catches ALL structured
// exceptions (access violations, stack overflows, divide-by-zero, etc.) that
// the _set_se_translator approach might miss when the crash originates in a
// plugin DLL's own exception handling or a background thread.
// MUST be a separate function — MSVC forbids __try/__except in functions
// that have C++ objects with destructors.
static LONG WINAPI processBlockSehFilter(EXCEPTION_POINTERS* ep)
{
    HDAW_LOG("plugin_host", "SEH exception in processBlock code=0x"
        + juce::String::toHexString(ep->ExceptionRecord->ExceptionCode));
    return EXCEPTION_EXECUTE_HANDLER;
}

// Write a minidump of the current process. Used by the watchdog to capture
// the state of a hanging processBlock.
static void writeMinidump(const char* reason, EXCEPTION_POINTERS* eps = nullptr)
{
    wchar_t dumpPath[MAX_PATH]{};
    GetTempPathW(MAX_PATH, dumpPath);
    wcscat_s(dumpPath, L"hdaw_plugin_host_");
    wchar_t reasonW[64]{};
    MultiByteToWideChar(CP_UTF8, 0, reason, -1, reasonW, 64);
    wcscat_s(dumpPath, reasonW);
    wcscat_s(dumpPath, L".dmp");

    HANDLE hFile = CreateFileW(dumpPath, GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;

    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId = GetCurrentThreadId();
    mei.ClientPointers = FALSE;
    mei.ExceptionPointers = eps;
    if (eps == nullptr)
        mei.ThreadId = 0;

    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                      hFile, MiniDumpWithFullMemory, &mei, nullptr, nullptr);
    CloseHandle(hFile);

    HDAW_LOG("plugin_host", juce::String("Wrote minidump: ") + reason
        + " path=" + juce::String(juce::CharPointer_UTF16(dumpPath)));
}
#endif

// Internal test processors derive from AudioPluginInstance; JUCE 8 makes
// AudioProcessor::addParameter private in that subclass, requiring parameters
// to go through the HostedAudioProcessorParameter route. This thin wrapper
// exposes a float parameter the same way AudioParameterFloat does.
class HostedFloatParameter : public juce::HostedAudioProcessorParameter
{
public:
    HostedFloatParameter(const juce::ParameterID& id, const juce::String& nm,
                         juce::NormalisableRange<float> r, float def)
        : range(std::move(r)), value(def), defaultValue(def), nameStr(nm), paramId(id)
    {
        // Convert the raw ID to a plain string for getParameterID.
    }

    juce::String getParameterID() const override { return paramId.getParamID(); }
    float getValue() const override { return range.convertTo0to1(value); }
    void setValue(float nv) override { value = range.convertFrom0to1(nv); }
    float getDefaultValue() const override { return range.convertTo0to1(defaultValue); }
    juce::String getName(int maxLen) const override
    {
        return maxLen > 0 && nameStr.length() > maxLen ? nameStr.substring(0, maxLen) : nameStr;
    }
    juce::String getLabel() const override { return {}; }
    int getNumSteps() const override { return 101; }
    bool isAutomatable() const override { return true; }
    juce::String getText(float v, int) const override { return juce::String(range.convertFrom0to1(v), 3); }
    float getValueForText(const juce::String&) const override { return 0.f; }

    float get() const noexcept { return value; }

private:
    juce::NormalisableRange<float> range;
    float value;
    float defaultValue;
    juce::String nameStr;
    juce::ParameterID paramId;
};

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

class StateEchoProcessor : public juce::AudioPluginInstance
{
public:
    StateEchoProcessor()
        : AudioPluginInstance(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
        addHostedParameter(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ "echo_a", 1 }, "Echo A",
            juce::NormalisableRange<float>(0.f, 1.f), 0.25f));
        addHostedParameter(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ "echo_b", 1 }, "Echo B",
            juce::NormalisableRange<float>(0.f, 1.f), 0.5f));
        addHostedParameter(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ "echo_c", 1 }, "Echo C",
            juce::NormalisableRange<float>(0.f, 1.f), 0.75f));
    }

    const juce::String getName() const override { return "StateEcho"; }
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
    int getNumPrograms() override { return 2; }
    int getCurrentProgram() override { return currentProgram_; }
    void setCurrentProgram(int i) override { currentProgram_ = i; }
    const juce::String getProgramName(int i) override {
        if (i == 0) return "Init";
        if (i == 1) return "Test Preset";
        return {};
    }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& dest) override
    {
        dest.setSize(state.getSize(), false);
        if (state.getSize() > 0)
            std::memcpy(dest.getData(), state.getData(), state.getSize());
    }
    void setStateInformation(const void* data, int sizeInBytes) override
    {
        if (data == nullptr || sizeInBytes <= 0) {
            state.setSize(0, false);
            return;
        }
        state.setSize(static_cast<size_t>(sizeInBytes), false);
        std::memcpy(state.getData(), data, static_cast<size_t>(sizeInBytes));
    }
    void fillInPluginDescription(juce::PluginDescription& d) const override
    {
        d.name = "StateEcho";
        d.pluginFormatName = "Internal";
        d.fileOrIdentifier = "__stateecho__";
    }

private:
    juce::MemoryBlock state;
    int currentProgram_ = 0;
};

class MidiEchoProcessor : public juce::AudioPluginInstance
{
public:
    MidiEchoProcessor()
        : AudioPluginInstance(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

    const juce::String getName() const override { return "MidiEcho"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override
    {
        (void)buffer;
        juce::MidiBuffer out;
        for (const auto metadata : midiMessages)
            out.addEvent(metadata.getMessage(), metadata.samplePosition);
        midiMessages.swapWith(out);
    }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
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
        d.name = "MidiEcho";
        d.pluginFormatName = "Internal";
        d.fileOrIdentifier = "__midiecho__";
    }
};

// Diagnostic processor for the transport-playhead handoff test: echoes the
// playhead snapshot it received through the audio output as a raw bit-packed
// payload (channel 0). The parent-side test decodes it and asserts the
// ChildPlayHead fields match what it packed. Zero payload = stopped-default
// playhead (no transport forwarded yet).
class TransportProbeProcessor : public juce::AudioPluginInstance
{
public:
    TransportProbeProcessor()
        : AudioPluginInstance(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

    struct ProbePayload {
        uint32_t isPlaying;
        float bpm;
        double seconds;
        double ppq;
    };

    const juce::String getName() const override { return "TransportProbe"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        buffer.clear();
        ProbePayload p{};
        if (auto* ph = getPlayHead())
        {
            if (auto pos = ph->getPosition())
            {
                p.isPlaying = pos->getIsPlaying() ? 1u : 0u;
                p.bpm = pos->getBpm().orFallback(120.0f);
                p.seconds = pos->getTimeInSeconds().orFallback(0.0);
                p.ppq = pos->getPpqPosition().orFallback(0.0);
            }
        }
        const size_t bytes = sizeof(ProbePayload);
        static_assert(bytes <= 64, "payload must fit well inside a block");
        if (buffer.getNumSamples() >= static_cast<int>(bytes / sizeof(float)))
            std::memcpy(buffer.getWritePointer(0), &p, bytes);
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
        d.name = "TransportProbe";
        d.pluginFormatName = "Internal";
        d.fileOrIdentifier = "__transportprobe__";
    }
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// ParamForwarder — child-side AudioProcessorListener that pushes hosted-plugin
// parameter changes onto the paramNotify shm ring so the parent can observe
// plugin editor knob moves. Lock-free: a failed reservation (slot busy) drops
// the event rather than blocking. The single notify ring slot is reserved
// optimistically; contention is rare (one producer across the hosted plugin).
// This listener is registered AFTER loadPlugin(); callbacks arrive on the
// child's audio/GUI threads and must stay allocation- and lock-free.
// ---------------------------------------------------------------------------
class PluginHost::ParamForwarder : public juce::AudioProcessorListener
{
public:
    explicit ParamForwarder(PluginHost& h) : host(h) {}

    void audioProcessorParameterChanged(juce::AudioProcessor*, int paramIndex, float newValue) override
    {
        push(paramIndex, newValue);
    }

    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}

private:
    // Lock-free single-producer push. JUCE delivers parameter change callbacks
    // serialised per parameter on the plugin's own thread, so this listener is
    // the sole writer of paramNotifyWritePos in the child process.
    void push(int paramIndex, float newValue) noexcept
    {
        auto* hdr = host.shm.getHeader();
        if (!hdr) return;
        auto* ring = host.shm.getParamNotifyRing();
        if (!ring) return;
        uint32_t w = hdr->paramNotifyWritePos.load(std::memory_order_relaxed);
        uint32_t r = hdr->paramNotifyReadPos.load(std::memory_order_acquire);
        if (w - r >= proxy::PARAM_RING_SIZE) return;
        uint64_t packed = (uint64_t(static_cast<uint32_t>(paramIndex)) << 32)
                          | uint64_t(*reinterpret_cast<const uint32_t*>(&newValue));
        ring[w & (proxy::PARAM_RING_SIZE - 1)].store(packed, std::memory_order_relaxed);
        hdr->paramNotifyWritePos.store(w + 1, std::memory_order_release);
    }

    PluginHost& host;
};

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
    if (messagePumpThread.joinable()) messagePumpThread.join();

    destroyEditorWindow();
    if (plugin && paramForwarder)
        plugin->removeListener(paramForwarder.get());
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

#if JUCE_WINDOWS
    // Diagnostic knob: HDAW_NO_CHILD_SEH=1 disables the crash-swallowing
    // guards so real faults terminate the child (WER LocalDumps) instead of
    // continuing.
    static const bool noChildSeh =
        juce::SystemStats::getEnvironmentVariable("HDAW_NO_CHILD_SEH", "") == "1";
    if (noChildSeh)
    {
        SetUnhandledExceptionFilter(nullptr);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    }
    else
    {
    // Install a process-wide unhandled exception filter so crashes on ANY
    // thread (including CLAP plugin background threads) don't kill the child
    // process. The SEH translator (sehProcessBlockCrashTranslator) only
    // protects the audio thread; this catches everything else.
    SetUnhandledExceptionFilter([](_EXCEPTION_POINTERS* ep) -> LONG {
        auto* er = ep->ExceptionRecord;
        auto* ctx = ep->ContextRecord;
        juce::String where = "?";
        HMODULE mod = nullptr;
        if (er->ExceptionAddress != nullptr)
        {
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCWSTR>(er->ExceptionAddress), &mod);
            wchar_t modPath[MAX_PATH]{};
            if (mod != nullptr && GetModuleFileNameW(mod, modPath, MAX_PATH) > 0)
                where = juce::String(juce::CharPointer_UTF16(modPath));
        }
        juce::String bytes;
        if (er->ExceptionAddress != nullptr)
            for (int bi = 0; bi < 12; ++bi)
                bytes += juce::String::toHexString(*reinterpret_cast<const uint8_t*>(
                    static_cast<const uint8_t*>(er->ExceptionAddress) + bi)) + " ";
        HDAW_LOG("plugin_host", "Unhandled exception, continuing (code=0x"
            + juce::String::toHexString(er->ExceptionCode)
            + " rip=" + juce::String::toHexString((juce::int64)ctx->Rip)
            + " rcx=" + juce::String::toHexString((juce::int64)ctx->Rcx)
            + " rdx=" + juce::String::toHexString((juce::int64)ctx->Rdx)
            + " r8=" + juce::String::toHexString((juce::int64)ctx->R8)
            + " r9=" + juce::String::toHexString((juce::int64)ctx->R9)
            + " r15=" + juce::String::toHexString((juce::int64)ctx->R15)
            + " mod=" + where
            + " bytes=" + bytes + ")");
        // Skip the faulting instruction to avoid infinite loop.
        // Move IP forward by instruction size (approximate — good enough
        // for crash recovery). The thread state is corrupted but the
        // audio thread continues producing silence.
        ep->ContextRecord->Rip += 1;
        return EXCEPTION_CONTINUE_EXECUTION;
    });

    // Suppress CRT abort dialog — if a C++ exception from a crashing
    // thread reaches std::terminate, just exit silently.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    }
#endif

    if (!pipe.connect()) return 1;
    if (!shm.open(shmName)) return 1;

    proxy::ProxyResponse readyResp{};
    readyResp.type = proxy::MessageType::READY;
    readyResp.result = 1;
    if (!pipe.sendResp(readyResp)) return 1;

    if (!loadPlugin()) return 1;

    controlThread = std::thread(&PluginHost::controlLoop, this);

    audioThread = std::thread(&PluginHost::audioLoop, this);

    // Diagnostic knob: HDAW_NO_CHILD_WORKERS=1 disables the message-pump and
    // watchdog threads so the audio thread (and JUCE construction) are the
    // only activity in the child process.
    static const bool noChildWorkers =
        juce::SystemStats::getEnvironmentVariable("HDAW_NO_CHILD_WORKERS", "") == "1";

    // Dedicated message-pump thread: continuously dispatches JUCE messages
    // so CLAP on_main_thread callbacks fire even when the audio thread is
    // blocked inside plugin->processBlock(). Without this, the main thread's
    // runDispatchLoopUntil(-1) may not dispatch AsyncUpdate fast enough
    // because the audio thread consumes all CPU in the export context.
    if (!noChildWorkers)
        messagePumpThread = std::thread([this]() {
        while (running.load()) {
            juce::MessageManager::getInstance()->runDispatchLoopUntil(0);
            Sleep(1);
        }
    });

    // Watchdog thread: monitors the audio thread for hanging processBlock.
    // If processBlockActive stays true for >1 second, writes a minidump
    // to %TEMP%\hdaw_plugin_host_hang.dmp for offline analysis.
    if (!noChildWorkers)
        watchdogThread = std::thread([this]() {
        int hangMs = 0;
        while (running.load()) {
            Sleep(250);
            if (processBlockActive.load(std::memory_order_acquire) && !dumpWritten.load()) {
                hangMs += 250;
                if (hangMs >= 1000) {
                    dumpWritten.store(true);
                    writeMinidump("processBlock hung for 1s");
                }
            } else {
                hangMs = 0;
            }
        }
    });

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

        // Chunked SET_STATE protocol: any message that is not a continuation
        // chunk aborts an in-flight accumulation.
        if (pendingStateTotal > pendingState.size()
            && msg.type != proxy::MessageType::STATE_CHUNK) {
            pendingStateTotal = 0;
            pendingState.clear();
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
                // dataSize carries the TOTAL state size; data holds the first
                // chunk and the remainder arrives as STATE_CHUNK messages. The
                // single SET_STATE response is sent only once fully accumulated.
                const uint32_t total = msg.dataSize;
                if (total <= sizeof(msg.data)) {
                    if (plugin && total > 0) {
                        plugin->setStateInformation(msg.data, static_cast<int>(total));
                    }
                    proxy::ProxyResponse resp{};
                    resp.type = proxy::MessageType::SET_STATE;
                    resp.result = 1;
                    pipe.sendResp(resp);
                } else {
                    pendingStateTotal = total;
                    pendingState.assign(msg.data, msg.data + sizeof(msg.data));
                }
                break;
            }

            case proxy::MessageType::STATE_CHUNK: {
                if (pendingStateTotal == 0)
                    break;
                const uint32_t remaining =
                    pendingStateTotal - static_cast<uint32_t>(pendingState.size());
                const uint32_t n = std::min<uint32_t>(
                    msg.dataSize,
                    std::min<uint32_t>(remaining,
                                       static_cast<uint32_t>(sizeof(msg.data))));
                pendingState.insert(pendingState.end(), msg.data, msg.data + n);
                if (pendingState.size() == pendingStateTotal) {
                    uint32_t result = 0;
                    if (plugin) {
                        plugin->setStateInformation(pendingState.data(),
                                                    static_cast<int>(pendingState.size()));
                        result = 1;
                    }
                    pendingStateTotal = 0;
                    pendingState.clear();
                    proxy::ProxyResponse resp{};
                    resp.type = proxy::MessageType::SET_STATE;
                    resp.result = result;
                    pipe.sendResp(resp);
                }
                break;
            }

            case proxy::MessageType::GET_STATE: {
                proxy::ProxyResponse resp{};
                resp.type = proxy::MessageType::GET_STATE_RESULT;
                if (plugin) {
                    juce::MemoryBlock block;
                    plugin->getStateInformation(block);
                    const size_t total = block.getSize();
                    resp.result = 1;
                    resp.dataSize = static_cast<uint32_t>(total);
                    const size_t first = std::min(total, sizeof(resp.data));
                    if (first > 0)
                        std::memcpy(resp.data, block.getData(), first);
                    pipe.sendResp(resp);
                    size_t offset = first;
                    while (offset < total) {
                        proxy::ProxyResponse chunk{};
                        chunk.type = proxy::MessageType::STATE_CHUNK;
                        chunk.dataSize = static_cast<uint32_t>(
                            std::min(total - offset, sizeof(chunk.data)));
                        std::memcpy(chunk.data,
                                    static_cast<const uint8_t*>(block.getData()) + offset,
                                    chunk.dataSize);
                        pipe.sendResp(chunk);
                        offset += chunk.dataSize;
                    }
                } else {
                    resp.result = 0;
                    pipe.sendResp(resp);
                }
                break;
            }

            case proxy::MessageType::SET_PARAM: {
                proxy::ProxyResponse resp{};
                resp.type = proxy::MessageType::SET_PARAM;
                if (plugin && msg.dataSize >= sizeof(uint32_t) + sizeof(float)) {
                    uint32_t index = 0;
                    float value = 0.f;
                    std::memcpy(&index, msg.data, sizeof(uint32_t));
                    std::memcpy(&value, msg.data + sizeof(uint32_t), sizeof(float));
                    auto& params = plugin->getParameters();
                    if (index < static_cast<uint32_t>(params.size())) {
                        params[static_cast<int>(index)]->setValue(value);
                        resp.result = 1;
                    } else {
                        resp.result = 0;
                    }
                } else {
                    resp.result = 0;
                }
                pipe.sendResp(resp);
                break;
            }

            case proxy::MessageType::GET_PARAM: {
                proxy::ProxyResponse resp{};
                resp.type = proxy::MessageType::GET_PARAM_RESULT;
                if (plugin && msg.dataSize >= sizeof(uint32_t)) {
                    uint32_t index = 0;
                    std::memcpy(&index, msg.data, sizeof(uint32_t));
                    auto& params = plugin->getParameters();
                    if (index < static_cast<uint32_t>(params.size())) {
                        float value = params[static_cast<int>(index)]->getValue();
                        std::memcpy(resp.data, &value, sizeof(float));
                        resp.dataSize = sizeof(float);
                        resp.result = 1;
                    } else {
                        resp.result = 0;
                    }
                } else {
                    resp.result = 0;
                }
                pipe.sendResp(resp);
                break;
            }

            case proxy::MessageType::GET_PARAM_COUNT: {
                proxy::ProxyResponse resp{};
                resp.type = proxy::MessageType::GET_PARAM_COUNT_RESULT;
                if (plugin) {
                    uint32_t count = static_cast<uint32_t>(plugin->getParameters().size());
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
                if (plugin && msg.dataSize >= sizeof(uint32_t)) {
                    uint32_t index = 0;
                    std::memcpy(&index, msg.data, sizeof(uint32_t));
                    auto& params = plugin->getParameters();
                    if (index < static_cast<uint32_t>(params.size())) {
                        auto* p = params[static_cast<int>(index)];
                        float defaultValue = p->getDefaultValue();
                        uint8_t automatable = p->isAutomatable() ? 1u : 0u;
                        juce::String nameStr = p->getName(512);
                        auto nameUtf8 = nameStr.toRawUTF8();
                        uint32_t nameLen = static_cast<uint32_t>(std::strlen(nameUtf8));
                        // Layout: float defaultValue; uint8 automatable;
                        //          uint32 nameLen; char name[nameLen]
                        uint32_t headerBytes = sizeof(float) + sizeof(uint8_t) + sizeof(uint32_t);
                        uint32_t total = headerBytes + nameLen;
                        resp.dataSize = total;
                        resp.result = 1;
                        uint32_t offset = 0;
                        std::memcpy(resp.data + offset, &defaultValue, sizeof(float));
                        offset += sizeof(float);
                        std::memcpy(resp.data + offset, &automatable, sizeof(uint8_t));
                        offset += sizeof(uint8_t);
                        std::memcpy(resp.data + offset, &nameLen, sizeof(uint32_t));
                        offset += sizeof(uint32_t);
                        uint32_t room = static_cast<uint32_t>(sizeof(resp.data)) - offset;
                        uint32_t firstChunk = std::min(nameLen, room);
                        std::memcpy(resp.data + offset, nameUtf8, firstChunk);
                        offset += firstChunk;
                        pipe.sendResp(resp);
                        // Send any remaining name bytes as STATE_CHUNK responses.
                        if (firstChunk < nameLen) {
                            uint32_t pos = firstChunk;
                            uint32_t rem = nameLen - firstChunk;
                            while (rem > 0) {
                                proxy::ProxyResponse chunk{};
                                chunk.type = proxy::MessageType::STATE_CHUNK;
                                uint32_t take = std::min(rem, static_cast<uint32_t>(sizeof(chunk.data)));
                                std::memcpy(chunk.data, nameUtf8 + pos, take);
                                chunk.dataSize = take;
                                pipe.sendResp(chunk);
                                pos += take;
                                rem -= take;
                            }
                        }
                        break;
                    }
                }
                resp.result = 0;
                pipe.sendResp(resp);
                break;
            }

            case proxy::MessageType::GET_PROGRAM_COUNT: {
                proxy::ProxyResponse resp{};
                resp.type = proxy::MessageType::GET_PROGRAM_COUNT_RESULT;
                if (plugin) {
                    uint32_t count = static_cast<uint32_t>(plugin->getNumPrograms());
                    std::memcpy(resp.data, &count, sizeof(count));
                    resp.dataSize = sizeof(count);
                    resp.result = 1;
                } else {
                    resp.result = 0;
                }
                pipe.sendResp(resp);
                break;
            }

            case proxy::MessageType::GET_PROGRAM_NAME: {
                proxy::ProxyResponse resp{};
                resp.type = proxy::MessageType::GET_PROGRAM_NAME_RESULT;
                if (plugin && msg.dataSize >= sizeof(uint32_t)) {
                    uint32_t index = 0;
                    std::memcpy(&index, msg.data, sizeof(uint32_t));
                    juce::String nameStr = plugin->getProgramName(static_cast<int>(index));
                    auto utf8 = nameStr.toRawUTF8();
                    uint32_t len = static_cast<uint32_t>(std::strlen(utf8));
                    resp.dataSize = len;
                    resp.result = 1;
                    uint32_t first = std::min(len, static_cast<uint32_t>(sizeof(resp.data)));
                    std::memcpy(resp.data, utf8, first);
                    pipe.sendResp(resp);
                    if (len > first) {
                        uint32_t offset = first;
                        while (offset < len) {
                            proxy::ProxyResponse chunk{};
                            chunk.type = proxy::MessageType::STATE_CHUNK;
                            uint32_t take = std::min(len - offset, static_cast<uint32_t>(sizeof(chunk.data)));
                            std::memcpy(chunk.data, utf8 + offset, take);
                            chunk.dataSize = take;
                            pipe.sendResp(chunk);
                            offset += take;
                        }
                    }
                    break;
                }
                resp.result = 0;
                pipe.sendResp(resp);
                break;
            }

            case proxy::MessageType::SET_PROGRAM: {
                proxy::ProxyResponse resp{};
                resp.type = proxy::MessageType::SET_PROGRAM_RESULT;
                if (plugin && msg.dataSize >= sizeof(uint32_t)) {
                    uint32_t index = 0;
                    std::memcpy(&index, msg.data, sizeof(uint32_t));
                    plugin->setCurrentProgram(static_cast<int>(index));
                    resp.result = 1;
                } else {
                    resp.result = 0;
                }
                pipe.sendResp(resp);
                break;
            }

            case proxy::MessageType::GET_CURRENT_PROGRAM: {
                proxy::ProxyResponse resp{};
                resp.type = proxy::MessageType::GET_CURRENT_PROGRAM_RESULT;
                if (plugin) {
                    uint32_t cur = static_cast<uint32_t>(plugin->getCurrentProgram());
                    std::memcpy(resp.data, &cur, sizeof(cur));
                    resp.dataSize = sizeof(cur);
                    resp.result = 1;
                } else {
                    resp.result = 0;
                }
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
        // Drain parent->child param set ring BEFORE block processing so staged
        // parameter values take effect for this block. Single reader (audio
        // thread); bounds-checked against PARAM_RING_SIZE and the live param
        // count.
        if (plugin) {
            auto* paramRing = shm.getParamSetRing();
            if (paramRing) {
                uint32_t pr = hdr->paramSetReadPos.load(std::memory_order_relaxed);
                uint32_t pw = hdr->paramSetWritePos.load(std::memory_order_acquire);
                auto& params = plugin->getParameters();
                int n = params.size();
                while (pr != pw) {
                    uint64_t packed = paramRing[pr & (proxy::PARAM_RING_SIZE - 1)]
                                          .load(std::memory_order_relaxed);
                    uint32_t idx = static_cast<uint32_t>(packed >> 32);
                    uint32_t bits = static_cast<uint32_t>(packed & 0xFFFFFFFFull);
                    float value;
                    std::memcpy(&value, &bits, sizeof(float));
                    if (idx < static_cast<uint32_t>(n))
                        params[static_cast<int>(idx)]->setValue(value);
                    ++pr;
                }
                hdr->paramSetReadPos.store(pr, std::memory_order_release);
            }
        }
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
            // TEMP ClapProbe: diagnostic only — remove after Phase 2
            // investigation (answers: does input audio flow reach the child?).
            {
                static std::atomic<int> openProbeCount{0};
                int opc = openProbeCount.fetch_add(1, std::memory_order_relaxed);
                if (opc < 5)
                {
                    HDAW_LOG("ClapProbe",
                        (juce::String("audioLoop gate OPEN w-r=")
                         + juce::String(static_cast<int>(w - r))
                         + " bs=" + juce::String(preparedBlockSize)
                         + " ch=" + juce::String(preparedNumChannels))
                            .toStdString());
                }
            }
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
                const uint8_t* sysexBuf = shm.getSysexInBuffer();
                for (uint32_t i = 0; i < toRead; ++i) {
                    const proxy::MidiEvent& evt = midiIn[(mr + i) & 0xFF];
                    if ((evt.flags & 0x80u) != 0) {
                        if (sysexBuf != nullptr && evt.sysexLen > 0
                            && evt.sysexLen <= proxy::SYSEX_BUFFER_SIZE) {
                            // Reconstructing the received SysEx allocates — the
                            // one accepted heap allocation on the audio path
                            // (rare event).
                            midiBuffer.addEvent(
                                juce::MidiMessage(sysexBuf, static_cast<int>(evt.sysexLen)),
                                static_cast<int>(evt.sampleOffset));
                        }
                        hdr->sysexInBusy.store(0, std::memory_order_release);
                    } else {
                        int n = static_cast<int>(evt.flags & 0x7Fu);
                        if (n < 1) n = 1;
                        if (n > 3) n = 3;
                        midiBuffer.addEvent(
                            juce::MidiMessage(evt.data, n),
                            static_cast<int>(evt.sampleOffset));
                    }
                }
                hdr->midiInReadPos.store(mr + toRead, std::memory_order_release);
            }

            // Pace the audio loop at approximately real-time speed so CLAP
            // plugins receive blocks at a rate similar to live playback.
            // During export the parent writes blocks at CPU speed; some CLAP
            // plugins (Vital, Dexed, JE8086) crash or hang when processBlock
            // is called faster than real-time because their internal timers
            // and on_main_thread callbacks assume real-time pacing.
            // Diagnostic knob: HDAW_NO_CHILD_PACING=1 disables the pacing
            // sleep.
            static thread_local const bool noChildPacing =
                juce::SystemStats::getEnvironmentVariable("HDAW_NO_CHILD_PACING", "") == "1";
            if (!noChildPacing)
            {
                static thread_local uint64_t lastPaceTimeNs = 0;
                if (lastPaceTimeNs == 0) {
                    lastPaceTimeNs = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()).count());
                } else {
                    double blockDurationNs = static_cast<double>(preparedBlockSize) / preparedSampleRate * 1e9;
                    uint64_t nowNs = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()).count());
                    uint64_t elapsedNs = nowNs - lastPaceTimeNs;
                    if (elapsedNs < static_cast<uint64_t>(blockDurationNs)) {
                        uint64_t sleepNs = static_cast<uint64_t>(blockDurationNs) - elapsedNs;
                        if (sleepNs > 1000000) // only sleep if > 1ms
                            Sleep(static_cast<DWORD>(sleepNs / 1000000));
                    }
                    lastPaceTimeNs = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()).count());
                }
            }

            // Transport snapshot: acquire-load the revision the parent
            // release-stores AFTER packing every field; on change, copy the
            // plain fields into ChildPlayHead (bit patterns are restored via
            // memcpy). An unchanged revision means "no new info" — keep the
            // previous snapshot (stopped-transport default until the first
            // one arrives). After this, only this audio thread touches the
            // ChildPlayHead members (plugin->processBlock runs here too).
            const uint32_t rev = hdr->transportRevision.load(std::memory_order_acquire);
            if (rev != lastTransportRevision)
            {
                childPlayHead.setSampleRate(preparedSampleRate);
                childPlayHead.snapshotFrom(hdr);
                lastTransportRevision = rev;
            }

            // SEH guard: catch crashes in the plugin's processBlock and
            // output silence instead of killing the child process.
            // Diagnostic knob: HDAW_NO_CHILD_SEH=1 disables this guard.
            static thread_local const bool noChildSeh =
                juce::SystemStats::getEnvironmentVariable("HDAW_NO_CHILD_SEH", "") == "1";
#if JUCE_WINDOWS
            if (noChildSeh)
            {
                plugin->processBlock(inputBuffer, midiBuffer);
            }
            else
            {
                static thread_local int crashCount = 0;
                static thread_local int blockNum = 0;
                auto oldTranslator = _set_se_translator(sehProcessBlockCrashTranslator);
                processBlockActive.store(true, std::memory_order_release);
                try
                {
                    plugin->processBlock(inputBuffer, midiBuffer);
                    crashCount = 0;
                }
                catch (const std::runtime_error&)
                {
                    ++crashCount;
                    if (crashCount < 5 || (crashCount % 25) == 0)
                        HDAW_LOG("SIL", "CRASH count=" + juce::String(crashCount)
                                  + " block=" + juce::String(blockNum));
                    inputBuffer.clear();
                }
                processBlockActive.store(false, std::memory_order_release);
                _set_se_translator(oldTranslator);
                ++blockNum;
            }
#else
            plugin->processBlock(inputBuffer, midiBuffer);
#endif

            hdr->audioFramesProduced.fetch_add(preparedBlockSize, std::memory_order_relaxed);
            hdr->audioBlocksProcessed.fetch_add(1, std::memory_order_relaxed);

            proxy::MidiEvent* midiOut = shm.getMidiOutRing();
            if (midiOut) {
                constexpr uint32_t midiCap = 256;
                uint32_t mw = hdr->midiOutWritePos.load(std::memory_order_relaxed);
                uint32_t mr = hdr->midiOutReadPos.load(std::memory_order_acquire);
                uint32_t space = midiCap - ((mw >= mr) ? (mw - mr) : 0);
                uint8_t* sysexBuf = shm.getSysexOutBuffer();
                uint32_t written = 0;
                for (const auto metadata : midiBuffer) {
                    if (written >= space) break;
                    const auto msg = metadata.getMessage();
                    proxy::MidiEvent& evt = midiOut[(mw + written) & 0xFF];

                    if (msg.isSysEx()) {
                        const size_t len = static_cast<size_t>(msg.getRawDataSize());
                        if (hdr->sysexOutBusy.load(std::memory_order_acquire) != 0) {
                            logSysexDrop("midiOut SysEx dropped: lane busy");
                            continue;
                        }
                        if (sysexBuf == nullptr || len > proxy::SYSEX_BUFFER_SIZE) {
                            logSysexDrop("midiOut SysEx dropped: exceeds 128KB buffer");
                            continue;
                        }
                        evt.sampleOffset = static_cast<uint32_t>(metadata.samplePosition);
                        std::memcpy(sysexBuf, msg.getRawData(), len);
                        evt.flags = 0x80;
                        evt.sysexLen = static_cast<uint32_t>(len);
                        // Becomes visible to the parent with the midiOutWritePos
                        // release store below.
                        hdr->sysexOutBusy.store(1, std::memory_order_release);
                        ++written;
                    } else {
                        evt.sampleOffset = static_cast<uint32_t>(metadata.samplePosition);
                        uint32_t n = static_cast<uint32_t>(msg.getRawDataSize());
                        if (n < 1) n = 1;
                        if (n > 3) n = 3;
                        const auto* bytes = msg.getRawData();
                        for (uint32_t i = 0; i < n; ++i)
                            evt.data[i] = bytes[i];
                        evt.flags = static_cast<uint8_t>(n);
                        ++written;
                    }
                }
                hdr->midiOutWritePos.store(mw + written, std::memory_order_release);
            }

            uint32_t ow = hdr->outputWritePos.load(std::memory_order_relaxed);
            for (int ch = 0; ch < preparedNumChannels; ++ch) {
                for (int s = 0; s < preparedBlockSize; ++s)
                    outRing[(ow + ch * preparedBlockSize + s) & (cap - 1)] = inputBuffer.getSample(ch, s);
            }
            hdr->outputWritePos.store(ow + static_cast<uint32_t>(preparedBlockSize * preparedNumChannels), std::memory_order_release);

            // Yield to give the main thread time to pump CLAP on_main_thread
            // callbacks. During export the audio loop runs at CPU speed and
            // the main thread (which dispatches request_callback via
            // AsyncUpdate → on_main_thread) can starve without this.
            Sleep(0);
        } else {
            // TEMP ClapProbe: diagnostic only — remove after Phase 2
            // investigation (answers: is the child starved of input frames?).
            static std::atomic<int> spinProbeCount{0};
            int spc = spinProbeCount.fetch_add(1, std::memory_order_relaxed);
            if (spc < 5 || (spc % 1024) == 0)
            {
                HDAW_LOG("ClapProbe",
                    (juce::String("audioLoop gate SPIN w-r=")
                     + juce::String(static_cast<int>(w - r))).toStdString());
            }
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
    bool loadedRealPlugin = loadPluginByPath(juce::String(pluginPath));
    // TEMP ClapProbe: diagnostic only — remove after Phase 2 investigation.
    HDAW_LOG("ClapProbe", (juce::String("loadPlugin final plugin=")
        + juce::String(loadedRealPlugin ? "yes" : "no")).toStdString());
    if (!loadedRealPlugin) {
        // Plugin failed to load (crashed during init, etc.).
        // Use a passthrough processor instead so the child stays alive
        // and the parent's proxy can still communicate. The child will
        // output silence (passthrough copies input to output, which is
        // zeroed by the parent).
        HDAW_LOG("plugin_host", "Plugin failed to load, using passthrough");
        plugin = std::make_unique<PassthroughProcessor>();
        pluginLoaded.store(true);
    }
    if (plugin) {
        paramForwarder = std::make_unique<ParamForwarder>(*this);
        plugin->addListener(paramForwarder.get());
        // Supply the transport playhead BEFORE the audio loop starts so
        // clock-reliant CLAP instruments (ShinRonin, Odin2, Gneiss, ...)
        // receive the parent's transport from their first processBlock.
        // Diagnostic knob: HDAW_NO_CHILD_PLAYHEAD=1 removes the playhead —
        // reproduces the pre-forward condition inside the child process.
        static const bool noChildPlayhead =
            juce::SystemStats::getEnvironmentVariable("HDAW_NO_CHILD_PLAYHEAD", "") == "1";
        if (!noChildPlayhead)
            plugin->setPlayHead(&childPlayHead);
    }
    return true;
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

    if (path == "__stateecho__") {
        plugin = std::make_unique<StateEchoProcessor>();
        pluginLoaded.store(true);
        return true;
    }

    if (path == "__midiecho__") {
        plugin = std::make_unique<MidiEchoProcessor>();
        pluginLoaded.store(true);
        return true;
    }

    if (path == "__transportprobe__") {
        plugin = std::make_unique<TransportProbeProcessor>();
        pluginLoaded.store(true);
        return true;
    }

    juce::String error;

    for (auto* fmt : formatManager.getFormats()) {
        bool mightContain = fmt->fileMightContainThisPluginType(path);
        juce::OwnedArray<juce::PluginDescription> types;
        int typeCount = 0;
        if (mightContain)
        {
            fmt->findAllTypesForFile(types, path);
            typeCount = types.size();
        }

        // TEMP ClapProbe: diagnostic only — remove after Phase 2
        // investigation (answers: what path+format reach the child?).
        {
            static std::atomic<int> lpProbeCount{0};
            int lpc = lpProbeCount.fetch_add(1, std::memory_order_relaxed);
            if (lpc < 4)
            {
                HDAW_LOG("ClapProbe",
                    (juce::String("loadPluginByPath path=") + path
                     + " format=" + fmt->getName()
                     + " mightContain=" + juce::String(mightContain ? 1 : 0)
                     + " types=" + juce::String(typeCount))
                        .toStdString());
            }
        }

        if (!mightContain)
            continue;

        for (auto* desc : types) {
            // SEH guard around plugin instantiation — CLAP plugins can
            // crash during init() on background threads.
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
