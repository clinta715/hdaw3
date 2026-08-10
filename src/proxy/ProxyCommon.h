#pragma once
#include <cstdint>
#include <atomic>

namespace proxy {

constexpr uint32_t SHM_MAGIC = 0x4844415B; // bumped 2026-08-09 for transport playhead forwarding (was "HDAZ" for param set/notify SPSC rings)

constexpr uint32_t PARAM_RING_SIZE = 256;

constexpr uint32_t GRACEFUL_EXIT_CODE = 0xC0DE0001;

constexpr uint32_t SYSEX_BUFFER_SIZE = 128 * 1024;

enum class MessageType : uint32_t {
    READY = 0,
    PREPARE,
    PREPARE_RESULT,
    SHUTDOWN,

    PROCESS_BLOCK,

    SET_STATE,
    GET_STATE,
    GET_STATE_RESULT,

    SET_PARAM,
    GET_PARAM,
    GET_PARAM_RESULT,
    GET_PARAM_COUNT,
    GET_PARAM_COUNT_RESULT,
    GET_PARAM_INFO,
    GET_PARAM_INFO_RESULT,

    SHOW_EDITOR,
    CLOSE_EDITOR,
    EDITOR_CLOSED,
    PARAM_CHANGED,

    HEARTBEAT,
    STATE_CHUNK,

    GET_PROGRAM_COUNT,
    GET_PROGRAM_COUNT_RESULT,
    GET_PROGRAM_NAME,
    GET_PROGRAM_NAME_RESULT,
    SET_PROGRAM,
    SET_PROGRAM_RESULT,
    GET_CURRENT_PROGRAM,
    GET_CURRENT_PROGRAM_RESULT,
};

struct alignas(256) ProxyMessage {
    MessageType type;
    uint32_t slotId;
    uint32_t dataSize;
    uint8_t  data[244];
};

struct alignas(256) ProxyResponse {
    MessageType type;
    uint32_t result;
    uint32_t dataSize;
    uint8_t  data[244];
};

struct ShmHeader {
    uint32_t magic;
    uint32_t numChannels;
    uint32_t blockSize;
    uint32_t sampleRate;
    uint32_t capacity;

    std::atomic<uint32_t> inputWritePos{0};
    std::atomic<uint32_t> inputReadPos{0};

    std::atomic<uint32_t> outputWritePos{0};
    std::atomic<uint32_t> outputReadPos{0};

    std::atomic<uint32_t> midiInWritePos{0};
    std::atomic<uint32_t> midiInReadPos{0};

    std::atomic<uint32_t> midiOutWritePos{0};
    std::atomic<uint32_t> midiOutReadPos{0};

    std::atomic<uint32_t> childAlive{0};
    std::atomic<uint32_t> dawAlive{0};

    // Child-side watchdog: incremented once per processed audio block.
    // Parent compares against a saved snapshot; a stall for >staleThresholdMs
    // is treated as a hang only when input is pending (inputWritePos !=
    // inputReadPos) — an idle child with no input is healthy, not hung.
    std::atomic<uint64_t> audioFramesProduced{0};
    std::atomic<uint64_t> audioBlocksProcessed{0};

    // One in-flight SysEx per direction. Writer sets after publishing the
    // event, reader clears after copying the bytes out.
    std::atomic<uint32_t> sysexInBusy{0};
    std::atomic<uint32_t> sysexOutBusy{0};

    // Parent->child param set ring (parent audio thread = single writer).
    // Child->parent param notify ring (child AudioProcessorListener = single
    // writer). Both ring bodies are arrays of std::atomic<uint64_t> laid out
    // after the SysEx buffers in the shm region; only the position atomics
    // live here. Each entry is packed (uint32_t(paramIndex) << 32) | bits-of-float.
    std::atomic<uint32_t> paramSetWritePos{0};
    std::atomic<uint32_t> paramSetReadPos{0};
    std::atomic<uint32_t> paramNotifyWritePos{0};
    std::atomic<uint32_t> paramNotifyReadPos{0};

    // ── Transport clock snapshot (playhead forward). ────────────────────────
    // The parent (PluginProxySlot::processBlock, live audio thread AND export)
    // reads its AudioPlayHead each block and packs the transport state below;
    // the child (hdaw_plugin_host) snapshots it into its own AudioPlayHead.
    // transportRevision is the "new data" signal: the parent release-stores an
    // incremented revision AFTER writing every field, the child acquire-loads
    // it and only copies the fields when the value changed. An unchanged
    // revision means "no new info" — the child keeps its last snapshot (which
    // starts out as a stopped-transport default). Wraps naturally at 2^32;
    // unused wrap is fine over a session. Parent is the single writer, child
    // the single reader — no locks, plain bit-pattern values (no pointers).
    std::atomic<uint32_t> transportRevision{0};
    uint32_t transportPlaying;     // 1 = transport running, 0 = stopped
    uint32_t transportTempoBits;   // IEEE 754 single-precision bits (BPM)
    uint64_t transportSecondsBits; // IEEE 754 double bits (time in seconds)
    uint64_t transportPpqBits;     // IEEE 754 double bits (PPQ position)
    uint32_t transportTsigNum;     // time-signature numerator (default 4)
    uint32_t transportTsigDenom;   // time-signature denominator (default 4)

    // The hosted plugin's summed channel layout, written ONCE by the child
    // right after load (0 = not yet known). The parent proxy uses these to
    // size PREPARE and its reported bus width, so multi-port plugins (e.g.
    // the 4-out Nord-2x port) get their full channel count in the child.
    uint32_t pluginNumInputChannels;
    uint32_t pluginNumOutputChannels;
};

struct MidiEvent {
    uint32_t sampleOffset;
    uint8_t  data[3];
    // Bit 7 set: SysEx reference (sysexLen valid, bytes live in the
    // direction's SysEx buffer). Else low bits = inline byte count (1-3).
    uint8_t  flags;
    uint32_t sysexLen;
};

inline uint32_t computeShmSize(uint32_t numChannels, uint32_t blockSize) {
    uint32_t cap = 1;
    while (cap < blockSize * numChannels) cap <<= 1;

    uint32_t headerSize = static_cast<uint32_t>(sizeof(ShmHeader));
    uint32_t inputRing  = cap * sizeof(float);
    uint32_t outputRing = cap * sizeof(float);
    uint32_t midiInRing  = 256 * sizeof(MidiEvent);
    uint32_t midiOutRing = 256 * sizeof(MidiEvent);

    return headerSize + inputRing + outputRing + midiInRing + midiOutRing
         + 2 * SYSEX_BUFFER_SIZE
         + 2 * PARAM_RING_SIZE * sizeof(std::atomic<uint64_t>);
}

// The shared-memory mapping is created ONCE at spawn for the worst-case
// audio config (multi-channel plugins like the 4-out Nord-2x port × the
// largest device block size), so both parent and child can safely let
// hdr->capacity float up to this at PREPARE. Indexing past the mapping
// would be a cross-process OOB write.
constexpr uint32_t kMaxShmChannels = 8;
constexpr uint32_t kMaxShmBlockSize = 4096;
constexpr uint32_t kMaxShmCapacitySamples = 32768; // pow2 >= 8 * 4096

} // namespace proxy
