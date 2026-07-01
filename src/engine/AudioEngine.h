#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "MainAudioProcessor.h"
#include "SPSCBridge.h"
#include "TransportManager.h"
#include "ProjectPool.h"
#include "PluginManager.h"
#include "MidiInputManager.h"
#include "../model/ProjectModel.h"
#include <memory>

class AudioEngine : private juce::ValueTree::Listener
{
public:
    AudioEngine();
    ~AudioEngine();

    void initialize();
    void shutdown();

    MainAudioProcessor* getMainProcessor() const { return mainProcessor.get(); }
    ProjectModel& getProjectModel() { return projectModel; }
    SPSCBridge& getBridge() { return spscBridge; }
    HDAW::TransportManager& getTransportManager() { return transportManager; }
    HDAW::ProjectPool& getProjectPool() { return projectPool; }
    HDAW::PluginManager& getPluginManager() { return pluginManager; }
    HDAW::MidiInputManager& getMidiInputManager() { return midiInputManager; }

    // Facade methods
    int getTrackCount() const { return mainProcessor ? mainProcessor->getNumTracks() : 0; }
    float getTrackVolume(int trackIndex) const;
    void setTrackVolume(int trackIndex, float volume);
    float getTrackPan(int trackIndex) const;
    void setTrackPan(int trackIndex, float pan);
    bool isTrackMuted(int trackIndex) const;
    void setTrackMuted(int trackIndex, bool muted);
    bool isTrackArmed(int trackIndex) const;
    void setTrackArmed(int trackIndex, bool armed);
    juce::String getTrackName(int trackIndex) const;

private:
    // ValueTree::Listener overrides
    void valueTreePropertyChanged(juce::ValueTree& treeWhosePropertyHasChanged, const juce::Identifier& property) override;
    void valueTreeChildAdded(juce::ValueTree& parentTree, juce::ValueTree& childWhichHasBeenAdded) override;
    void valueTreeChildRemoved(juce::ValueTree& parentTree, juce::ValueTree& childWhichHasBeenRemoved, int indexFromWhichItWasRemoved) override;

    void rebuildTempoMap();

    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorPlayer processorPlayer;
    std::unique_ptr<MainAudioProcessor> mainProcessor;
    ProjectModel projectModel;
    SPSCBridge spscBridge;
    HDAW::TransportManager transportManager;
    HDAW::ProjectPool projectPool;
    HDAW::PluginManager pluginManager;
    HDAW::MidiInputManager midiInputManager;
};
