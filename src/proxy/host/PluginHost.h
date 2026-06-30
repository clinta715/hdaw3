#pragma once
#include "proxy/ProxyCommon.h"
#include "proxy/ProxyPipe.h"
#include "proxy/ProxySharedMemory.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <string>
#include <atomic>
#include <memory>
#include <thread>

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

    uint32_t slotId;
    std::string pipeName, shmName, pluginPath;

    proxy::PipeServer pipe;
    proxy::ShmRegion shm;

    std::atomic<bool> running{true};
    std::atomic<bool> pluginLoaded{false};
    std::atomic<bool> editorVisible{false};

    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::AudioPluginFormatManager formatManager;

    std::thread controlThread;
    std::thread audioThread;
};
