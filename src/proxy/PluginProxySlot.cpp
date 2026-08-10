#include "PluginProxySlot.h"
#include "ProxyEditor.h"
#include "CrashDialog.h"
#include "../common/DebugLog.h"
#include <algorithm>
#include <cstring>
#include <chrono>
#include <thread>
#include <vector>

namespace proxy {

namespace {
constexpr size_t kStateChunkSize = sizeof(ProxyMessage::data);
constexpr size_t kMaxStateBytes = size_t{512} * 1024 * 1024;

// SysEx drops are rare; log the first and every 256th so a stuck lane
// can't spam the log from the audio thread.
void logSysexDrop(const char* reason) {
    static std::atomic<uint32_t> count{0};
    if ((count.fetch_add(1, std::memory_order_relaxed) & 0xFFu) == 0)
        HDAW_LOG("proxy", reason);
}
} // namespace

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
    fetchParamMetadata();
    startTimer(100);
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
    // Notify the registry LAST, after all shm/process resources are released,
    // so the caller (PluginManager) can safely erase this slot and cancel any
    // pending respawn — a respawn that would otherwise dereference `this`
    // after destruction completes.
    if (slotDestroyedFn) slotDestroyedFn(slotId);
}

void PluginProxySlot::prepareToPlay(double sampleRate, int samplesPerBlock) {
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;

    auto* pipe = processManager.getPipe(slotId);
    if (!pipe) return;

    // The child wrote the hosted plugin's channel layout into the shm header
    // at load. Use it for the PREPARE width so multi-port plugins (4-out
    // Nord-2x port) get their full channel count in the child.
    if (shmHandle)
    {
        if (auto* hdr = shmHandle->getHeader())
        {
            reportedNumInputs = static_cast<int>(hdr->pluginNumInputChannels);
            reportedNumOutputs = static_cast<int>(hdr->pluginNumOutputChannels);
            if (reportedNumOutputs > 0 && reportedNumOutputs > numChannels)
                numChannels = reportedNumOutputs;
        }
    }

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

// ---------------------------------------------------------------------------
// ProxiedParameter — getValue/setValue/stageParam.
float ProxiedParameter::getValue() const { return loadCache(); }
void ProxiedParameter::setValue(float newValue) {
    setCache(newValue);
    ownerSlot.stageParam(index, newValue);
}
juce::String ProxiedParameter::getName(int maxLen) const {
    if (maxLen > 0 && nameStr.length() > maxLen)
        return nameStr.substring(0, maxLen);
    return nameStr;
}

// ---------------------------------------------------------------------------
// stageParam — message OR audio thread writes the parent-local staging slot
// and marks it dirty; processBlock (audio thread, single writer) flushes it
// into the shm paramSet ring.
void PluginProxySlot::stageParam(uint32_t index, float value) {
    if (index >= paramCacheSize_ || !stagedParams_) return;
    stagedParams_[index].store(value, std::memory_order_relaxed);
    paramDirty_[index].store(1, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// fetchParamMetadata — runs on the message thread at construction. The child's
// controlLoop starts AFTER loadPlugin(), so a bounded round-trip waits for the
// plugin to be loaded. Any failure (null pipe, timeout, OOB) early-returns
// leaving 0 params — never hangs.
void PluginProxySlot::fetchParamMetadata() {
    auto* pipe = processManager.getPipe(slotId);
    if (!pipe) return;

    static constexpr DWORD kMetaTimeoutMs = 3000;

    ProxyMessage countMsg{};
    countMsg.type = MessageType::GET_PARAM_COUNT;
    countMsg.slotId = slotId;
    if (!pipe->sendMsgBounded(countMsg, kMetaTimeoutMs)) return;
    ProxyResponse countResp{};
    if (!pipe->receiveRespBounded(countResp, kMetaTimeoutMs)) return;
    if (countResp.type != MessageType::GET_PARAM_COUNT_RESULT || countResp.result != 1)
        return;
    if (countResp.dataSize < sizeof(uint32_t)) return;
    uint32_t n = 0;
    std::memcpy(&n, countResp.data, sizeof(uint32_t));
    if (n == 0 || n > 4096) return;

    stagedParams_ = std::unique_ptr<std::atomic<float>[]>(new std::atomic<float>[n]);
    paramDirty_ = std::unique_ptr<std::atomic<uint32_t>[]>(new std::atomic<uint32_t>[n]);
    for (uint32_t i = 0; i < n; ++i) {
        stagedParams_[i].store(0.f, std::memory_order_relaxed);
        paramDirty_[i].store(0, std::memory_order_relaxed);
    }
    paramCacheSize_ = n;

    for (uint32_t i = 0; i < n; ++i) {
        ProxyMessage infoMsg{};
        infoMsg.type = MessageType::GET_PARAM_INFO;
        infoMsg.slotId = slotId;
        std::memcpy(infoMsg.data, &i, sizeof(uint32_t));
        infoMsg.dataSize = sizeof(uint32_t);
        if (!pipe->sendMsgBounded(infoMsg, kMetaTimeoutMs)) { paramCacheSize_ = 0; return; }
        ProxyResponse infoResp{};
        if (!pipe->receiveRespBounded(infoResp, kMetaTimeoutMs)) { paramCacheSize_ = 0; return; }
        if (infoResp.type != MessageType::GET_PARAM_INFO_RESULT || infoResp.result != 1) { paramCacheSize_ = 0; return; }

        float defaultValue = 0.f;
        uint8_t automatable = 0u;
        uint32_t nameLen = 0;
        uint32_t headerBytes = sizeof(float) + sizeof(uint8_t) + sizeof(uint32_t);
        if (infoResp.dataSize < headerBytes) { paramCacheSize_ = 0; return; }
        uint32_t off = 0;
        std::memcpy(&defaultValue, infoResp.data + off, sizeof(float)); off += sizeof(float);
        std::memcpy(&automatable, infoResp.data + off, sizeof(uint8_t)); off += sizeof(uint8_t);
        std::memcpy(&nameLen, infoResp.data + off, sizeof(uint32_t)); off += sizeof(uint32_t);

        std::vector<char> nameBuf;
        nameBuf.reserve(nameLen + 1);
        uint32_t inFirst = std::min(nameLen, static_cast<uint32_t>(sizeof(infoResp.data)) - off);
        nameBuf.insert(nameBuf.end(), reinterpret_cast<const char*>(infoResp.data + off),
                       reinterpret_cast<const char*>(infoResp.data + off) + inFirst);
        uint32_t got = inFirst;
        while (got < nameLen) {
            ProxyResponse chunk{};
            if (!pipe->receiveRespBounded(chunk, kMetaTimeoutMs)) { paramCacheSize_ = 0; return; }
            if (chunk.type != MessageType::STATE_CHUNK) { paramCacheSize_ = 0; return; }
            uint32_t take = std::min<uint32_t>(chunk.dataSize, nameLen - got);
            take = std::min<uint32_t>(take, sizeof(chunk.data));
            nameBuf.insert(nameBuf.end(),
                           reinterpret_cast<const char*>(chunk.data),
                           reinterpret_cast<const char*>(chunk.data) + take);
            got += take;
        }
        nameBuf.push_back('\0');
        juce::String name = juce::String::fromUTF8(nameBuf.data());

        ProxyMessage getMsg{};
        getMsg.type = MessageType::GET_PARAM;
        getMsg.slotId = slotId;
        std::memcpy(getMsg.data, &i, sizeof(uint32_t));
        getMsg.dataSize = sizeof(uint32_t);
        float value = defaultValue;
        if (pipe->sendMsgBounded(getMsg, kMetaTimeoutMs)) {
            ProxyResponse getResp{};
            if (pipe->receiveRespBounded(getResp, kMetaTimeoutMs)
                && getResp.type == MessageType::GET_PARAM_RESULT
                && getResp.result == 1
                && getResp.dataSize >= sizeof(float)) {
                std::memcpy(&value, getResp.data, sizeof(float));
            }
        }

        auto* p = new ProxiedParameter(i, name, defaultValue, automatable != 0, *this);
        p->setCache(value);
        addHostedParameter(std::unique_ptr<HostedParameter>(p));
    }

    ProxyMessage progMsg{};
    progMsg.type = MessageType::GET_PROGRAM_COUNT;
    progMsg.slotId = slotId;
    if (pipe->sendMsgBounded(progMsg, kMetaTimeoutMs)) {
        ProxyResponse progResp{};
        if (pipe->receiveRespBounded(progResp, kMetaTimeoutMs)
            && progResp.type == MessageType::GET_PROGRAM_COUNT_RESULT
            && progResp.result == 1
            && progResp.dataSize >= sizeof(uint32_t)) {
            std::memcpy(&numProgramsCached_, progResp.data, sizeof(uint32_t));
        }
    }
}

int PluginProxySlot::getCurrentProgram() {
    if (crashed.load()) return 0;
    auto* pipe = processManager.getPipe(slotId);
    if (!pipe) return 0;
    ProxyMessage msg{};
    msg.type = MessageType::GET_CURRENT_PROGRAM;
    msg.slotId = slotId;
    static constexpr DWORD kT = 3000;
    if (!pipe->sendMsgBounded(msg, kT)) return 0;
    ProxyResponse resp{};
    if (!pipe->receiveRespBounded(resp, kT)) return 0;
    if (resp.type != MessageType::GET_CURRENT_PROGRAM_RESULT || resp.result != 1) return 0;
    if (resp.dataSize < sizeof(uint32_t)) return 0;
    uint32_t cur = 0;
    std::memcpy(&cur, resp.data, sizeof(uint32_t));
    return static_cast<int>(cur);
}

void PluginProxySlot::setCurrentProgram(int index) {
    if (crashed.load()) return;
    auto* pipe = processManager.getPipe(slotId);
    if (!pipe) return;
    ProxyMessage msg{};
    msg.type = MessageType::SET_PROGRAM;
    msg.slotId = slotId;
    uint32_t idx = static_cast<uint32_t>(index);
    std::memcpy(msg.data, &idx, sizeof(uint32_t));
    msg.dataSize = sizeof(uint32_t);
    static constexpr DWORD kT = 3000;
    pipe->sendMsgBounded(msg, kT);
    ProxyResponse resp{};
    pipe->receiveRespBounded(resp, kT);
}

const juce::String PluginProxySlot::getProgramName(int index) {
    if (crashed.load()) return {};
    auto* pipe = processManager.getPipe(slotId);
    if (!pipe) return {};
    ProxyMessage msg{};
    msg.type = MessageType::GET_PROGRAM_NAME;
    msg.slotId = slotId;
    uint32_t idx = static_cast<uint32_t>(index);
    std::memcpy(msg.data, &idx, sizeof(uint32_t));
    msg.dataSize = sizeof(uint32_t);
    static constexpr DWORD kT = 3000;
    if (!pipe->sendMsgBounded(msg, kT)) return {};
    ProxyResponse resp{};
    if (!pipe->receiveRespBounded(resp, kT)) return {};
    if (resp.type != MessageType::GET_PROGRAM_NAME_RESULT || resp.result != 1) return {};
    uint32_t total = resp.dataSize;
    std::vector<char> buf;
    buf.reserve(total + 1);
    uint32_t first = std::min<uint32_t>(total, sizeof(resp.data));
    buf.insert(buf.end(), reinterpret_cast<const char*>(resp.data),
               reinterpret_cast<const char*>(resp.data) + first);
    uint32_t got = first;
    while (got < total) {
        ProxyResponse chunk{};
        if (!pipe->receiveRespBounded(chunk, kT)) return {};
        if (chunk.type != MessageType::STATE_CHUNK) return {};
        uint32_t take = std::min<uint32_t>(chunk.dataSize, total - got);
        take = std::min<uint32_t>(take, sizeof(chunk.data));
        buf.insert(buf.end(), reinterpret_cast<const char*>(chunk.data),
                   reinterpret_cast<const char*>(chunk.data) + take);
        got += take;
    }
    buf.push_back('\0');
    return juce::String::fromUTF8(buf.data());
}

// ---------------------------------------------------------------------------
// drainParamNotifications — message thread. Pops the local notification queue
// (filled by processBlock from the paramNotify shm ring) and forwards each to
// the AudioProcessor listeners of the matching ProxiedParameter.
void PluginProxySlot::drainParamNotifications() {
    uint32_t r = notifyQRead.load(std::memory_order_relaxed);
    uint32_t w = notifyQWrite.load(std::memory_order_acquire);
    auto& params = getParameters();
    uint32_t consumed = 0;
    while (r != w && consumed < kNotifyQueueCap) {
        const NotifyEntry& e = notifyQueue_[r & (kNotifyQueueCap - 1)];
        if (e.index >= 0 && e.index < params.size()
            && params[e.index] != nullptr) {
            auto* p = static_cast<ProxiedParameter*>(params[e.index]);
            p->setCache(e.value);
            p->sendValueChangedMessageToListeners(e.value);
        }
        ++r;
        ++consumed;
    }
    notifyQRead.store(r, std::memory_order_release);
}
void PluginProxySlot::processBlock(juce::AudioBuffer<float>& buffer,
                                    juce::MidiBuffer& midiMessages) {

    if (crashed.load())
        return;

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

    // Transport clock forward: snapshot the engine playhead into the shm
    // header (lock-free, allocation-free — audio thread + export render).
    // Fields are written first, revision release-stored last so the child
    // sees a consistent snapshot. No playhead / no position → revision
    // unchanged → child interprets it as "no new info" and keeps its last
    // snapshot (a stopped-transport default if never populated).
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            const float tempo = pos->getBpm().orFallback(120.0);
            const double seconds = pos->getTimeInSeconds().orFallback(0.0);
            const double ppq = pos->getPpqPosition().orFallback(0.0);
            uint32_t tempoBits = 0;
            uint64_t secondsBits = 0, ppqBits = 0;
            std::memcpy(&tempoBits, &tempo, sizeof(tempo));
            std::memcpy(&secondsBits, &seconds, sizeof(seconds));
            std::memcpy(&ppqBits, &ppq, sizeof(ppq));
            hdr->transportPlaying = pos->getIsPlaying() ? 1u : 0u;
            hdr->transportTempoBits = tempoBits;
            hdr->transportSecondsBits = secondsBits;
            hdr->transportPpqBits = ppqBits;
            hdr->transportTsigNum = 4;
            hdr->transportTsigDenom = 4;
            hdr->transportRevision.store(
                hdr->transportRevision.load(std::memory_order_relaxed) + 1,
                std::memory_order_release);
        }
    }

    int totalSamples = buffer.getNumChannels() * buffer.getNumSamples();

    uint32_t w = hdr->inputWritePos.load(std::memory_order_relaxed);
    uint32_t r = hdr->inputReadPos.load(std::memory_order_acquire);
    if (static_cast<uint32_t>(totalSamples) > cap - (w - r)) {
        // In render mode, spin-wait for the child to consume input.
        if (isRenderMode()) {
            constexpr int kMaxSpinMs = 200;
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kMaxSpinMs);
            while (static_cast<uint32_t>(totalSamples) > cap - (w - r)) {
                if (crashed.load()) return;
                if (isRenderCancelRequested()) return;
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
        uint8_t* sysexBuf = shm->getSysexInBuffer();

        for (const auto metadata : midiMessages) {
            const auto& msg = metadata.getMessage();
            if ((mw - mr) >= midiCap) break;

            MidiEvent& evt = midiIn[mw & 0xFF];
            evt.sampleOffset = static_cast<uint32_t>(metadata.samplePosition);

            if (msg.isSysEx()) {
                const size_t len = static_cast<size_t>(msg.getRawDataSize());
                if (hdr->sysexInBusy.load(std::memory_order_acquire) != 0) {
                    logSysexDrop("midiIn SysEx dropped: lane busy");
                    continue;
                }
                if (sysexBuf == nullptr || len > SYSEX_BUFFER_SIZE) {
                    logSysexDrop("midiIn SysEx dropped: exceeds 128KB buffer");
                    continue;
                }
                std::memcpy(sysexBuf, msg.getRawData(), len);
                evt.flags = 0x80;
                evt.sysexLen = static_cast<uint32_t>(len);
                // Becomes visible to the child with the midiInWritePos
                // release store below.
                hdr->sysexInBusy.store(1, std::memory_order_release);
            } else {
                uint32_t n = static_cast<uint32_t>(msg.getRawDataSize());
                if (n < 1) n = 1;
                if (n > 3) n = 3;
                const auto* bytes = msg.getRawData();
                for (uint32_t i = 0; i < n; ++i)
                    evt.data[i] = bytes[i];
                evt.flags = static_cast<uint8_t>(n);
            }
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
        const uint8_t* sysexBuf = shm->getSysexOutBuffer();

        for (uint32_t i = 0; i < toRead; ++i) {
            const MidiEvent& evt = midiOut[(or_mr + i) & 0xFF];
            if ((evt.flags & 0x80u) != 0) {
                if (sysexBuf != nullptr && evt.sysexLen > 0
                    && evt.sysexLen <= SYSEX_BUFFER_SIZE) {
                    // Reconstructing the received SysEx allocates — the one
                    // accepted heap allocation on the audio path (rare event).
                    midiMessages.addEvent(
                        juce::MidiMessage(sysexBuf, static_cast<int>(evt.sysexLen)),
                        static_cast<int>(evt.sampleOffset));
                }
                hdr->sysexOutBusy.store(0, std::memory_order_release);
            } else {
                int n = static_cast<int>(evt.flags & 0x7Fu);
                if (n < 1) n = 1;
                if (n > 3) n = 3;
                midiMessages.addEvent(
                    juce::MidiMessage(evt.data, n),
                    static_cast<int>(evt.sampleOffset));
            }
        }
        hdr->midiOutReadPos.store(or_mr + toRead, std::memory_order_release);
    }

    // Param bridge — single audio-thread writer. Flush parent-local staged
    // params into the shm paramSet ring; if the ring is full leave the dirty
    // flag set for the next block.
    if (paramCacheSize_ > 0 && stagedParams_ && paramDirty_) {
        auto* setRing = shm->getParamSetRing();
        if (setRing) {
            uint32_t sw = hdr->paramSetWritePos.load(std::memory_order_relaxed);
            uint32_t sr = hdr->paramSetReadPos.load(std::memory_order_acquire);
            for (uint32_t i = 0; i < paramCacheSize_; ++i) {
                if (!paramDirty_[i].load(std::memory_order_relaxed)) continue;
                if (sw - sr >= PARAM_RING_SIZE) break;
                float v = stagedParams_[i].load(std::memory_order_relaxed);
                uint64_t packed = (uint64_t(i) << 32)
                                  | uint64_t(*reinterpret_cast<const uint32_t*>(&v));
                setRing[sw & (PARAM_RING_SIZE - 1)].store(packed, std::memory_order_relaxed);
                ++sw;
                paramDirty_[i].store(0, std::memory_order_relaxed);
            }
            hdr->paramSetWritePos.store(sw, std::memory_order_release);
        }
    }

    // Param bridge — drain child->parent notify ring into the parent-local
    // bounded queue (consumed by drainParamNotifications on the message thread).
    if (auto* notifyRing = shm->getParamNotifyRing()) {
        uint32_t nw = hdr->paramNotifyWritePos.load(std::memory_order_acquire);
        uint32_t nr = hdr->paramNotifyReadPos.load(std::memory_order_relaxed);
        uint32_t qW = notifyQWrite.load(std::memory_order_relaxed);
        uint32_t qR = notifyQRead.load(std::memory_order_acquire);
        while (nr != nw) {
            if (qW - qR >= kNotifyQueueCap) break;
            uint64_t packed = notifyRing[nr & (PARAM_RING_SIZE - 1)]
                                  .load(std::memory_order_relaxed);
            int idx = static_cast<int>(static_cast<uint32_t>(packed >> 32));
            uint32_t bits = static_cast<uint32_t>(packed & 0xFFFFFFFFull);
            float v;
            std::memcpy(&v, &bits, sizeof(float));
            notifyQueue_[qW & (kNotifyQueueCap - 1)] = { idx, v };
            ++qW;
            ++nr;
        }
        notifyQWrite.store(qW, std::memory_order_release);
        hdr->paramNotifyReadPos.store(nr, std::memory_order_release);
    }

    uint32_t ow = hdr->outputWritePos.load(std::memory_order_relaxed);
    uint32_t or_ = hdr->outputReadPos.load(std::memory_order_acquire);
    uint32_t available = (ow >= or_) ? (ow - or_) : 0;

    // In render mode, spin-wait for the child process to produce output.
    // The render loop runs at CPU speed with no real-time pacing, so the
    // child (separate OS process) needs explicit time to process each block.
    if (isRenderMode() && available < static_cast<uint32_t>(totalSamples)) {
        constexpr int kMaxSpinMs = 200;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kMaxSpinMs);
        while (available < static_cast<uint32_t>(totalSamples)) {
            if (crashed.load()) break;
            if (isRenderCancelRequested()) break;
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
    if (!pipe->receiveRespBounded(resp, kStateTimeoutMs)) return;
    if (resp.type != MessageType::GET_STATE_RESULT || resp.result != 1) return;

    // dataSize carries the TOTAL state size; resp.data holds the first chunk
    // and the remainder arrives as STATE_CHUNK responses.
    const size_t total = resp.dataSize;
    if (total == 0) return;
    if (total > kMaxStateBytes) return;

    destData.setSize(0, false);
    const size_t first = std::min(total, sizeof(resp.data));
    destData.append(resp.data, first);

    while (destData.getSize() < total) {
        ProxyResponse chunk{};
        if (!pipe->receiveRespBounded(chunk, kStateTimeoutMs)) return;
        if (chunk.type != MessageType::STATE_CHUNK) return;
        const size_t take = std::min<size_t>(chunk.dataSize, sizeof(chunk.data));
        destData.append(chunk.data, take);
    }
}

void PluginProxySlot::setStateInformation(const void* data, int sizeInBytes) {
    auto* pipe = processManager.getPipe(slotId);
    if (!pipe || !data || sizeInBytes <= 0) return;

    static constexpr DWORD kStateTimeoutMs = 3000;

    const auto total = static_cast<size_t>(sizeInBytes);
    const auto* bytes = static_cast<const uint8_t*>(data);

    // dataSize carries the TOTAL state size; data holds the first chunk and
    // the remainder is sent as STATE_CHUNK messages before the single response.
    ProxyMessage msg{};
    msg.type = MessageType::SET_STATE;
    msg.slotId = slotId;
    msg.dataSize = static_cast<uint32_t>(total);
    const size_t first = std::min(total, kStateChunkSize);
    std::memcpy(msg.data, bytes, first);
    if (!pipe->sendMsgBounded(msg, kStateTimeoutMs)) return;

    size_t offset = first;
    while (offset < total) {
        ProxyMessage chunk{};
        chunk.type = MessageType::STATE_CHUNK;
        chunk.slotId = slotId;
        const size_t take = std::min(total - offset, kStateChunkSize);
        chunk.dataSize = static_cast<uint32_t>(take);
        std::memcpy(chunk.data, bytes + offset, take);
        if (!pipe->sendMsgBounded(chunk, kStateTimeoutMs)) return;
        offset += take;
    }

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
    desc.numInputChannels = getReportedNumInputChannels();
    desc.numOutputChannels = getReportedNumOutputChannels();
}

int PluginProxySlot::getReportedNumInputChannels() const {
    return reportedNumInputs > 0 ? reportedNumInputs : 2;
}

int PluginProxySlot::getReportedNumOutputChannels() const {
    return reportedNumOutputs > 0 ? reportedNumOutputs : 2;
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
    // The param set/notify rings live inside the shm region body, so they are
    // carried automatically by the shmHandle swap below — no extra wiring.
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
    drainParamNotifications();
    static uint32_t tick = 0;
    if ((++tick % 50) == 0 && !crashed.load())
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
