#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "RoutingManager.h"
#include "TransportManager.h"
#include "../model/ProjectModel.h"
#include <atomic>
#include <memory>
#include <functional>
#include <thread>

namespace HDAW {

class ExportManager
{
public:
    ExportManager();
    ~ExportManager();

    enum Format { WAV, AIFF, FLAC };

    bool startExport(const juce::ValueTree& projectTree, juce::AudioFormatManager& formatManager,
                     PluginManager* pluginManager, const juce::File& outputPath,
                     double sampleRate, double startTime, double duration,
                     Format format = WAV, int bitDepth = 24);

    void cancel();
    bool isExporting() const { return active.load(); }

    std::function<void(float)> onProgress;
    std::function<void(bool success, const juce::String& message)> onComplete;

    static double calculateProjectDuration(ProjectModel& model);

private:
    void renderThreadFunc(juce::ValueTree projectTree, juce::AudioFormatManager* formatManager,
                          PluginManager* pluginManager, juce::File outputPath,
                          double sampleRate, double startTime, double duration,
                          Format format, int bitDepth);

    std::atomic<bool> active{ false };
    std::atomic<bool> cancelFlag{ false };
    std::thread renderThread;
};

} // namespace HDAW
