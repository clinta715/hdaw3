#pragma once
#include "proxy/ProxyCommon.h"
#include "proxy/ProxyPipe.h"
#include "proxy/ProxySharedMemory.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cstring>
#include <string>
#include <atomic>
#include <memory>
#include <thread>
#include <mutex>
#include <deque>
#include <vector>
#include <functional>

// Child-side AudioPlayHead fed from the parent's transport snapshot over the
// shared-memory header (see ShmHeader's transport* fields). The parent packs
// on ITS audio thread and release-stores a bumped transportRevision; the
// child audioLoop acquire-loads the revision and copies the plain fields
// into this class before plugin->processBlock. After that copy only the
// child audio thread touches the members (processBlock runs on the same
// thread), so plain fields are correct — no atomics needed child-side.
// Until the first snapshot, getPosition() returns a stopped-transport
// default (isPlaying=false) instead of null, so clock-reliant plugins see a
// stopped transport rather than none.
class ChildPlayHead : public juce::AudioPlayHead
{
public:
    void setSampleRate(double sr) { sampleRate = sr; }

    void snapshotFrom(const proxy::ShmHeader* hdr)
    {
        float tempo = 0.0f;
        double sec = 0.0, ppqPos = 0.0;
        std::memcpy(&tempo, &hdr->transportTempoBits, sizeof(tempo));
        std::memcpy(&sec, &hdr->transportSecondsBits, sizeof(sec));
        std::memcpy(&ppqPos, &hdr->transportPpqBits, sizeof(ppqPos));
        playing = hdr->transportPlaying != 0;
        bpm = tempo;
        seconds = sec;
        ppq = ppqPos;
    }

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setTimeSignature(juce::AudioPlayHead::TimeSignature { 4, 4 });
        info.setIsPlaying(playing);
        info.setTimeInSeconds(seconds);
        info.setTimeInSamples(static_cast<int64_t>(seconds * sampleRate));
        info.setBpm(bpm);
        info.setPpqPosition(ppq);
        return info;
    }

private:
    double sampleRate = 44100.0;
    bool playing = false;
    double bpm = 120.0;
    double seconds = 0.0;
    double ppq = 0.0;
};

class PluginHost {
public:
    PluginHost(uint32_t slotId, const std::string& pipeName,
               const std::string& shmName, const std::string& pluginPath);
    ~PluginHost();

    int run();

private:
    void controlLoop();
    void audioLoop();
    bool loadPlugin();
    bool loadPluginByPath(const juce::String& path);

    // Runs fn on the child's message thread and waits (bounded). CLAP
    // lifecycle calls (activate/prepareToPlay/set/getState) must run on the
    // thread the host reports as main; the control loop is a pipe thread, and
    // thread-checking plugins (Odin2) std::terminate otherwise. Returns false
    // on timeout.
    bool runLifecycleOnMessageThread(const std::function<void()>& fn, int timeoutMs);

    // Correlation-id echo: stamps `requestId` into `resp` and sends it. EVERY
    // child response — single replies AND every multi-chunk continuation —
    // goes through here, so the echo cannot be forgotten at a site. The parent
    // matches replies by this id (ProxyCommon.h). `requestId` is the request
    // message's id, or proxy::kUnsolicitedRequestId for a requestless
    // notification (EDITOR_CLOSED / the READY handshake).
    // Note: taken by const ref and copied internally — MSVC rejects passing an
    // alignas(256) type by value (C2719).
    bool sendResponse(const proxy::ProxyResponse& resp, uint32_t requestId);

    void openEditorOnGUIThread();
    void closeEditorOnGUIThread();
    void destroyEditorWindow();
    void onEditorWindowClosed(bool wasParentInitiated);

    uint32_t slotId;
    std::string pipeName, shmName, pluginPath;

    proxy::PipeClient pipe;
    proxy::ShmRegion shm;

    std::atomic<bool> running{true};
    std::atomic<bool> pluginLoaded{false};
    std::atomic<bool> editorVisible{false};
    // Set when a control-thread plugin call (prepareToPlay / setState /
    // getState) threw a C++ exception. The audio thread then outputs silence
    // instead of calling processBlock, so a throwing plugin cannot abort the
    // child (std::terminate) — it degrades to a failed-plugin state.
    std::atomic<bool> pluginFailed{false};

    double preparedSampleRate = 44100.0;
    int preparedBlockSize = 512;
    int preparedNumChannels = 2;

    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::AudioPluginFormatManager formatManager;

    // SET_STATE chunk accumulation (controlLoop thread only)
    uint32_t pendingStateTotal = 0;
    std::vector<uint8_t> pendingState;

    // Parent->child plugin-state ring (shm). Applied from the audio loop when a
    // complete record is available and no OS warmup is running (the warmup's
    // processBlocks run on the control thread, so applying state concurrently
    // would race the plugin).
    std::vector<uint8_t> pendingStateFromRing;
    std::atomic<bool> warmupActive{false};
    // Expected wall duration (ms) of the in-flight Virus warmup. The watchdog
    // suppresses its dump while warmupActive is set and instead uses
    // warmupExpectedMs + 1000 as the real-hang threshold: the intentional
    // warmup blocking the audio-loop flag is expected, while a genuine hang
    // during/after it is still captured. Written (release) BEFORE warmupActive
    // is raised and never zeroed, so the pair is race-free under acquire loads.
    std::atomic<int> warmupExpectedMs{0};
    void applyPendingRingState();

    // Last state successfully handed to the plugin. Re-applied after
    // prepareToPlay: plugins (e.g. Serum 2) accept and cache state that
    // arrives before audio setup but never decode it into their DSP.
    std::vector<uint8_t> lastSetState;

    // Editor window (owned, lives on GUI thread)
    class EditorWindow;
    std::unique_ptr<EditorWindow> editorWindow;
    bool parentInitiatedClose = false;
    std::mutex editorMutex;

    // Pipe thread -> GUI thread message queue
    struct GUIMessage {
        enum Type { OpenEditor, CloseEditor } type;
    };
    std::mutex guiMutex;
    std::deque<GUIMessage> guiQueue;
    bool guiQueueReady = false;

    // Threads
    std::thread controlThread;
    std::thread audioThread;
    std::thread messagePumpThread;
    std::thread watchdogThread;

    // Watchdog: set by audio thread before processBlock, cleared after.
    // If it stays set for >1s the watchdog writes a minidump — EXCEPT during
    // the intentional Virus warmup, where the threshold is
    // warmupExpectedMs + 1s (see warmupExpectedMs above).
    std::atomic<bool> processBlockActive{false};
    std::atomic<bool> dumpWritten{false};

    // Child-side AudioProcessorListener registered on the hosted plugin;
    // forwards parameter changes to the parent via the paramNotify shm ring.
    class ParamForwarder;
    std::unique_ptr<ParamForwarder> paramForwarder;

    // Transport playhead forwarded from the parent. setPlayHead happens on
    // the control thread in loadPlugin (before audioLoop starts); afterwards
    // only the audio thread reads/writes these.
    ChildPlayHead childPlayHead;
    uint32_t lastTransportRevision = 0;
};
