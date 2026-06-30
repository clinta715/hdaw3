#include "PluginProxySlot.h"
#include "ProxyEditor.h"
#include <cstring>

namespace proxy {

PluginProxySlot::PluginProxySlot(ProxyProcessManager& mgr, uint32_t id,
                                   const juce::String& name)
    : AudioPluginInstance(juce::AudioProcessor::BusesProperties()
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      processManager(mgr),
      slotId(id),
      pluginDisplayName(name)
{
}

PluginProxySlot::~PluginProxySlot() {
    releaseResources();
}

void PluginProxySlot::prepareToPlay(double sampleRate, int samplesPerBlock) {
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;

    auto* pipe = processManager.getPipe(slotId);
    if (!pipe) return;

    ProxyMessage msg{};
    msg.type = MessageType::PREPARE;
    msg.slotId = slotId;

    struct PrepareData {
        double sampleRate;
        int32_t blockSize;
        int32_t numChannels;
    };
    PrepareData data{};
    data.sampleRate = sampleRate;
    data.blockSize = samplesPerBlock;
    data.numChannels = numChannels;
    std::memcpy(msg.data, &data, sizeof(data));
    msg.dataSize = sizeof(data);

    pipe->sendMsg(msg);
    ProxyResponse resp{};
    pipe->receiveResp(resp);
}

void PluginProxySlot::releaseResources() {
}

void PluginProxySlot::processBlock(juce::AudioBuffer<float>& buffer,
                                    juce::MidiBuffer& midiMessages) {
    if (crashed.load()) return;

    auto* shm = processManager.getShm(slotId);
    if (!shm || !shm->getHeader()) return;

    auto* hdr = shm->getHeader();
    uint32_t cap = hdr->capacity;
    int totalSamples = buffer.getNumChannels() * buffer.getNumSamples();

    uint32_t w = hdr->inputWritePos.load(std::memory_order_relaxed);
    uint32_t r = hdr->inputReadPos.load(std::memory_order_acquire);
    if (static_cast<uint32_t>(totalSamples) > cap - (w - r)) return;

    float* inRing = shm->getInputRing();
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        for (int s = 0; s < buffer.getNumSamples(); ++s)
            inRing[(w + ch * buffer.getNumSamples() + s) & (cap - 1)] =
                buffer.getSample(ch, s);
    }
    hdr->inputWritePos.store(w + static_cast<uint32_t>(totalSamples),
                              std::memory_order_release);

    auto* pipe = processManager.getPipe(slotId);
    if (pipe) {
        ProxyMessage msg{};
        msg.type = MessageType::PROCESS_BLOCK;
        msg.slotId = slotId;
        pipe->sendMsg(msg);
    }

    uint32_t ow = hdr->outputWritePos.load(std::memory_order_relaxed);
    uint32_t or_ = hdr->outputReadPos.load(std::memory_order_acquire);
    uint32_t available = (ow >= or_) ? (ow - or_) : 0;

    if (available >= static_cast<uint32_t>(totalSamples)) {
        float* outRing = shm->getOutputRing();
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
            for (int s = 0; s < buffer.getNumSamples(); ++s)
                buffer.setSample(ch, s,
                    outRing[(or_ + ch * buffer.getNumSamples() + s) & (cap - 1)]);
        }
        hdr->outputReadPos.store(or_ + static_cast<uint32_t>(totalSamples),
                                  std::memory_order_release);
    } else {
        buffer.clear();
    }

    midiMessages.clear();
}

void PluginProxySlot::reset() {
}

void PluginProxySlot::getStateInformation(juce::MemoryBlock& destData) {
    auto* pipe = processManager.getPipe(slotId);
    if (!pipe) return;

    ProxyMessage msg{};
    msg.type = MessageType::GET_STATE;
    msg.slotId = slotId;
    pipe->sendMsg(msg);

    ProxyResponse resp{};
    if (pipe->receiveResp(resp) && resp.result == 1 && resp.dataSize > 0) {
        destData.append(resp.data, resp.dataSize);
    }
}

void PluginProxySlot::setStateInformation(const void* data, int sizeInBytes) {
    auto* pipe = processManager.getPipe(slotId);
    if (!pipe || !data || sizeInBytes <= 0) return;

    ProxyMessage msg{};
    msg.type = MessageType::SET_STATE;
    msg.slotId = slotId;
    msg.dataSize = static_cast<uint32_t>(sizeInBytes);
    auto copySize = static_cast<size_t>(sizeInBytes);
    auto maxData = sizeof(msg.data);
    if (copySize > maxData) copySize = maxData;
    std::memcpy(msg.data, data, copySize);
    pipe->sendMsg(msg);

    ProxyResponse resp{};
    pipe->receiveResp(resp);
}

const juce::String PluginProxySlot::getName() const {
    return pluginDisplayName;
}

void PluginProxySlot::fillInPluginDescription(juce::PluginDescription& desc) const {
    desc.name = pluginDisplayName;
    desc.pluginFormatName = "Isolated";
    desc.fileOrIdentifier = "isolated_" + juce::String(static_cast<int>(slotId));
}

juce::AudioProcessorEditor* PluginProxySlot::createEditor() {
    return new ProxyEditor(*this);
}

bool PluginProxySlot::hasEditor() const {
    return true;
}

void PluginProxySlot::onChildCrashed() {
    crashed.store(true);
}

} // namespace proxy
