#include <gtest/gtest.h>
#include "engine/Dx7SysexImport.h"
#include <cstring>
#include <vector>

using namespace HDAW;

static std::vector<uint8_t> makeSingleVoiceSysex(const uint8_t voiceData[155]) {
    std::vector<uint8_t> sysex;
    sysex.reserve(163);
    sysex.push_back(0xF0);
    sysex.push_back(0x43);
    sysex.push_back(0x00);
    sysex.push_back(0x00);
    sysex.push_back(0x00);
    sysex.push_back(0x9B);
    int sum = 0;
    for (int i = 0; i < 155; ++i) {
        sysex.push_back(voiceData[i]);
        sum += voiceData[i];
    }
    sysex.push_back(static_cast<uint8_t>((~sum + 1) & 0x7F));
    sysex.push_back(0xF7);
    return sysex;
}

static std::vector<uint8_t> makeCartridgeSysex() {
    std::vector<uint8_t> sysex;
    sysex.reserve(4104);
    sysex.push_back(0xF0);
    sysex.push_back(0x43);
    sysex.push_back(0x00);
    sysex.push_back(0x09);
    sysex.push_back(0x20);
    sysex.push_back(0x00);
    int sum = 0;
    for (size_t i = 0; i < 4096; ++i) {
        uint8_t b = static_cast<uint8_t>(i & 0x7F);
        sysex.push_back(b);
        sum += b;
    }
    sysex.push_back(static_cast<uint8_t>((~sum + 1) & 0x7F));
    sysex.push_back(0xF7);
    return sysex;
}

TEST(Dx7SysexImport, SingleVoiceValidPatch) {
    uint8_t voice[155] = {};
    voice[134] = 5;
    voice[135] = 3;
    voice[145] = 'E';
    voice[146] = '.';
    voice[147] = 'P';
    voice[148] = 'I';
    voice[149] = 'A';
    voice[150] = 'N';
    voice[151] = 'O';
    voice[152] = ' ';
    voice[153] = ' ';
    voice[154] = ' ';

    auto sysex = makeSingleVoiceSysex(voice);
    auto result = parseSingleVoiceSysex(sysex.data(), sysex.size());

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->algorithm, 5);
    EXPECT_EQ(result->feedback, 3);
    EXPECT_EQ(result->voiceName, "E.PIANO");
    EXPECT_EQ(result->patchData[134], 5u);
    EXPECT_EQ(result->patchData[135], 3u);
    EXPECT_EQ(result->patchData[155], 0x3F);
}

TEST(Dx7SysexImport, SingleVoiceChecksumError) {
    uint8_t voice[155] = {};
    auto sysex = makeSingleVoiceSysex(voice);
    sysex[10] ^= 0x01;

    auto result = parseSingleVoiceSysex(sysex.data(), sysex.size());
    EXPECT_FALSE(result.has_value());
}

TEST(Dx7SysexImport, SingleVoiceWrongFormat) {
    uint8_t voice[155] = {};
    auto sysex = makeSingleVoiceSysex(voice);
    sysex[3] = 0x09;

    auto result = parseSingleVoiceSysex(sysex.data(), sysex.size());
    EXPECT_FALSE(result.has_value());
}

TEST(Dx7SysexImport, SingleVoiceTooSmall) {
    uint8_t data[10] = {0xF0, 0x43, 0x00, 0x00, 0x00, 0x9B};
    auto result = parseSingleVoiceSysex(data, sizeof(data));
    EXPECT_FALSE(result.has_value());
}

TEST(Dx7SysexImport, SingleVoiceNotSysex) {
    uint8_t data[163] = {};
    auto result = parseSingleVoiceSysex(data, sizeof(data));
    EXPECT_FALSE(result.has_value());
}

TEST(Dx7SysexImport, SingleVoiceAllParamsCopied) {
    uint8_t voice[155] = {};
    for (int i = 0; i < 155; ++i)
        voice[i] = static_cast<uint8_t>(i);

    auto sysex = makeSingleVoiceSysex(voice);
    auto result = parseSingleVoiceSysex(sysex.data(), sysex.size());

    ASSERT_TRUE(result.has_value());
    for (int i = 0; i < 155; ++i)
        EXPECT_EQ(result->patchData[i], static_cast<uint8_t>(i))
            << "mismatch at offset " << i;
    EXPECT_EQ(result->patchData[155], 0x3F);
}

TEST(Dx7SysexImport, VmemUnpackEgRates) {
    uint8_t packed[128] = {};
    packed[0] = 90; packed[1] = 80; packed[2] = 70; packed[3] = 60;
    packed[4] = 99; packed[5] = 85; packed[6] = 50; packed[7] = 0;

    uint8_t unpacked[156] = {};
    unpackVmemVoice(packed, unpacked);

    EXPECT_EQ(unpacked[0], 90u);
    EXPECT_EQ(unpacked[1], 80u);
    EXPECT_EQ(unpacked[2], 70u);
    EXPECT_EQ(unpacked[3], 60u);
    EXPECT_EQ(unpacked[4], 99u);
    EXPECT_EQ(unpacked[5], 85u);
    EXPECT_EQ(unpacked[6], 50u);
    EXPECT_EQ(unpacked[7], 0u);
}

TEST(Dx7SysexImport, VmemUnpackBitPackedFields) {
    uint8_t packed[128] = {};
    packed[11] = 0x0B;
    packed[12] = 0x55;
    packed[13] = 0x32;
    packed[14] = 80;
    packed[15] = 0x0F;
    packed[16] = 99;

    uint8_t unpacked[156] = {};
    unpackVmemVoice(packed, unpacked);

    EXPECT_EQ(unpacked[11], 0x0Bu & 0x03u);       // left curve  = 3
    EXPECT_EQ(unpacked[12], (0x0Bu >> 2) & 0x03u); // right curve = 2
    EXPECT_EQ(unpacked[13], 0x55u & 0x07u);        // rate scale  = 5
    EXPECT_EQ(unpacked[14], 0x32u & 0x03u);        // amp mod sens = 2
    EXPECT_EQ(unpacked[15], (0x32u >> 2) & 0x07u); // key vel sens = 4
    EXPECT_EQ(unpacked[16], 80u);                  // output level = 80
    EXPECT_EQ(unpacked[17], 0x0Fu & 0x01u);        // osc mode = 1
    EXPECT_EQ(unpacked[18], (0x0Fu >> 1) & 0x1Fu); // freq coarse = 7
    EXPECT_EQ(unpacked[19], 99u);                  // freq fine = 99
    EXPECT_EQ(unpacked[20], (0x55u >> 3) & 0x0Fu); // detune = 10
}

TEST(Dx7SysexImport, VmemUnpackOutputLevelAndOscMode) {
    uint8_t packed[128] = {};
    // All six operators: output level = 75, osc mode bit0 = 1 (fixed),
    // detune bits (packed 12 high nibble) set to verify the detune slot.
    for (int op = 0; op < 6; ++op) {
        packed[op * 17 + 14] = 75;
        packed[op * 17 + 15] = 0x01;
        packed[op * 17 + 12] = (10 << 3);
    }

    uint8_t unpacked[156] = {};
    unpackVmemVoice(packed, unpacked);

    for (int op = 0; op < 6; ++op) {
        SCOPED_TRACE("op " + std::to_string(op));
        EXPECT_EQ(unpacked[op * 21 + 16], 75u);
        EXPECT_EQ(unpacked[op * 21 + 17], 1u);
        EXPECT_EQ(unpacked[op * 21 + 20], 10u);
    }
}

TEST(Dx7SysexImport, VmemUnpackGlobalSection) {
    uint8_t packed[128] = {};
    packed[102] = 99; packed[103] = 80; packed[104] = 60; packed[105] = 40;
    packed[106] = 90; packed[107] = 70; packed[108] = 50; packed[109] = 30;
    packed[110] = 15;
    packed[111] = 0x0B;
    packed[112] = 77; packed[113] = 33; packed[114] = 55; packed[115] = 11;
    packed[116] = 0x57;
    packed[117] = 24;
    packed[118] = 'T'; packed[119] = 'E'; packed[120] = 'S'; packed[121] = 'T';
    packed[122] = 'V'; packed[123] = 'O'; packed[124] = 'I'; packed[125] = 'C';
    packed[126] = 'E'; packed[127] = ' ';

    uint8_t unpacked[156] = {};
    unpackVmemVoice(packed, unpacked);

    EXPECT_EQ(unpacked[126], 99u);
    EXPECT_EQ(unpacked[127], 80u);
    EXPECT_EQ(unpacked[130], 90u);
    EXPECT_EQ(unpacked[134], 15u);
    EXPECT_EQ(unpacked[135], 5u);
    EXPECT_EQ(unpacked[136], 1u);
    EXPECT_EQ(unpacked[137], 77u);
    EXPECT_EQ(unpacked[138], 33u);
    EXPECT_EQ(unpacked[139], 55u);
    EXPECT_EQ(unpacked[140], 11u);
    EXPECT_EQ(unpacked[141], 1u);
    EXPECT_EQ(unpacked[142], 3u);
    EXPECT_EQ(unpacked[143], 5u);
    EXPECT_EQ(unpacked[144], 24u);
    EXPECT_EQ(unpacked[155], 0x3F);
    char name[11] = {};
    std::memcpy(name, unpacked + 145, 10);
    EXPECT_EQ(std::string(name, 10), "TESTVOICE ");
}

static std::vector<uint8_t> makeCartridgeSysexDexed() {
    std::vector<uint8_t> sysex;
    sysex.reserve(4104);
    sysex.push_back(0xF0);
    sysex.push_back(0x43);
    sysex.push_back(0x00);
    sysex.push_back(0x09);
    sysex.push_back(0x10); // Dexed variant: 0x10 instead of spec's 0x20
    sysex.push_back(0x00);
    int sum = 0;
    for (size_t i = 0; i < 4096; ++i) {
        uint8_t b = static_cast<uint8_t>(i & 0x7F);
        sysex.push_back(b);
        sum += b;
    }
    sysex.push_back(static_cast<uint8_t>((~sum + 1) & 0x7F));
    sysex.push_back(0xF7);
    return sysex;
}

TEST(Dx7SysexImport, CartridgeDexedVariantAccepted) {
    auto sysex = makeCartridgeSysexDexed();
    auto voices = parseCartridgeSysex(sysex.data(), sysex.size());
    EXPECT_EQ(voices.size(), 32u);
}

TEST(Dx7SysexImport, CartridgeReturns32Voices) {
    auto sysex = makeCartridgeSysex();
    auto voices = parseCartridgeSysex(sysex.data(), sysex.size());
    EXPECT_EQ(voices.size(), 32u);
}

TEST(Dx7SysexImport, CartridgeChecksumError) {
    auto sysex = makeCartridgeSysex();
    sysex[200] ^= 0x01;
    auto voices = parseCartridgeSysex(sysex.data(), sysex.size());
    EXPECT_TRUE(voices.empty());
}

TEST(Dx7SysexImport, CartridgeWrongFormat) {
    auto sysex = makeCartridgeSysex();
    sysex[3] = 0x00;
    auto voices = parseCartridgeSysex(sysex.data(), sysex.size());
    EXPECT_TRUE(voices.empty());
}

TEST(Dx7SysexImport, CartridgeTooSmall) {
    uint8_t data[10] = {0xF0, 0x43, 0x00, 0x09, 0x20, 0x00};
    auto voices = parseCartridgeSysex(data, sizeof(data));
    EXPECT_TRUE(voices.empty());
}

TEST(Dx7SysexImport, CartridgeVoiceNamesExtracted) {
    auto sysex = makeCartridgeSysex();
    sysex[6 + 118] = 'B';
    sysex[6 + 119] = 'A';
    sysex[6 + 120] = 'S';
    sysex[6 + 121] = 'S';
    sysex[6 + 122] = ' ';
    sysex[6 + 123] = ' ';
    sysex[6 + 124] = ' ';
    sysex[6 + 125] = ' ';
    sysex[6 + 126] = ' ';
    sysex[6 + 127] = ' ';
    int sum = 0;
    for (size_t i = 6; i < 6 + 4096; ++i)
        sum += sysex[i];
    sysex[6 + 4096] = static_cast<uint8_t>((~sum + 1) & 0x7F);

    auto voices = parseCartridgeSysex(sysex.data(), sysex.size());
    ASSERT_EQ(voices.size(), 32u);
    EXPECT_EQ(voices[0].voiceName, "BASS");
}

TEST(Dx7SysexImport, CartridgeAlgorithmAndFeedback) {
    auto sysex = makeCartridgeSysex();
    sysex[6 + 110] = 31;
    sysex[6 + 111] = (7 << 1) | 0;
    int sum = 0;
    for (size_t i = 6; i < 6 + 4096; ++i)
        sum += sysex[i];
    sysex[6 + 4096] = static_cast<uint8_t>((~sum + 1) & 0x7F);

    auto voices = parseCartridgeSysex(sysex.data(), sysex.size());
    ASSERT_EQ(voices.size(), 32u);
    EXPECT_EQ(voices[0].algorithm, 31);
    EXPECT_EQ(voices[0].feedback, 7);
}

TEST(Dx7SysexImport, RawBankReturns32Voices) {
    std::vector<uint8_t> data(4096);
    for (int v = 0; v < 32; ++v) {
        for (int j = 0; j < 128; ++j)
            data[v * 128 + j] = static_cast<uint8_t>(((v + 1) * 16 + j) & 0x7F);
        // Packed op0 output level (byte 14) -> VCED op0 output level [16].
        data[v * 128 + 14] = static_cast<uint8_t>(40 + v);
    }

    auto voices = parseCartridgeSysex(data.data(), data.size());
    ASSERT_EQ(voices.size(), 32u);
    for (int v = 0; v < 32; ++v) {
        SCOPED_TRACE("voice " + std::to_string(v));
        EXPECT_EQ(voices[v].patchData[16], static_cast<uint8_t>(40 + v));
        EXPECT_FALSE(voices[v].voiceName.empty());
        // algorithm/feedback consistent with packed[110]/[111].
        EXPECT_EQ(voices[v].algorithm,
                  static_cast<int>(data[v * 128 + 110] & 0x1F));
        EXPECT_EQ(voices[v].feedback,
                  static_cast<int>((data[v * 128 + 111] >> 1) & 0x07));
        EXPECT_EQ(voices[v].patchData[155], 0x3F);
    }
    bool differ = false;
    for (int v = 1; v < 32 && !differ; ++v)
        differ = (voices[v].patchData != voices[0].patchData);
    EXPECT_TRUE(differ);
}

TEST(Dx7SysexImport, RawBankRejectsWrongSize) {
    std::vector<uint8_t> d4095(4095, 0x20);
    std::vector<uint8_t> d4098(4098, 0x20);
    EXPECT_TRUE(parseCartridgeSysex(d4095.data(), d4095.size()).empty());
    EXPECT_TRUE(parseCartridgeSysex(d4098.data(), d4098.size()).empty());
}

TEST(Dx7SysexImport, RawBankTrailingF7Accepted) {
    std::vector<uint8_t> data(4096);
    for (int v = 0; v < 32; ++v)
        for (int j = 0; j < 128; ++j)
            data[v * 128 + j] = static_cast<uint8_t>(((v + 1) * 16 + j) & 0x7F);
    data.push_back(0xF7);

    auto voices = parseCartridgeSysex(data.data(), data.size());
    EXPECT_EQ(voices.size(), 32u);
}
