#pragma once
#include "../common/PluginParamService.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

class MainAudioProcessor;

class PluginParamServiceImpl : public PluginParamService {
public:
    // `ensureLive` settles the live routing projection on demand
    // (AudioEngine::ensureLiveRouting: drain + bounded full rebuild) when a
    // lookup hits a null track. Empty (default) = no-op, so bare
    // construction keeps its previous behavior.
    using EnsureLiveRoutingFn = std::function<bool(int)>;
    explicit PluginParamServiceImpl(MainAudioProcessor& proc,
                                    EnsureLiveRoutingFn ensureLive = {});
    ~PluginParamServiceImpl() override;

    std::vector<PluginParamSnapshot> getParams(int trackIndex, const std::string& pluginID) override;
    std::string getParamText(int trackIndex, const std::string& pluginID, int paramIndex, float normalizedValue) override;
    void setParam(int trackIndex, const std::string& pluginID, int paramIndex, float normalizedValue) override;

    void setParamChangeCallback(int trackIndex, const std::string& pluginID, ParamChangeCallback cb) override;

    int getProgramCount(int trackIndex, const std::string& pluginID) override;
    int getCurrentProgram(int trackIndex, const std::string& pluginID) override;
    std::string getProgramName(int trackIndex, const std::string& pluginID, int programIndex) override;
    void setCurrentProgram(int trackIndex, const std::string& pluginID, int programIndex) override;

private:
    juce::AudioPluginInstance* resolveInstance(int trackIndex, const std::string& pluginID) const;
    void clearCallback(int trackIndex, const std::string& pluginID);

    MainAudioProcessor& proc_;
    EnsureLiveRoutingFn ensureLive_;

    struct InternalListener : public juce::AudioProcessorListener {
        ParamChangeCallback onChanged;
        void audioProcessorParameterChanged(juce::AudioProcessor*, int idx, float v) override {
            if (onChanged) onChanged(idx, v);
        }
        void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) override {}
        void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int) override {}
        void audioProcessorChanged(juce::AudioProcessor*, const juce::AudioProcessorListener::ChangeDetails&) override {}
    };

    struct CallbackEntry {
        int trackIndex;
        std::string pluginID;
        std::unique_ptr<InternalListener> listener;
    };

    std::mutex cbMutex;
    std::vector<CallbackEntry> callbacks;
};
