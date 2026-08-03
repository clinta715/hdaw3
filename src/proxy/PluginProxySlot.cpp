#include "PluginProxySlot.h"
#include "ProxyEditor.h"
#include "CrashDialog.h"
#include "../common/DebugLog.h"
#include <cstring>
#include <chrono>
#include <thread>

namespace proxy {

PluginProxySlot::PluginProxySlot(ProxyProcessManager& mgr, uint32_t id,
                                   const juce::String& name)
    : AudioPluginInstance(juce::AudioProcessor::BusesProperties()
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      processManager(mgr),
      slotId(id),
      pluginDisplayName(name)
{
    startTimer(5000);
}

PluginProxySlot::~PluginProxySlot() {
    // Tear down the child process + release the pipe/shm for this slot.
    // Runs on the message thread (proxies are destroyed during graph rebuild
    // under MainAudioProcessor::graphLock, serialized with the audio callback,
    // so this cannot overlap processBlock on this proxy). killPluginHost
    // TerminateProcess'es the child, closes its handle, stops the pipe, and
    // erases the child entry — freeing the named pipe/shm so a later spawn for
    // any slot id (including a reused one) doesn't collide on a stale orphan.
    // Returns false if there is no child for this slot, which is safe to ignore.
    processManager.killPluginHost(slotId);
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
    static std::atomic<int> s_callCount{ 0 };
    int cc = s_callCount.fetch_add(1, std::memory_order_relaxed);
    if (cc < 3 || (cc % 500) == 0)
        HDAW_LOG("ProxyProc", (juce::String("processBlock call=") + juce::String(cc) + " slot=" + juce::String((int)slotId) + " crashed=" + (crashed.load()?"1":"0") + " renderMode=" + (s_renderMode.load()?"1":"0") + " midi=" + juce::String(midiMessages.getNumEvents())).toStdString());

    if (crashed.load()) return;

    auto* shm = processManager.getShm(slotId);
    if (!shm || !shm->getHeader()) {
        if (cc < 3) HDAW_LOG("ProxyProc", "processBlock: shm null, returning");
        return;
    }

    auto* hdr = shm->getHeader();
    uint32_t cap = hdr->capacity;
    int totalSamples = buffer.getNumChannels() * buffer.getNumSamples();

    uint32_t w = hdr->inputWritePos.load(std::memory_order_relaxed);
    uint32_t r = hdr->inputReadPos.load(std::memory_order_acquire);
    if (static_cast<uint32_t>(totalSamples) > cap - (w - r)) {
        // In render mode, spin-wait for the child to consume input.
        if (s_renderMode.load(std::memory_order_relaxed)) {
            constexpr int kMaxSpinMs = 200;
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kMaxSpinMs);
            while (static_cast<uint32_t>(totalSamples) > cap - (w - r)) {
                if (crashed.load()) return;
                if (std::chrono::steady_clock::now() >= deadline) return;
                std::this_thread::yield();
                w = hdr->inputWritePos.load(std::memory_order_relaxed);
                r = hdr->inputReadPos.load(std::memory_order_acquire);
            }
        } else {
            return;
        }
    }

    float* inRing = shm->getInputRing();
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        for (int s = 0; s < buffer.getNumSamples(); ++s)
            inRing[(w + ch * buffer.getNumSamples() + s) & (cap - 1)] =
                buffer.getSample(ch, s);
    }

    // Write MIDI BEFORE signaling inputWritePos so the host sees both
    // audio + MIDI atomically when it detects new input data.
    MidiEvent* midiIn = shm->getMidiInRing();
    if (midiIn) {
        uint32_t midiCap = 256;
        uint32_t mw = hdr->midiInWritePos.load(std::memory_order_relaxed);
        uint32_t mr = hdr->midiInReadPos.load(std::memory_order_acquire);

        for (const auto metadata : midiMessages) {
            const auto& msg = metadata.getMessage();
            if ((mw - mr) >= midiCap) break;

            MidiEvent& evt = midiIn[mw & 0xFF];
            evt.sampleOffset = static_cast<uint32_t>(metadata.samplePosition);
            const auto* bytes = msg.getRawData();
            evt.data[0] = bytes[0];
            evt.data[1] = bytes[1];
            evt.data[2] = bytes[2];
            evt._pad = 0;
            ++mw;
        }
        hdr->midiInWritePos.store(mw, std::memory_order_release);
    }

    // NOW signal that audio input is available — host won't start processing
    // until this store is visible, and MIDI is already written above.
    hdr->inputWritePos.store(w + static_cast<uint32_t>(totalSamples),
                              std::memory_order_release);

    MidiEvent* midiOut = shm->getMidiOutRing();
    if (midiOut) {
        uint32_t midiCap = 256;
        uint32_t or_mw = hdr->midiOutWritePos.load(std::memory_order_relaxed);
        uint32_t or_mr = hdr->midiOutReadPos.load(std::memory_order_acquire);
        uint32_t avail = (or_mw >= or_mr) ? (or_mw - or_mr) : 0;
        uint32_t toRead = (std::min)(avail, midiCap);

        for (uint32_t i = 0; i < toRead; ++i) {
            const MidiEvent& evt = midiOut[(or_mr + i) & 0xFF];
            midiMessages.addEvent(
                juce::MidiMessage(evt.data[0], evt.data[1], evt.data[2]),
                static_cast<int>(evt.sampleOffset));
        }
        hdr->midiOutReadPos.store(or_mr + toRead, std::memory_order_release);
    }

    uint32_t ow = hdr->outputWritePos.load(std::memory_order_relaxed);
    uint32_t or_ = hdr->outputReadPos.load(std::memory_order_acquire);
    uint32_t available = (ow >= or_) ? (ow - or_) : 0;

    // In render mode, spin-wait for the child process to produce output.
    // The render loop runs at CPU speed with no real-time pacing, so the
    // child (separate OS process) needs explicit time to process each block.
    if (s_renderMode.load(std::memory_order_relaxed) && available < static_cast<uint32_t>(totalSamples)) {
        constexpr int kMaxSpinMs = 200;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kMaxSpinMs);
        while (available < static_cast<uint32_t>(totalSamples)) {
            if (crashed.load()) break;
            if (std::chrono::steady_clock::now() >= deadline) break;
            std::this_thread::yield();
            ow = hdr->outputWritePos.load(std::memory_order_relaxed);
            or_ = hdr->outputReadPos.load(std::memory_order_acquire);
            available = (ow >= or_) ? (ow - or_) : 0;
        }
    }

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
    saveStateToTemp();

    // Show crash dialog on the message thread
    juce::MessageManager::callAsync([this]() {
        proxy::CrashDialog dialog(juce::String(pluginDisplayName).toRawUTF8());
        if (dialog.exec() == QDialog::Accepted && dialog.shouldRestart()) {
            restartAfterCrash();
        }
    });
}

void PluginProxySlot::saveStateToTemp() {
    juce::MemoryBlock block;
    getStateInformation(block);
    if (block.getSize() == 0) return;

    auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    auto file = tempDir.getChildFile("hdaw_proxy_state_" +
        juce::String(static_cast<int>(slotId)) + ".bin");
    file.getParentDirectory().createDirectory();
    file.replaceWithData(block.getData(), block.getSize());
}

bool PluginProxySlot::restartAfterCrash() {
    if (!crashed.load()) return true;

    processManager.killPluginHost(slotId);

    if (!restoreStateFromTemp()) return false;

    crashed.store(false);
    return true;
}

bool PluginProxySlot::restoreStateFromTemp() {
    auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    auto file = tempDir.getChildFile("hdaw_proxy_state_" +
        juce::String(static_cast<int>(slotId)) + ".bin");

    juce::FileInputStream stream(file);
    if (!stream.openedOk()) return false;

    juce::MemoryBlock block(stream.getTotalLength());
    stream.read(block.getData(), block.getSize());

    if (block.getSize() > 0) {
        setStateInformation(block.getData(), static_cast<int>(block.getSize()));
        return true;
    }
    return false;
}

void PluginProxySlot::timerCallback() {
    if (!crashed.load())
        saveStateToTemp();
}

} // namespace proxy
