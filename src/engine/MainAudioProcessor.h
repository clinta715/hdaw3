#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "SPSCBridge.h"
#include "Track.h"
#include "TransportManager.h"
#include "RoutingManager.h"
#include "MasterBusProcessor.h"
#include "PluginManager.h"
#include "AudioRecorder.h"
#include "ExportManager.h"
#include "Metronome.h"
#include "ClipSourceProcessor.h"
#include "../model/ProjectModel.h"
#include <memory>

class MainAudioProcessor : public juce::AudioProcessor
{
public:
    MainAudioProcessor();
    ~MainAudioProcessor() override;

    void setBridge(SPSCBridge* bridge) { spscBridge = bridge; }
    void setTransportManager(HDAW::TransportManager* tm);
    void setProjectModel(ProjectModel* model) { projectModel = model; }
    void setFormatManager(juce::AudioFormatManager& fm) { formatManager = &fm; }
    void setPluginManager(HDAW::PluginManager* pm) { pluginManager = pm; }
    void setStretchCache(HDAW::StretchCache* sc) { stretchCache = sc; }

    // Track Management (delegated to RoutingManager)
    HDAW::Track* getTrack(int index) const;
    int getNumTracks() const { return routingManager != nullptr ? routingManager->getNumTracks() : 0; }

    HDAW::LevelMeter& getMasterMeter();
    HDAW::RoutingManager* getRoutingManager() const { return routingManager.get(); }
    HDAW::Metronome& getMetronome() { return metronome; }
    void setCountInEnabled(bool enabled, int bars = 1) { countInEnabled = enabled; countInBars = bars; }
    void addExternalMidiMessage(const juce::MidiMessage& msg);
    void rebuildTrackFX(int trackIndex);
    void rebuildModulation(int trackIndex);
    void toggleFXEditor(int trackIndex, int slotIndex);
    void rebuildRoutingGraph();
    void rebuildAutomationCache(int trackIndex);
    void updateClipGainEnvelope(int clipId, const std::vector<HDAW::ClipSourceProcessor::GainPoint>& points);

    bool beginActualRecording();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    // Accept any layout where the main input/output buses have equal channel
    // counts (stereo↔stereo, mono↔mono, etc.). Without this override JUCE
    // may disable the buses during host layout negotiation, which starves the
    // AudioProcessorGraph's audioOutputNode of input channels and silently
    // drops all audio output (the master meter still moves because the master
    // bus processes its inputs, but no signal reaches the speaker buffer).
    bool isBusesLayoutSupported(const juce::AudioProcessor::BusesLayout& layouts) const override;

    // Recording
    bool startRecording();
    void stopRecording();
    bool isRecording() const;
    HDAW::AudioRecorder& getRecorder() { return *audioRecorder; }

    // Export
    HDAW::ExportManager& getExportManager() { return exportManager; }
    bool isExporting() const { return exportManager.isExporting(); }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    const juce::String getName() const override { return "HDAW Engine"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

private:
    SPSCBridge* spscBridge = nullptr;
    HDAW::TransportManager* transportManager = nullptr;
    ProjectModel* projectModel = nullptr;
    juce::AudioFormatManager* formatManager = nullptr;
    HDAW::PluginManager* pluginManager = nullptr;
    HDAW::StretchCache* stretchCache = nullptr;
    std::unique_ptr<HDAW::InternalPlayHead> internalPlayHead;

    juce::AudioProcessorGraph graph;
    std::unique_ptr<HDAW::RoutingManager> routingManager;
    std::unique_ptr<HDAW::AudioRecorder> audioRecorder;
    int64_t recordingStartSample = 0;
    int64_t pendingRecordStartSample = -1;
    std::atomic<bool> countInActive{ false };
    bool countInEnabled = false;
    int countInBars = 1;
    bool wasMetronomeOn = false;
    HDAW::Metronome metronome;
    HDAW::ExportManager exportManager;

    juce::CriticalSection midiLock;
    juce::MidiBuffer pendingMidi;

    juce::SpinLock graphLock;
    std::atomic<bool> graphRebuildPending{ false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainAudioProcessor)
};
