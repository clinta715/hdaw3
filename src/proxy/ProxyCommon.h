#pragma once
#include <cstdint>
#include <atomic>

namespace proxy {

constexpr uint32_t SHM_MAGIC = 0x48444157; // "HDAW"

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
};

struct MidiEvent {
    uint32_t sampleOffset;
    uint8_t  data[3];
    uint8_t  _pad;
};

inline uint32_t computeShmSize(uint32_t numChannels, uint32_t blockSize) {
    uint32_t cap = 1;
    while (cap < blockSize * numChannels) cap <<= 1;

    uint32_t headerSize = static_cast<uint32_t>(sizeof(ShmHeader));
    uint32_t inputRing  = cap * sizeof(float);
    uint32_t outputRing = cap * sizeof(float);
    uint32_t midiInRing  = 256 * sizeof(MidiEvent);
    uint32_t midiOutRing = 256 * sizeof(MidiEvent);

    return headerSize + inputRing + outputRing + midiInRing + midiOutRing;
}

} // namespace proxy
