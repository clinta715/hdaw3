#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <clap/clap.h>
#include <clap/helpers/host.hh>
#include <clap/ext/timer-support.h>
#include <clap/ext/log.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/gui.h>
#include <clap/ext/latency.h>
#include "CLAPPluginFormat.h"
#include <atomic>
#include <vector>
#include <array>
#include <memory>

class CLAPPluginInstance;

// ── CLAPHost ────────────────────────────────────────────────────

class CLAPHost : public clap::helpers::Host<clap::helpers::MisbehaviourHandler::Terminate,
                                             clap::helpers::CheckingLevel::Maximal>,
                 private juce::AsyncUpdater
{
public:
    explicit CLAPHost(CLAPPluginInstance* instance);
    ~CLAPHost() override { cancelPendingUpdate(); }
    void setInstance(CLAPPluginInstance* inst) { instance = inst; }

    bool threadCheckIsMainThread() const noexcept override;
    bool threadCheckIsAudioThread() const noexcept override;

    void requestRestart() noexcept override;
    void requestProcess() noexcept override;
    void requestCallback() noexcept override;

    const void* getExtension(const char* id) const noexcept override;

    // Params
    bool implementsParams() const noexcept override { return true; }
    void paramsRescan(clap_param_rescan_flags flags) noexcept override;
    void paramsClear(clap_id paramId, clap_param_clear_flags flags) noexcept override;

    // State
    bool implementsState() const noexcept override { return true; }
    void stateMarkDirty() noexcept override;

    // Gui
    bool implementsGui() const noexcept override { return true; }
    bool guiRequestResize(uint32_t w, uint32_t h) noexcept override;
    void guiClosed(bool wasDestroyed) noexcept override;

    // Latency
    bool implementsLatency() const noexcept override { return true; }
    void latencyChanged() noexcept override;

    // Log
    bool implementsLog() const noexcept override { return true; }
    void logLog(clap_log_severity severity, const char* msg) const noexcept override;

    // Timer Support
    bool implementsTimerSupport() const noexcept { return true; }
    bool timerSupportRegister(uint32_t period_ms, clap_id* timer_id) noexcept;
    bool timerSupportUnregister(clap_id timer_id) noexcept;

    const clap_host* getClapHost() const { return clapHost(); }

private:
    CLAPPluginInstance* instance;

    static bool CLAP_ABI registerTimerFn(const clap_host_t* host, uint32_t period_ms, clap_id* timer_id);
    static bool CLAP_ABI unregisterTimerFn(const clap_host_t* host, clap_id timer_id);
    clap_host_timer_support_t timerHub{ registerTimerFn, unregisterTimerFn };

    struct TimerInfo : public juce::Timer
    {
        CLAPPluginInstance* instance;
        clap_id timerID;
        uint32_t period;

        void timerCallback() override;
    };
    std::vector<std::unique_ptr<TimerInfo>> timers;
    clap_id nextTimerID = 1;

    void handleAsyncUpdate() override;
};

// ── CLAPParameter ───────────────────────────────────────────────

class CLAPParameter : public juce::AudioProcessorParameterWithID
{
public:
    CLAPParameter(CLAPPluginInstance& owner,
                  const clap_plugin_t* plugin,
                  const clap_plugin_params_t* paramsExt,
                  const clap_param_info_t& info);

    float getValue() const override;
    void setValue(float newValue) override;
    float getDefaultValue() const override;
    juce::String getName(int maxLen) const override;
    juce::String getLabel() const override;
    juce::String getText(float value, int maxLen) const override;
    float getValueForText(const juce::String& text) const override;

    clap_id getClapID() const { return info.id; }
    bool isStepped() const { return (info.flags & CLAP_PARAM_IS_STEPPED) != 0; }
    double getPlainValue() const { return currentPlain.load(); }
    void setPlainValue(double v) { currentPlain.store(v); }

private:
    CLAPPluginInstance& owner;
    const clap_plugin_t* plugin;
    const clap_plugin_params_t* params;
    clap_param_info_t info;
    std::atomic<double> currentPlain{ 0.0 };
};

// ── Event list wrappers ─────────────────────────────────────────

class CLAPInputEvents final
{
public:
    static constexpr uint32_t MAX_EVENTS = 256;
    static constexpr size_t STORAGE_SIZE = 16384;

    CLAPInputEvents();
    ~CLAPInputEvents() = default;

    void clear();
    void push(const clap_event_header_t& event);
    const clap_input_events_t* getInterface() const { return &iface; }

private:
    static uint32_t CLAP_ABI sizeFn(const clap_input_events* list);
    static const clap_event_header_t* CLAP_ABI getFn(const clap_input_events* list, uint32_t index);

    clap_input_events_t iface;
    std::array<uint8_t, STORAGE_SIZE> storage{};
    std::array<const clap_event_header_t*, MAX_EVENTS> pointers{};
    uint32_t eventCount = 0;
    size_t storageUsed = 0;
};

class CLAPOutputEvents final
{
public:
    static constexpr uint32_t MAX_EVENTS = 256;
    static constexpr size_t STORAGE_SIZE = 16384;

    CLAPOutputEvents();
    ~CLAPOutputEvents() = default;

    void clear();
    const clap_output_events_t* getInterface() const { return &iface; }

    uint32_t getNumEvents() const { return eventCount; }
    const clap_event_header_t* getEvent(uint32_t i) const;

private:
    static bool CLAP_ABI tryPushFn(const clap_output_events* list,
                                   const clap_event_header_t* event);

    clap_output_events_t iface;
    std::array<uint8_t, STORAGE_SIZE> storage{};
    std::array<const clap_event_header_t*, MAX_EVENTS> pointers{};
    uint32_t eventCount = 0;
    size_t storageUsed = 0;
};

// ── CLAPPluginInstance ──────────────────────────────────────────

class CLAPPluginInstance : public juce::AudioPluginInstance
{
public:
    CLAPPluginInstance(std::shared_ptr<CLAPModule> module,
                       const clap_plugin_t* plugin,
                       std::unique_ptr<CLAPHost> host,
                       double sampleRate, int blockSize);
    ~CLAPPluginInstance() override;

    void initialize();

    // AudioProcessor
    const juce::String getName() const override;
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
    void reset() override;
    double getTailLengthSeconds() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    // Programs — single program, no-ops for hosted plugins
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    // State
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Plugin info
    void fillInPluginDescription(juce::PluginDescription& desc) const override;



    // Accessors
    const clap_plugin_t* getClapPlugin() const { return plugin; }
    CLAPHost& getHost() const { return *host; }

    void flushParameter(clap_id paramId, double value);
    void addCLAPParameter(std::unique_ptr<CLAPParameter> param);

private:
    void buildParameters();
    void buildBuses();
    void processMidiToClap(const juce::MidiBuffer& midi,
                           CLAPInputEvents& inEvents,
                           uint32_t framesCount);

    std::shared_ptr<CLAPModule> module;
    const clap_plugin_t* plugin;
    std::unique_ptr<CLAPHost> host;

    // Extensions
    const clap_plugin_params_t* paramsExt = nullptr;
    const clap_plugin_state_t* stateExt = nullptr;
    const clap_plugin_gui_t* guiExt = nullptr;
    const clap_plugin_audio_ports_t* audioPortsExt = nullptr;
    const clap_plugin_note_ports_t* notePortsExt = nullptr;

    // Parameters (raw pointers — JUCE owns via addHostedParameter)
    std::vector<CLAPParameter*> parameters;

    // Audio config
    juce::AudioBuffer<float> scratchBuffer;
    int numInputs = 0;
    int numOutputs = 0;
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;

    // Lifecycle state
    bool activated = false;
    bool processing = false;

    // Reusable event lists (pre-allocated)
    CLAPInputEvents inEvents;
    CLAPOutputEvents outEvents;
    clap_event_transport_t transportEvent{};
};
