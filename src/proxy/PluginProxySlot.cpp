#include "PluginProxySlot.h"
#include "ProxyEditor.h"
#include "CrashDialog.h"
#include "../common/DebugLog.h"
#include <cstring>
#include <chrono>
#include <thread>

namespace proxy {

PluginProxySlot::PluginProxySlot(ProxyProcessManager& mgr, uint32_t id,
                                    const juce::String& name,
                                    const juce::String& pluginPath)
    : AudioPluginInstance(juce::AudioProcessor::BusesProperties()
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      processManager(mgr),
      slotId(id),
      pluginDisplayName(name),
      pluginPathForRecovery(pluginPath)
{
    auto* raw = processManager.getShm(slotId);
    if (raw)
        shmHandle = std::shared_ptr<ShmRegion>(raw, [](ShmRegion*){});
    childAlive.store(processManager.isChildAlive(slotId), std::memory_order_relaxed);
    startTimer(5000);
}

PluginProxySlot::~PluginProxySlot() {
    // Runs on the message thread after the current audio callback completes
    // (graph rebuild serialized via graphLock). Full cleanup is safe here.
    processManager.removeSlotCrashCallback(slotId);
    if (editorWatcherThread.joinable())
        editorWatcherThread.detach();
    processManager.killPluginHost(slotId, KillMode::KillGraceful);
    releaseResources();
    shmHandle.reset();
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

    // Use bounded IPC with a 5-second timeout. If the child is dead or hung,
    // this prevents blocking the message thread (which holds graphLock) and
    // avoids starving the audio callback.
    static constexpr DWORD kPrepareTimeoutMs = 5000;
    pipe->sendMsgBounded(msg, kPrepareTimeoutMs);
    ProxyResponse resp{};
    pipe->receiveRespBounded(resp, kPrepareTimeoutMs);
}

void PluginProxySlot::releaseResources() {
}

void PluginProxySlot::processBlock(juce::AudioBuffer<float>& buffer,
                                    juce::MidiBuffer& midiMessages) {

    if (crashed.load()) return;

    // Lock-free check: if the child has been terminated (e.g. by the crash
    // handler), don't access the shm — it may be about to be destroyed.
    if (!childAlive.load(std::memory_order_relaxed)) {
        buffer.clear();
        return;
    }

    // Use cached pointer instead of getShm() (which takes a mutex — forbidden
    // on the audio thread). The pointer is valid for the proxy's lifetime:
    // killPluginHost(fullCleanup=false) keeps the ShmRegion alive in the map.
    auto shm = shmHandle;
    if (!shm || !shm->getHeader()) {
        buffer.clear();
        return;
    }

    auto* hdr = shm->getHeader();
    uint32_t cap = hdr->capacity;
    if (cap == 0) {
        buffer.clear();
        return;
    }
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

    static constexpr DWORD kStateTimeoutMs = 3000;
    if (!pipe->sendMsgBounded(msg, kStateTimeoutMs)) return;

    ProxyResponse resp{};
    if (pipe->receiveRespBounded(resp, kStateTimeoutMs) && resp.result == 1 && resp.dataSize > 0) {
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

    static constexpr DWORD kStateTimeoutMs = 3000;
    pipe->sendMsgBounded(msg, kStateTimeoutMs);

    ProxyResponse resp{};
    pipe->receiveRespBounded(resp, kStateTimeoutMs);
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
    childAlive.store(false, std::memory_order_relaxed);
    saveStateToTemp();

    if (crashRecoveryNotifier)
        crashRecoveryNotifier(slotId, pluginDisplayName, pluginPathForRecovery);

    juce::MessageManager::callAsync([this]() {
        proxy::CrashDialog dialog(
            juce::String(pluginDisplayName).toRawUTF8(),
            [this]() { requestRespawn(); });
        dialog.exec();
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
    requestRespawn();
    return true;
}

void PluginProxySlot::migrateToNewSlot(uint32_t newSlotId, std::shared_ptr<ShmRegion> newShm) {
    slotId = newSlotId;
    shmHandle = std::move(newShm);
    crashed.store(false);
    childAlive.store(true);
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

void PluginProxySlot::waitForEditorClosed() {
    auto* pipe = processManager.getPipe(slotId);
    if (!pipe) return;
    proxy::ProxyResponse resp{};
    while (childAlive.load(std::memory_order_relaxed)) {
        if (pipe->receiveRespBounded(resp, 500)) {
            if (resp.type == proxy::MessageType::EDITOR_CLOSED) {
                if (editorClosedCb) editorClosedCb();
                return;
            }
        }
    }
}

void PluginProxySlot::startEditorWatcher() {
    if (editorWatcherThread.joinable()) return;
    editorWatcherThread = std::thread([this]{ waitForEditorClosed(); });
}

void PluginProxySlot::timerCallback() {
    if (!crashed.load())
        saveStateToTemp();
}

juce::MemoryBlock PluginProxySlot::loadStateForOldSlotId(uint32_t oldSlotId) {
    auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    auto file = tempDir.getChildFile("hdaw_proxy_state_" + juce::String((int)oldSlotId) + ".bin");
    juce::MemoryBlock block;
    if (file.existsAsFile()) {
        juce::FileInputStream stream(file);
        if (stream.openedOk())
            stream.readIntoMemoryBlock(block);
    }
    return block;
}

void PluginProxySlot::clearStateForSlotId(uint32_t slotId) {
    auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    tempDir.getChildFile("hdaw_proxy_state_" + juce::String((int)slotId) + ".bin").deleteFile();
}

} // namespace proxy
