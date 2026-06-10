#include "CLAPPluginInstance.h"
#include "CLAPPluginEditor.h"
#include <juce_core/juce_core.h>
#include <clap/helpers/host.hxx>

// ═══════════════════════════════════════════════════════════════
//  CLAPHost
// ═══════════════════════════════════════════════════════════════

CLAPHost::CLAPHost(CLAPPluginInstance* inst)
    : Host("HDAW", "HDAW", "https://github.com/user/hdaw", "0.2.0"),
      instance(inst)
{
}

bool CLAPHost::threadCheckIsMainThread() const noexcept
{
    return juce::MessageManager::getInstance()->isThisTheMessageThread();
}

bool CLAPHost::threadCheckIsAudioThread() const noexcept
{
    return !juce::MessageManager::getInstance()->isThisTheMessageThread();
}

void CLAPHost::requestRestart() noexcept
{
}

void CLAPHost::requestProcess() noexcept
{
}

void CLAPHost::requestCallback() noexcept
{
    juce::MessageManager::callAsync([this]() {
        if (instance != nullptr)
            const_cast<clap_plugin_t*>(instance->getClapPlugin())->on_main_thread(
                instance->getClapPlugin());
    });
}

const void* CLAPHost::getExtension(const char* id) const noexcept
{
    return Host::getExtension(id);
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
}

void CLAPHost::logLog(clap_log_severity severity, const char* msg) const noexcept
{
    juce::ignoreUnused(severity);
    juce::Logger::writeToLog("CLAP: " + juce::String(msg));
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
    storage.clear();
    pointers.clear();
}

void CLAPInputEvents::push(const clap_event_header_t& event)
{
    auto offset = storage.size();
    storage.resize(offset + event.size);
    std::memcpy(storage.data() + offset, &event, event.size);
    pointers.push_back(
        reinterpret_cast<const clap_event_header_t*>(storage.data() + offset));
}

uint32_t CLAP_ABI CLAPInputEvents::sizeFn(const clap_input_events* list)
{
    auto& self = *reinterpret_cast<const CLAPInputEvents*>(list->ctx);
    return static_cast<uint32_t>(self.pointers.size());
}

const clap_event_header_t* CLAP_ABI CLAPInputEvents::getFn(
    const clap_input_events* list, uint32_t index)
{
    auto& self = *reinterpret_cast<const CLAPInputEvents*>(list->ctx);
    if (index < self.pointers.size())
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
    events.clear();
}

const clap_event_header_t* CLAPOutputEvents::getEvent(uint32_t i) const
{
    if (i < static_cast<uint32_t>(events.size()))
        return &events[i];
    return nullptr;
}

bool CLAP_ABI CLAPOutputEvents::tryPushFn(
    const clap_output_events* list, const clap_event_header_t* event)
{
    auto& self = *reinterpret_cast<CLAPOutputEvents*>(list->ctx);
    self.events.push_back(*event);
    return true;
}

// ═══════════════════════════════════════════════════════════════
//  CLAPPluginInstance
// ═══════════════════════════════════════════════════════════════

CLAPPluginInstance::CLAPPluginInstance(std::shared_ptr<CLAPModule> mod,
                                       const clap_plugin_t* p,
                                       std::unique_ptr<CLAPHost> h,
                                       double sampleRate, int blockSize)
    : AudioPluginInstance(BusesProperties()
          .withInput("Main Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Main Output", juce::AudioChannelSet::stereo(), true)),
      module(std::move(mod)),
      plugin(p),
      host(std::move(h)),
      currentSampleRate(sampleRate),
      currentBlockSize(blockSize)
{
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
}

CLAPPluginInstance::~CLAPPluginInstance()
{
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
}

void CLAPPluginInstance::addCLAPParameter(std::unique_ptr<CLAPParameter> param)
{
    auto* ptr = param.get();
    parameters.push_back(std::move(param));
    addHostedParameter(std::unique_ptr<juce::AudioProcessorParameterWithID>(
        static_cast<juce::AudioProcessorParameterWithID*>(ptr)));
}

const juce::String CLAPPluginInstance::getName() const
{
    if (plugin != nullptr && plugin->desc != nullptr)
        return juce::String(plugin->desc->name);
    return "CLAP Plugin";
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
            if (info.flags & CLAP_AUDIO_PORT_IS_MAIN)
                numInputs = static_cast<int>(info.channel_count);
        }
    }

    numOutputs = 0;
    uint32_t outCount = audioPortsExt->count(plugin, false);
    for (uint32_t i = 0; i < outCount; ++i)
    {
        clap_audio_port_info_t info;
        if (audioPortsExt->get(plugin, i, false, &info))
        {
            if (info.flags & CLAP_AUDIO_PORT_IS_MAIN)
                numOutputs = static_cast<int>(info.channel_count);
        }
    }

    if (numInputs == 0) numInputs = 2;
    if (numOutputs == 0) numOutputs = 2;
}

void CLAPPluginInstance::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;

    if (plugin == nullptr)
        return;

    if (!activated)
    {
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

        plugin->activate(plugin, sampleRate, 1,
                         static_cast<uint32_t>((std::max)(samplesPerBlock, 1)));
        activated = true;

        plugin->start_processing(plugin);
        processing = true;
    }

    scratchBuffer.setSize((std::max)(numInputs, numOutputs), samplesPerBlock);
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
    juce::ScopedNoDenormals noDenormals;

    if (plugin == nullptr || !activated || !processing)
    {
        buffer.clear();
        midiMessages.clear();
        return;
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

    if (!stateExt->load(plugin, &stream))
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
