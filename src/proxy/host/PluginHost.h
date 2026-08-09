#pragma once
#include "proxy/ProxyCommon.h"
#include "proxy/ProxyPipe.h"
#include "proxy/ProxySharedMemory.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <string>
#include <atomic>
#include <memory>
#include <thread>
#include <mutex>
#include <deque>
#include <vector>

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

    double preparedSampleRate = 44100.0;
    int preparedBlockSize = 512;
    int preparedNumChannels = 2;

    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::AudioPluginFormatManager formatManager;

    // SET_STATE chunk accumulation (controlLoop thread only)
    uint32_t pendingStateTotal = 0;
    std::vector<uint8_t> pendingState;

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
    // If set for >5s, the watchdog writes a minidump.
    std::atomic<bool> processBlockActive{false};
    std::atomic<bool> dumpWritten{false};

    // Child-side AudioProcessorListener registered on the hosted plugin;
    // forwards parameter changes to the parent via the paramNotify shm ring.
    class ParamForwarder;
    std::unique_ptr<ParamForwarder> paramForwarder;
};
