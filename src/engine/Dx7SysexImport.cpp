#include "Dx7SysexImport.h"
#include <cstring>

namespace HDAW {

static bool verifyChecksum(const uint8_t* data, size_t len) {
    int sum = 0;
    for (size_t i = 0; i < len; ++i)
        sum += data[i];
    return (sum & 0x7F) == 0;
}

std::optional<Dx7Voice> parseSingleVoiceSysex(const uint8_t* data, size_t size) {
    if (size < 6 || data[0] != 0xF0 || data[1] != 0x43)
        return std::nullopt;

    if (data[3] != 0x00)
        return std::nullopt;

    size_t dataLen = (static_cast<size_t>(data[4]) << 7) | data[5];
    if (dataLen != 155)
        return std::nullopt;

    size_t totalExpected = 6 + dataLen + 1;
    if (size < totalExpected)
        return std::nullopt;

    if (!verifyChecksum(data + 6, dataLen + 1))
        return std::nullopt;

    Dx7Voice voice;
    std::memcpy(voice.patchData.data(), data + 6, 155);
    voice.patchData[155] = 0x3F;

    char nameBuf[11] = {};
    std::memcpy(nameBuf, data + 6 + 145, 10);
    voice.voiceName = std::string(nameBuf, 10);
    auto pos = voice.voiceName.find_last_not_of(' ');
    if (pos != std::string::npos)
        voice.voiceName.erase(pos + 1);

    voice.algorithm = data[6 + 134] & 0x1F;
    voice.feedback = data[6 + 135] & 0x07;

    return voice;
}

void unpackVmemVoice(const uint8_t packed[128], uint8_t unpacked[156]) {
    std::memset(unpacked, 0, 156);

    for (int op = 0; op < 6; ++op) {
        const uint8_t* p = packed + op * 17;
        uint8_t* u = unpacked + op * 21;

        for (int i = 0; i < 8; ++i)
            u[i] = p[i];

        u[8]  = p[8];
        u[9]  = p[9];
        u[10] = p[10];

        // VMEM packed layout (Dexed Documentation/sysex-format.txt):
        // p11: LC bits0-1, RC bits2-3 | p12: RS bits0-2, DET bits3-6 |
        // p13: AMS bits0-1, KVS bits2-4 | p14: OUTPUT LEVEL (0-99) |
        // p15: M bit0, FC bits1-5 | p16: FREQ FINE (0-99)
        u[11] = p[11] & 0x03;               // left curve
        u[12] = (p[11] >> 2) & 0x03;        // right curve

        u[13] = p[12] & 0x07;               // rate scale
        u[14] = p[13] & 0x03;               // amp mod sens
        u[15] = (p[13] >> 2) & 0x07;        // key vel sens
        u[16] = p[14];                      // output level (0-99)

        u[17] = p[15] & 0x01;               // osc mode
        u[18] = (p[15] >> 1) & 0x1F;        // freq coarse
        u[19] = p[16];                      // freq fine (0-99)
        u[20] = (p[12] >> 3) & 0x0F;        // detune
    }

    for (int i = 0; i < 8; ++i)
        unpacked[126 + i] = packed[102 + i];

    unpacked[134] = packed[110] & 0x1F;

    unpacked[135] = (packed[111] >> 1) & 0x07;
    unpacked[136] = packed[111] & 0x01;

    for (int i = 0; i < 4; ++i)
        unpacked[137 + i] = packed[112 + i];

    unpacked[141] = packed[116] & 0x01;
    unpacked[142] = (packed[116] >> 1) & 0x07;
    unpacked[143] = (packed[116] >> 4) & 0x07;

    unpacked[144] = packed[117];

    for (int i = 0; i < 10; ++i)
        unpacked[145 + i] = packed[118 + i];

    unpacked[155] = 0x3F;
}

// Unpack a full 32-voice bank from its 4096 packed bytes (128 bytes/voice),
// setting the name (VCED 145-154, trimmed), algorithm and feedback. Shared by
// the framed-cartridge path (payload = data + 6) and the raw 4096-byte VMEM
// bank path (payload = data).
static std::vector<Dx7Voice> unpackBank(const uint8_t* data) {
    std::vector<Dx7Voice> voices;
    voices.reserve(32);

    for (int v = 0; v < 32; ++v) {
        Dx7Voice voice;
        unpackVmemVoice(data + v * 128, voice.patchData.data());

        char nameBuf[11] = {};
        std::memcpy(nameBuf, voice.patchData.data() + 145, 10);
        voice.voiceName = std::string(nameBuf, 10);
        auto pos = voice.voiceName.find_last_not_of(' ');
        if (pos != std::string::npos)
            voice.voiceName.erase(pos + 1);

        voice.algorithm = voice.patchData[134] & 0x1F;
        voice.feedback = voice.patchData[135] & 0x07;

        voices.push_back(std::move(voice));
    }

    return voices;
}

std::vector<Dx7Voice> parseCartridgeSysex(const uint8_t* data, size_t size) {
    // Raw 4096-byte VMEM bank: exactly 32 voices x 128 packed bytes, with NO
    // F0 43 sysex framing, no checksum, no trailing F7. Some raw banks carry
    // a trailing F7 (4097 bytes) that is not part of the payload. Detect by
    // size + absence of the framing header; the payload starts at byte 0.
    if (size >= 4096 && size <= 4097 &&
        !(data[0] == 0xF0 && data[1] == 0x43) &&
        (size == 4096 || (size == 4097 && data[4096] == 0xF7)))
        return unpackBank(data);

    if (size < 4104 || data[0] != 0xF0 || data[1] != 0x43)
        return {};

    if (data[3] != 0x09)
        return {};

    // Accept both spec (0x20) and Dexed variant (0x10) at byte 4.
    // File size is ground truth — 4104 = 6 header + 4096 data + 1 checksum + 1 F7.
    constexpr size_t kVoiceDataLen = 4096;

    if (!verifyChecksum(data + 6, kVoiceDataLen + 1))
        return {};

    return unpackBank(data + 6);
}

} // namespace HDAW
