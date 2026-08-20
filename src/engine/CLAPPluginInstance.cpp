#include "CLAPPluginInstance.h"
#include "CLAPPluginEditor.h"
#include "../proxy/PluginProxySlot.h"
#include "../common/DebugLog.h"
#include "../common/RunOnMessageThreadBounded.h"
#include <juce_core/juce_core.h>
#include <clap/helpers/host.hxx>

// ═══════════════════════════════════════════════════════════════
//  CLAPHost
// ═══════════════════════════════════════════════════════════════

CLAPHost::CLAPHost(CLAPPluginInstance* inst)
    : Host("HDAW", "HDAW", "https://github.com/user/hdaw", "0.3.0"),
      instance(inst)
{
}

bool CLAPHost::threadCheckIsMainThread() const noexcept
{
    // Accept both the JUCE message thread (normal live path) and the
    // export render thread (offline render path). CLAP spec defines the
    // "main thread" as the thread that called entry->init(); during
    // offline render the module is loaded on the render worker thread,
    // so CLAP plugins must treat that thread as main.
    return juce::MessageManager::getInstance()->isThisTheMessageThread()
        || proxy::isRenderThread();
}

bool CLAPHost::threadCheckIsAudioThread() const noexcept
{
    // The CLAP audio thread is the one running process() — recorded in
    // CLAPPluginInstance::processBlock. Reporting "any thread that is not
    // the message thread" is wrong: it marks the Qt main thread (headless
    // MCP tool dispatch / frontend command thread) as the audio thread,
    // and clap-helpers then terminates plugins that legally call
    // request_flush() from the main thread ([thread-safe,!audio-thread]).
    const auto audio = audioThreadId.load(std::memory_order_relaxed);
    return audio != std::thread::id{} && std::this_thread::get_id() == audio;
}

void CLAPHost::requestRestart() noexcept
{
}

void CLAPHost::requestProcess() noexcept
{
}

void CLAPHost::requestCallback() noexcept
{
    static std::atomic<int> reqCount{0};
    int rc = reqCount.fetch_add(1, std::memory_order_relaxed);
    if (rc < 5 || (rc % 100) == 0)
        HDAW_LOG("CLAPHost", "requestCallback count=" + juce::String(rc));
    triggerAsyncUpdate();
}

void CLAPHost::handleAsyncUpdate()
{
    static std::atomic<int> cbCount{0};
    int cc = cbCount.fetch_add(1, std::memory_order_relaxed);
    if (cc < 5 || (cc % 100) == 0)
        HDAW_LOG("CLAPHost", "on_main_thread callback count=" + juce::String(cc));
    if (instance != nullptr)
    {
        auto* p = const_cast<clap_plugin_t*>(instance->getClapPlugin());
        if (p != nullptr)
            p->on_main_thread(p);
    }
}

const void* CLAPHost::getExtension(const char* id) const noexcept
{
    if (std::strcmp(id, CLAP_EXT_TIMER_SUPPORT) == 0)
        return &timerHub;
    if (std::strcmp(id, CLAP_EXT_PRESET_LOAD) == 0)
        return &presetLoadHub;
    // Plugins advertising only the compat id (e.g. Altitude) may query with it too.
    if (std::strcmp(id, CLAP_EXT_PRESET_LOAD_COMPAT) == 0)
        return &presetLoadHub;
    return Host::getExtension(id);
}

void CLAP_ABI CLAPHost::presetLoadOnErrorFn(const clap_host_t*,
    uint32_t location_kind, const char* location, const char* load_key,
    int32_t os_error, const char* msg)
{
    HDAW_LOG("clap_preset_load", juce::String("on_error: ")
        + (msg != nullptr ? msg : "")
        + " os=" + juce::String(os_error)
        + " kind=" + juce::String(static_cast<int>(location_kind))
        + " location=" + (location != nullptr ? location : "")
        + " load_key=" + (load_key != nullptr ? load_key : ""));
}

void CLAP_ABI CLAPHost::presetLoadLoadedFn(const clap_host_t*,
    uint32_t location_kind, const char* location, const char* load_key)
{
    HDAW_LOG("clap_preset_load", juce::String("loaded: kind=")
        + juce::String(static_cast<int>(location_kind))
        + " location=" + (location != nullptr ? location : "")
        + " load_key=" + (load_key != nullptr ? load_key : ""));
}

void CLAPHost::paramsRescan(clap_param_rescan_flags flags) noexcept
{
    juce::ignoreUnused(flags);
}

void CLAPHost::paramsClear(clap_id paramId, clap_param_clear_flags flags) noexcept
{
    juce::ignoreUnused(paramId, flags);
}

void CLAPHost::stateMarkDirty() noexcept
{
}

bool CLAPHost::guiRequestResize(uint32_t w, uint32_t h) noexcept
{
    if (instance == nullptr)
        return false;
    auto* editor = dynamic_cast<CLAPPluginEditor*>(instance->getActiveEditor());
    if (editor == nullptr)
        return false;
    juce::MessageManager::callAsync([editor, w, h]() {
        editor->setSize(static_cast<int>(w), static_cast<int>(h));
    });
    return true;
}

void CLAPHost::guiClosed(bool wasDestroyed) noexcept
{
    juce::ignoreUnused(wasDestroyed);
}

void CLAPHost::latencyChanged() noexcept
{
    if (instance != nullptr)
    {
        auto* p = instance->getClapPlugin();
        if (p != nullptr)
        {
            auto* latExt = static_cast<const clap_plugin_latency_t*>(
                p->get_extension(p, CLAP_EXT_LATENCY));
            if (latExt != nullptr)
            {
                auto pluginLatency = latExt->get(p);
                instance->setLatencySamples(static_cast<int>(pluginLatency));
                instance->updateHostDisplay();
            }
        }
    }
}

void CLAPHost::logLog(clap_log_severity severity, const char* msg) const noexcept
{
    juce::ignoreUnused(severity);
    juce::Logger::writeToLog("CLAP: " + juce::String(msg));
}

bool CLAPHost::timerSupportRegister(uint32_t period_ms, clap_id* timer_id) noexcept
{
    if (timer_id == nullptr) return false;

    auto t = std::make_unique<TimerInfo>();
    t->instance = instance;
    t->timerID = nextTimerID++;
    t->period = period_ms;
    t->startTimer(static_cast<int>(period_ms));

    *timer_id = t->timerID;
    timers.push_back(std::move(t));
    return true;
}

bool CLAPHost::timerSupportUnregister(clap_id timer_id) noexcept
{
    for (auto it = timers.begin(); it != timers.end(); ++it)
    {
        if ((*it)->timerID == timer_id)
        {
            (*it)->stopTimer();
            timers.erase(it);
            return true;
        }
    }
    return false;
}

bool CLAP_ABI CLAPHost::registerTimerFn(const clap_host_t* host, uint32_t period_ms, clap_id* timer_id)
{
    auto& self = *reinterpret_cast<CLAPHost*>(host->host_data);
    return self.timerSupportRegister(period_ms, timer_id);
}

bool CLAP_ABI CLAPHost::unregisterTimerFn(const clap_host_t* host, clap_id timer_id)
{
    auto& self = *reinterpret_cast<CLAPHost*>(host->host_data);
    return self.timerSupportUnregister(timer_id);
}

void CLAPHost::TimerInfo::timerCallback()
{
    if (instance != nullptr)
    {
        auto* p = const_cast<clap_plugin_t*>(instance->getClapPlugin());
        if (p != nullptr)
        {
            auto* timerExt = static_cast<const clap_plugin_timer_support_t*>(
                p->get_extension(p, CLAP_EXT_TIMER_SUPPORT));
            if (timerExt != nullptr && timerExt->on_timer != nullptr)
                timerExt->on_timer(p, timerID);
        }
    }
}

// ═══════════════════════════════════════════════════════════════
//  CLAPParameter
// ═══════════════════════════════════════════════════════════════

CLAPParameter::CLAPParameter(CLAPPluginInstance& own,
                             const clap_plugin_t* p,
                             const clap_plugin_params_t* pe,
                             const clap_param_info_t& i)
    : AudioProcessorParameterWithID(juce::String(static_cast<int>(i.id)),
                                    juce::String(i.name)),
      owner(own), plugin(p), params(pe), info(i)
{
    currentPlain.store(info.default_value);
}

float CLAPParameter::getValue() const
{
    double plain = currentPlain.load();
    double range = info.max_value - info.min_value;
    if (range <= 0.0) return 0.0f;
    return static_cast<float>((plain - info.min_value) / range);
}

void CLAPParameter::setValue(float newValue)
{
    double range = info.max_value - info.min_value;
    double plain = info.min_value + static_cast<double>(newValue) * range;
    currentPlain.store(plain);

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        if (params != nullptr)
            params->flush(plugin, nullptr, nullptr);
    }
    else
    {
        owner.flushParameter(info.id, plain);
    }
}

float CLAPParameter::getDefaultValue() const
{
    double range = info.max_value - info.min_value;
    if (range <= 0.0) return 0.0f;
    return static_cast<float>((info.default_value - info.min_value) / range);
}

juce::String CLAPParameter::getName(int maxLen) const
{
    return juce::String(info.name).substring(0, maxLen);
}

juce::String CLAPParameter::getLabel() const
{
    return {};
}

juce::String CLAPParameter::getText(float value, int maxLen) const
{
    if (params == nullptr || params->value_to_text == nullptr)
        return AudioProcessorParameterWithID::getText(value, maxLen);

    double range = info.max_value - info.min_value;
    double plain = info.min_value + static_cast<double>(value) * range;

    char buf[256] = {};
    if (params->value_to_text(plugin, info.id, plain, buf, sizeof(buf)))
        return juce::String(buf).substring(0, maxLen);
    return AudioProcessorParameterWithID::getText(value, maxLen);
}

float CLAPParameter::getValueForText(const juce::String& text) const
{
    if (params != nullptr && params->text_to_value != nullptr)
    {
        double out = info.default_value;
        if (params->text_to_value(plugin, info.id, text.toRawUTF8(), &out))
        {
            double range = info.max_value - info.min_value;
            if (range > 0.0)
                return static_cast<float>((out - info.min_value) / range);
        }
    }
    return getDefaultValue();
}

// ═══════════════════════════════════════════════════════════════
//  CLAPInputEvents
// ═══════════════════════════════════════════════════════════════

CLAPInputEvents::CLAPInputEvents()
{
    iface.ctx = this;
    iface.size = &sizeFn;
    iface.get = &getFn;
}

void CLAPInputEvents::clear()
{
    eventCount = 0;
    storageUsed = 0;
}

void CLAPInputEvents::push(const clap_event_header_t& event)
{
    if (eventCount >= MAX_EVENTS) return;
    if (storageUsed + event.size > STORAGE_SIZE) return;

    auto offset = storageUsed;
    std::memcpy(storage.data() + offset, &event, event.size);
    pointers[eventCount] =
        reinterpret_cast<const clap_event_header_t*>(storage.data() + offset);
    ++eventCount;
    storageUsed += event.size;
}

uint32_t CLAP_ABI CLAPInputEvents::sizeFn(const clap_input_events* list)
{
    auto& self = *reinterpret_cast<const CLAPInputEvents*>(list->ctx);
    return self.eventCount;
}

const clap_event_header_t* CLAP_ABI CLAPInputEvents::getFn(
    const clap_input_events* list, uint32_t index)
{
    auto& self = *reinterpret_cast<const CLAPInputEvents*>(list->ctx);
    if (index < self.eventCount)
        return self.pointers[index];
    return nullptr;
}

// ═══════════════════════════════════════════════════════════════
//  CLAPOutputEvents
// ═══════════════════════════════════════════════════════════════

CLAPOutputEvents::CLAPOutputEvents()
{
    iface.ctx = this;
    iface.try_push = &tryPushFn;
}

void CLAPOutputEvents::clear()
{
    eventCount = 0;
    storageUsed = 0;
}

const clap_event_header_t* CLAPOutputEvents::getEvent(uint32_t i) const
{
    if (i < eventCount)
        return pointers[i];
    return nullptr;
}

bool CLAP_ABI CLAPOutputEvents::tryPushFn(
    const clap_output_events* list, const clap_event_header_t* event)
{
    auto& self = *reinterpret_cast<CLAPOutputEvents*>(list->ctx);
    if (self.eventCount >= MAX_EVENTS) return false;
    if (self.storageUsed + event->size > STORAGE_SIZE) return false;

    auto offset = self.storageUsed;
    std::memcpy(self.storage.data() + offset, event, event->size);
    self.pointers[self.eventCount] =
        reinterpret_cast<const clap_event_header_t*>(self.storage.data() + offset);
    ++self.eventCount;
    self.storageUsed += event->size;
    return true;
}

// ═══════════════════════════════════════════════════════════════
//  CLAPPluginInstance
// ═══════════════════════════════════════════════════════════════

CLAPPluginInstance::CLAPPluginInstance(std::shared_ptr<CLAPModule> mod,
                                       const clap_plugin_t* p,
                                       std::unique_ptr<CLAPHost> h)
    : AudioPluginInstance(BusesProperties()
          .withInput("Main Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Main Output", juce::AudioChannelSet::stereo(), true)),
      module(std::move(mod)),
      plugin(p),
      host(std::move(h))
{
    alive = std::make_shared<std::atomic<bool>>(true);

    paramsExt = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    stateExt = static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    guiExt = static_cast<const clap_plugin_gui_t*>(
        plugin->get_extension(plugin, CLAP_EXT_GUI));
    audioPortsExt = static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    notePortsExt = static_cast<const clap_plugin_note_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
    presetLoadExt = static_cast<const clap_plugin_preset_load_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PRESET_LOAD));
    if (presetLoadExt == nullptr)
        presetLoadExt = static_cast<const clap_plugin_preset_load_t*>(
            plugin->get_extension(plugin, CLAP_EXT_PRESET_LOAD_COMPAT));

    if (plugin->desc != nullptr && plugin->desc->id != nullptr)
        clapPluginId = juce::String::fromUTF8(plugin->desc->id);
}

CLAPPluginInstance::~CLAPPluginInstance()
{
    // First: invalidate any preset-load lambda still queued from a timed-out marshal.
    if (alive) alive->store(false, std::memory_order_release);

    releaseResources();

    if (plugin != nullptr)
    {
        plugin->destroy(plugin);
        plugin = nullptr;
    }
}

void CLAPPluginInstance::initialize()
{
    host->setInstance(this);
    buildParameters();
    buildBuses();

    HDAW_LOG("clap_host", "ext-probe plugin=" + getName()
        + " params=" + juce::String(paramsExt != nullptr ? static_cast<int>(paramsExt->count(plugin)) : 0)
        + " program-list=" + juce::String(plugin->get_extension(plugin, "clap.program-list") ? "true" : "false")
        + " preset-load=" + juce::String(plugin->get_extension(plugin, "clap.preset-load") ? "true" : "false")
        + " preset-load/2=" + juce::String(plugin->get_extension(plugin, "clap.preset-load/2") ? "true" : "false")
        + " preset-load.draft/2=" + juce::String(plugin->get_extension(plugin, "clap.preset-load.draft/2") ? "true" : "false")
        + " preset-discovery=" + juce::String(plugin->get_extension(plugin, "clap.preset-discovery") ? "true" : "false"));
}

void CLAPPluginInstance::addCLAPParameter(std::unique_ptr<CLAPParameter> param)
{
    auto* ptr = param.get();
    parameters.push_back(ptr);
    addHostedParameter(std::move(param));
}

const juce::String CLAPPluginInstance::getName() const
{
    if (plugin != nullptr && plugin->desc != nullptr)
        return juce::String(plugin->desc->name);
    return "CLAP Plugin";
}

// ── Programs (preset-discovery database + preset-load extension) ──

void CLAPPluginInstance::ensurePresets() const
{
    if (presets != nullptr || module == nullptr || module->loadedPath.isEmpty())
        return;
    // Double-checked: concurrent first access from control vs message thread (isolated child).
    const std::lock_guard<std::mutex> lock(presetsMutex);
    if (presets != nullptr)
        return;
    presets = CLAPPresetDatabase::ModulePresets::getForModule(module->loadedPath, module);
    if (presets != nullptr)
        presets->startBuildAsync();
}

const std::vector<CLAPPresetEntry>* CLAPPluginInstance::presetList() const
{
    ensurePresets();
    if (presets == nullptr || !presets->isReady())
        return nullptr;
    return presets->presetsFor(clapPluginId);
}

int CLAPPluginInstance::getNumPrograms()
{
    if (const auto* list = presetList())
        return static_cast<int>(list->size());
    return 1; // VST3-parity default (also while the async build is in flight)
}

int CLAPPluginInstance::getCurrentProgram()
{
    return currentProgram.load(std::memory_order_relaxed);
}

const juce::String CLAPPluginInstance::getProgramName(int index)
{
    if (const auto* list = presetList())
        if (index >= 0 && index < static_cast<int>(list->size()))
            return (*list)[static_cast<size_t>(index)].name;
    return {};
}

void CLAPPluginInstance::setCurrentProgram(int index)
{
    // Missing extension or list: silent no-op (VST3-parity).
    if (presetLoadExt == nullptr || presetLoadExt->from_location == nullptr)
        return;
    const auto* list = presetList();
    if (list == nullptr || index < 0 || index >= static_cast<int>(list->size()))
        return;

    // Copy: the lambda may outlive this call on the timeout path.
    const CLAPPresetEntry entry = (*list)[static_cast<size_t>(index)];
    const clap_plugin_preset_load_t* loadExt = presetLoadExt;
    const clap_plugin_t* p = plugin;
    // Alive-flag idiom: on marshal timeout the posted lambda STILL runs later;
    // it must never touch a destroyed instance, so no `this` capture.
    auto aliveFlag = alive;
    std::atomic<int>* programOut = &currentProgram;
    auto doLoad = [aliveFlag, programOut, index, entry, loadExt, p]() {
        if (aliveFlag == nullptr || !aliveFlag->load(std::memory_order_acquire))
            return;
        bool ok = false;
        try
        {
            ok = loadExt->from_location(p, entry.locationKind,
                entry.location.isEmpty() ? nullptr : entry.location.toRawUTF8(),
                entry.loadKey.isEmpty() ? nullptr : entry.loadKey.toRawUTF8());
        }
        catch (...)
        {
            HDAW_LOG("clap_preset_load", "from_location threw for '" + entry.name + "'");
            ok = false;
        }
        if (ok)
        {
            // Re-check before the store; same-thread serialization makes the window negligible.
            if (aliveFlag != nullptr && aliveFlag->load(std::memory_order_acquire))
                programOut->store(index, std::memory_order_relaxed);
        }
        else
            HDAW_LOG("clap_preset_load", "from_location failed for '" + entry.name + "'");
    };

    // from_location is [main-thread] (preset-load.h). In-process callers may
    // run on the command/MCP thread — self-marshal with a bounded wait so a
    // stuck plugin can never hang the caller.
    if (host != nullptr && host->threadCheckIsMainThread())
    {
        doLoad();
        return;
    }
    if (!runOnMessageThreadBounded(doLoad, 5000))
        HDAW_LOG("clap_preset_load", "marshal timed out for '" + entry.name
            + "' — program unchanged");
}

void CLAPPluginInstance::buildParameters()
{
    parameters.clear();


    if (paramsExt == nullptr)
        return;

    uint32_t count = paramsExt->count(plugin);
    for (uint32_t i = 0; i < count; ++i)
    {
        clap_param_info_t info;
        if (paramsExt->get_info(plugin, i, &info))
        {
            auto param = std::make_unique<CLAPParameter>(
                *this, plugin, paramsExt, info);
            addCLAPParameter(std::move(param));
        }
    }
}

void CLAPPluginInstance::buildBuses()
{
    if (audioPortsExt == nullptr)
    {
        numInputs = 2;
        numOutputs = 2;
        return;
    }

    numInputs = 0;
    uint32_t inCount = audioPortsExt->count(plugin, true);
    for (uint32_t i = 0; i < inCount; ++i)
    {
        clap_audio_port_info_t info;
        if (audioPortsExt->get(plugin, i, true, &info))
        {
            // Main input port only — sidechain/aux inputs must not be summed
            // into the main mix width.
            if (info.flags & CLAP_AUDIO_PORT_IS_MAIN)
                numInputs = static_cast<int>(info.channel_count);
        }
    }

    // Sum ALL output ports: multi-port CLAP plugins (e.g. the gearmulator
    // Nord-2x port with "Out AB" + "Out CD") write to every declared port.
    // Reading only the main port starves the plugin of channels and its
    // output copy then writes past the host buffer. Single-main-port
    // plugins (the common case) sum to their one port's width.
    numOutputs = 0;
    uint32_t outCount = audioPortsExt->count(plugin, false);
    for (uint32_t i = 0; i < outCount; ++i)
    {
        clap_audio_port_info_t info;
        if (audioPortsExt->get(plugin, i, false, &info))
            numOutputs += static_cast<int>(info.channel_count);
    }

    if (numInputs == 0) numInputs = 2;
    if (numOutputs == 0) numOutputs = 2;
}

void CLAPPluginInstance::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    if (plugin == nullptr)
        return;

    if (!activated)
    {
        plugin->activate(plugin, sampleRate, 1,
                         static_cast<uint32_t>((std::max)(samplesPerBlock, 1)));
        activated = true;

        // start_processing() is intentionally NOT called here: the CLAP
        // spec requires it to be called on the audio thread (the thread
        // that calls process). Strict plugins (Odin2) abort if it is
        // called on the message thread — defer to the first processBlock.
    }
}

void CLAPPluginInstance::releaseResources()
{
    if (plugin == nullptr)
        return;

    if (processing)
    {
        plugin->stop_processing(plugin);
        processing = false;
    }

    if (activated)
    {
        plugin->deactivate(plugin);
        activated = false;
    }
}

void CLAPPluginInstance::reset()
{
    if (plugin != nullptr)
        plugin->reset(plugin);
}

double CLAPPluginInstance::getTailLengthSeconds() const
{
    return 0.0;
}

bool CLAPPluginInstance::acceptsMidi() const
{
    return true;
}

bool CLAPPluginInstance::producesMidi() const
{
    return notePortsExt != nullptr;
}

void CLAPPluginInstance::processMidiToClap(const juce::MidiBuffer& midi,
                                           CLAPInputEvents& events,
                                           uint32_t framesCount)
{
    juce::ignoreUnused(framesCount);

    for (const auto& meta : midi)
    {
        auto msg = meta.getMessage();
        int samplePos = (std::min)(meta.samplePosition,
                                 static_cast<int>(framesCount) - 1);
        if (samplePos < 0) samplePos = 0;

        if (msg.isNoteOn())
        {
            clap_event_note_t note{};
            note.header.size = sizeof(clap_event_note_t);
            note.header.time = static_cast<uint32_t>(samplePos);
            note.header.type = CLAP_EVENT_NOTE_ON;
            note.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            note.port_index = 0;
            note.channel = static_cast<int16_t>(msg.getChannel() - 1);
            note.key = static_cast<int16_t>(msg.getNoteNumber());
            note.velocity = msg.getFloatVelocity();
            note.note_id = -1;
            events.push(note.header);
        }
        else if (msg.isNoteOff())
        {
            clap_event_note_t note{};
            note.header.size = sizeof(clap_event_note_t);
            note.header.time = static_cast<uint32_t>(samplePos);
            note.header.type = CLAP_EVENT_NOTE_OFF;
            note.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            note.port_index = 0;
            note.channel = static_cast<int16_t>(msg.getChannel() - 1);
            note.key = static_cast<int16_t>(msg.getNoteNumber());
            note.velocity = msg.getFloatVelocity();
            note.note_id = -1;
            events.push(note.header);
        }
        else if (msg.isController())
        {
            clap_event_midi_t midiEvent{};
            midiEvent.header.size = sizeof(clap_event_midi_t);
            midiEvent.header.time = static_cast<uint32_t>(samplePos);
            midiEvent.header.type = CLAP_EVENT_MIDI;
            midiEvent.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            midiEvent.port_index = 0;
            midiEvent.data[0] = static_cast<uint8_t>(msg.getChannel() - 1);
            midiEvent.data[1] = static_cast<uint8_t>(msg.getControllerNumber());
            midiEvent.data[2] = static_cast<uint8_t>(msg.getControllerValue());
            events.push(midiEvent.header);
        }
    }
}

void CLAPPluginInstance::processBlock(juce::AudioBuffer<float>& buffer,
                                      juce::MidiBuffer& midiMessages)
{
    host->audioThreadId.store(std::this_thread::get_id(), std::memory_order_relaxed);
    juce::ScopedNoDenormals noDenormals;

    if (plugin == nullptr || !activated)
    {
        buffer.clear();
        midiMessages.clear();
        return;
    }

    if (!processing)
    {
        // start_processing() must be called on the audio thread (the thread
        // that calls process). Deferred here from prepareToPlay — strict
        // plugins (Odin2) abort if called on the message thread.
        plugin->start_processing(plugin);
        processing = true;
    }

    uint32_t frames = static_cast<uint32_t>(buffer.getNumSamples());
    if (frames == 0) return;

    clap_process_t process;
    std::memset(&process, 0, sizeof(process));

    process.steady_time = -1;
    process.frames_count = frames;

    // Audio buffers
    clap_audio_buffer_t audioIn{};
    clap_audio_buffer_t audioOut{};

    if (numInputs > 0 && buffer.getNumChannels() > 0)
    {
        audioIn.data32 = const_cast<float**>(buffer.getArrayOfReadPointers());
        audioIn.channel_count = static_cast<uint32_t>(
            (std::min)(numInputs, buffer.getNumChannels()));
        process.audio_inputs = &audioIn;
        process.audio_inputs_count = 1;
    }

    if (numOutputs > 0)
    {
        audioOut.data32 = const_cast<float**>(buffer.getArrayOfWritePointers());
        audioOut.channel_count = static_cast<uint32_t>(
            (std::min)(numOutputs, buffer.getNumChannels()));
        process.audio_outputs = &audioOut;
        process.audio_outputs_count = 1;

        for (int c = buffer.getNumChannels(); c < numOutputs; ++c)
            buffer.clear(c, 0, static_cast<int>(frames));
    }

    // Input events
    inEvents.clear();
    processMidiToClap(midiMessages, inEvents, frames);

    // Transport event
    // Diagnostic knob: HDAW_NO_TRANSPORT_EVENT=1 suppresses the CLAP
    // transport event (test whether transport events trigger plugin
    // crashes/silence).
    static const bool noTransportEvent =
        juce::SystemStats::getEnvironmentVariable("HDAW_NO_TRANSPORT_EVENT", "") == "1";
    if (!noTransportEvent)
    if (auto* ph = getPlayHead())
    {
        auto pos = ph->getPosition();
        if (pos)
        {
            std::memset(&transportEvent, 0, sizeof(transportEvent));
            transportEvent.header.size = sizeof(clap_event_transport_t);
            transportEvent.header.time = 0;
            transportEvent.header.type = CLAP_EVENT_TRANSPORT;
            transportEvent.header.space_id = CLAP_CORE_EVENT_SPACE_ID;

            if (pos->getIsPlaying())
                transportEvent.flags |= CLAP_TRANSPORT_IS_PLAYING;

            if (pos->getTimeInSeconds().hasValue())
            {
                transportEvent.flags |= CLAP_TRANSPORT_HAS_SECONDS_TIMELINE;
                transportEvent.song_pos_seconds =
                    static_cast<clap_sectime>(pos->getTimeInSeconds().orFallback(0.0)
                                              * CLAP_SECTIME_FACTOR);
            }

            if (pos->getBpm().hasValue())
            {
                transportEvent.flags |= CLAP_TRANSPORT_HAS_TEMPO;
                transportEvent.tempo = pos->getBpm().orFallback(120.0);
            }

            if (pos->getTimeSignature().hasValue())
            {
                transportEvent.flags |= CLAP_TRANSPORT_HAS_TIME_SIGNATURE;
                auto ts = pos->getTimeSignature().orFallback(
                    juce::AudioPlayHead::TimeSignature{4, 4});
                transportEvent.tsig_num = ts.numerator;
                transportEvent.tsig_denom = ts.denominator;
            }

            if (pos->getPpqPosition().hasValue())
            {
                transportEvent.flags |= CLAP_TRANSPORT_HAS_BEATS_TIMELINE;
                transportEvent.song_pos_beats =
                    static_cast<clap_beattime>(pos->getPpqPosition().orFallback(0.0)
                                               * CLAP_BEATTIME_FACTOR);
            }

            inEvents.push(transportEvent.header);
        }
    }

    process.in_events = inEvents.getInterface();

    // Output events
    outEvents.clear();
    process.out_events = outEvents.getInterface();

    auto status = plugin->process(plugin, &process);

    // Output events → MIDI
    midiMessages.clear();
    for (uint32_t i = 0; i < outEvents.getNumEvents(); ++i)
    {
        const auto* ev = outEvents.getEvent(i);
        if (ev == nullptr) continue;

        if (ev->type == CLAP_EVENT_NOTE_ON || ev->type == CLAP_EVENT_NOTE_OFF)
        {
            const auto* note = reinterpret_cast<const clap_event_note_t*>(ev);
            auto msg = (ev->type == CLAP_EVENT_NOTE_ON)
                ? juce::MidiMessage::noteOn(note->channel + 1, note->key,
                                             static_cast<float>(note->velocity))
                : juce::MidiMessage::noteOff(note->channel + 1, note->key,
                                              static_cast<float>(note->velocity));
            midiMessages.addEvent(msg, static_cast<int>(ev->time));
        }
        else if (ev->type == CLAP_EVENT_MIDI)
        {
            const auto* midiEv = reinterpret_cast<const clap_event_midi_t*>(ev);
            auto msg = juce::MidiMessage(midiEv->data[0],
                                          midiEv->data[1],
                                          midiEv->data[2]);
            midiMessages.addEvent(msg, static_cast<int>(ev->time));
        }
    }

    if (status == CLAP_PROCESS_ERROR)
    {
        buffer.clear();
        midiMessages.clear();
    }
}

juce::AudioProcessorEditor* CLAPPluginInstance::createEditor()
{
    return new CLAPPluginEditor(*this);
}

bool CLAPPluginInstance::hasEditor() const
{
    return guiExt != nullptr;
}

void CLAPPluginInstance::getStateInformation(juce::MemoryBlock& destData)
{
    if (stateExt == nullptr)
        return;

    struct WriteStream : public clap_ostream
    {
        juce::MemoryBlock* dest = nullptr;

        static int64_t CLAP_ABI writeFn(const clap_ostream* stream,
                                         const void* data, uint64_t size)
        {
            auto& self = *const_cast<WriteStream*>(
                static_cast<const WriteStream*>(stream));
            self.dest->append(data, static_cast<size_t>(size));
            return static_cast<int64_t>(size);
        }
    };

    WriteStream stream;
    stream.ctx = &stream;
    stream.write = &WriteStream::writeFn;
    stream.dest = &destData;

    if (!stateExt->save(plugin, &stream))
        juce::Logger::writeToLog("HDAW: CLAP plugin state save failed.");
}

void CLAPPluginInstance::setStateInformation(const void* data, int sizeInBytes)
{
    if (stateExt == nullptr || sizeInBytes <= 0)
        return;

    struct ReadStream : public clap_istream
    {
        const uint8_t* src = nullptr;
        uint64_t pos = 0;
        uint64_t total = 0;

        static int64_t CLAP_ABI readFn(const clap_istream* stream,
                                        void* buffer, uint64_t size)
        {
            auto& self = *const_cast<ReadStream*>(
                static_cast<const ReadStream*>(stream));
            uint64_t available = self.total - self.pos;
            uint64_t toRead = (std::min)(size, available);
            if (toRead == 0) return 0;
            std::memcpy(buffer, self.src + self.pos, static_cast<size_t>(toRead));
            self.pos += toRead;
            return static_cast<int64_t>(toRead);
        }
    };

    ReadStream stream;
    stream.ctx = &stream;
    stream.read = &ReadStream::readFn;
    stream.src = static_cast<const uint8_t*>(data);
    stream.total = static_cast<uint64_t>(sizeInBytes);

    const bool loadOk = stateExt->load(plugin, &stream);
    if (!loadOk)
        juce::Logger::writeToLog("HDAW: CLAP plugin state load failed.");
}

void CLAPPluginInstance::fillInPluginDescription(
    juce::PluginDescription& desc) const
{
    if (plugin != nullptr && plugin->desc != nullptr)
    {
        desc.name = juce::String(plugin->desc->name);
        desc.descriptiveName = juce::String(plugin->desc->name);
        desc.manufacturerName = juce::String(
            plugin->desc->vendor ? plugin->desc->vendor : "");
        desc.version = juce::String(
            plugin->desc->version ? plugin->desc->version : "1.0.0");
        desc.fileOrIdentifier = {};
    }

    desc.pluginFormatName = "CLAP";
    desc.numInputChannels = numInputs;
    desc.numOutputChannels = numOutputs;
    desc.isInstrument = false;

    if (plugin != nullptr && plugin->desc != nullptr && plugin->desc->features)
    {
        const auto* features = plugin->desc->features;
        while (*features != nullptr)
        {
            if (std::strcmp(*features, CLAP_PLUGIN_FEATURE_INSTRUMENT) == 0)
                desc.isInstrument = true;
            ++features;
        }
    }
}

void CLAPPluginInstance::flushParameter(clap_id paramId, double value)
{
    for (const auto& p : parameters)
    {
        if (p->getClapID() == paramId)
        {
            p->setPlainValue(value);
            break;
        }
    }
}
