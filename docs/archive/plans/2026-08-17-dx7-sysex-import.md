# DX7 SysEx Import Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Import DX7 .syx voice/cartridge files into HDAW's internal FM synth engine via MCP tool, with voice name metadata returned.

**Architecture:** A self-contained parser (`Dx7SysexImport`) handles both single-voice (VCED, 163 bytes) and 32-voice cartridge (VMEM packed, 4104 bytes) SysEx formats. The parser converts packed VMEM byte-fields to the engine's 156-byte VCED layout and validates checksums. An MCP tool (`fm_synth_import_sysex`) reads a .syx file from disk, parses it, and calls `loadPatch()` on the target FM synth slot. Voice name metadata is returned for UI display.

**Tech Stack:** C++17, JUCE (File I/O), gtest. No new dependencies.

**Key insight — operator ordering:** The engine stores operators in DX7 VCED order (OP6 at offset 0, OP1 at offset 101). Both VCED SysEx dumps and the engine's `patchData_[156]` share this layout — no reordering needed. VMEM packed dumps also store OP6-first, so unpacking extracts bit-fields in-place without transposition.

---

## File Structure

| Action | File | Responsibility |
|--------|------|----------------|
| Create | `src/engine/Dx7SysexImport.h` | Static parsing functions: `parseSingleVoiceSysex`, `parseCartridgeSysex`, `unpackVmemVoice` |
| Create | `tests/unit/engine/dx7_sysex_import_test.cpp` | Unit tests for all three parsing paths + checksum validation |
| Modify | `src/mcp/McpTools_Audio.cpp` | Add `fm_synth_import_sysex` MCP tool (~40 lines) |
| Modify | `CMakeLists.txt:106` | Add `src/engine/Dx7SysexImport.cpp` to engine sources |
| Modify | `tests/CMakeLists.txt:45` | Add `unit/engine/dx7_sysex_import_test.cpp` to test sources |
| Modify | `frontend/src/components/FileBrowser.tsx:17-19` | Add `.syx` to supported file types for FM synth drag/drop |

---

## Task 1: Dx7SysexImport parser — header + implementation

**Files:**
- Create: `src/engine/Dx7SysexImport.h`
- Create: `src/engine/Dx7SysexImport.cpp`
- Modify: `CMakeLists.txt:106` (add new .cpp to sources)

- [ ] **Step 1: Create the header**

`src/engine/Dx7SysexImport.h`:
```cpp
#pragma once
#include <cstdint>
#include <array>
#include <optional>
#include <QString>

namespace HDAW {

struct Dx7Voice {
    std::array<uint8_t, 156> patchData{};   // Engine VCED layout (OP6 at [0])
    std::string voiceName;                   // 10-char ASCII name
    int algorithm = 0;
    int feedback = 0;
};

// Parse a single-voice SysEx dump (F0 43 00 00 01 1B ... F7, 163 bytes total).
// Returns nullopt on: wrong header, bad checksum, wrong size.
std::optional<Dx7Voice> parseSingleVoiceSysex(const uint8_t* data, size_t size);

// Parse a 32-voice cartridge SysEx dump (F0 43 00 09 20 00 ... F7, 4104 bytes total).
// Returns a vector of up to 32 voices, or empty on error.
std::vector<Dx7Voice> parseCartridgeSysex(const uint8_t* data, size_t size);

// Unpack one 128-byte VMEM voice to 156-byte engine layout.
// Called internally by parseCartridgeSysex; exposed for testing.
void unpackVmemVoice(const uint8_t packed[128], uint8_t unpacked[156]);

} // namespace HDAW
```

- [ ] **Step 2: Create the implementation**

`src/engine/Dx7SysexImport.cpp`:
```cpp
#include "Dx7SysexImport.h"
#include <cstring>

namespace HDAW {

static bool verifyChecksum(const uint8_t* data, size_t len) {
    int sum = 0;
    for (size_t i = 0; i < len; ++i)
        sum += data[i];
    return ((~sum + 1) & 0x7F) == 0;
}

std::optional<Dx7Voice> parseSingleVoiceSysex(const uint8_t* data, size_t size) {
    if (size < 6 || data[0] != 0xF0 || data[1] != 0x43)
        return std::nullopt;

    // Byte 3: format number. 0x00 = 1 voice (VCED).
    // Byte 4-5: data length MSB/LSB (big-endian 14-bit).
    if (data[3] != 0x00)
        return std::nullopt;

    size_t dataLen = (static_cast<size_t>(data[4]) << 7) | data[5];
    if (dataLen != 155)
        return std::nullopt;

    size_t totalExpected = 6 + dataLen + 1; // header(6) + data(155) + checksum(1)
    if (size < totalExpected)
        return std::nullopt;

    // Verify checksum (over data bytes only)
    if (!verifyChecksum(data + 6, dataLen))
        return std::nullopt;

    Dx7Voice voice;
    // Copy 155 VCED bytes into patchData. VCED layout matches engine layout
    // (OP6 at offset 0, OP1 at offset 101). Byte 155 (op on/off) defaults
    // to 0x3F (all ops on) since VCED dumps don't include it.
    std::memcpy(voice.patchData.data(), data + 6, 155);
    voice.patchData[155] = 0x3F; // all 6 operators on

    // Extract voice name (VCED bytes 145-154 = ASCII, space-padded)
    char nameBuf[11] = {};
    std::memcpy(nameBuf, data + 6 + 145, 10);
    voice.voiceName = std::string(nameBuf, 10);
    // Trim trailing spaces
    auto pos = voice.voiceName.find_last_not_of(' ');
    if (pos != std::string::npos)
        voice.voiceName.erase(pos + 1);

    voice.algorithm = data[6 + 134] & 0x1F;
    voice.feedback = data[6 + 135] & 0x07;

    return voice;
}

void unpackVmemVoice(const uint8_t packed[128], uint8_t unpacked[156]) {
    std::memset(unpacked, 0, 156);

    // VMEM stores operators in reverse order: OP6 (packed bytes 0-16),
    // OP5 (17-33), ..., OP1 (85-101). The engine also stores OP6 at
    // patchData[0], so no transposition is needed — just unpack bit-fields.

    for (int op = 0; op < 6; ++op) {
        const uint8_t* p = packed + op * 17;
        uint8_t* u = unpacked + op * 21;

        // Bytes 0-7: EG rates 1-4 and levels 1-4 (straight copy, 7-bit each)
        for (int i = 0; i < 8; ++i)
            u[i] = p[i];

        // Bytes 8-10: break point, left depth, right depth (straight copy)
        u[8]  = p[8];
        u[9]  = p[9];
        u[10] = p[10];

        // Byte 11: left curve [2:0], right curve [5:3]
        u[11] = p[11] & 0x07;             // left curve
        u[12] = (p[11] >> 3) & 0x07;      // right curve

        // Byte 12: rate scaling [2:0], detune [6:3]
        u[13] = p[12] & 0x07;             // rate scaling
        u[14] = (p[12] >> 3) & 0x0F;      // detune (4 bits, 0-14)

        // Byte 13: amp mod sensitivity [2:0], key velocity sensitivity [5:3]
        u[15] = p[13] & 0x07;             // amp mod sensitivity
        u[16] = (p[13] >> 3) & 0x07;      // key velocity sensitivity

        // Byte 14: output level (straight copy)
        u[17] = p[14];

        // Byte 15: osc mode [0], freq coarse [5:1]
        u[18] = p[15] & 0x01;             // osc mode (0=ratio, 1=fixed)
        u[19] = (p[15] >> 1) & 0x1F;      // freq coarse (5 bits, 0-31)

        // Byte 16: freq fine (straight copy)
        u[20] = p[16];
    }

    // Global section: VMEM bytes 102-127 → engine offsets 126-155
    // Pitch EG rates/levels (bytes 102-109 → offsets 126-133)
    for (int i = 0; i < 8; ++i)
        unpacked[126 + i] = packed[102 + i];

    // Algorithm [4:0] (byte 110 → offset 134)
    unpacked[134] = packed[110] & 0x1F;

    // Feedback [3:1] + osc key sync [0] (byte 111 → offsets 135-136)
    unpacked[135] = (packed[111] >> 1) & 0x07;
    unpacked[136] = packed[111] & 0x01;

    // LFO speed, delay, pitch mod depth, amp mod depth (bytes 112-115 → 137-140)
    for (int i = 0; i < 4; ++i)
        unpacked[137 + i] = packed[112 + i];

    // LFO key sync [0], waveform [3:1], pitch mod sensitivity [6:4] (byte 116 → 141-143)
    unpacked[141] = packed[116] & 0x01;              // lfo key sync
    unpacked[142] = (packed[116] >> 1) & 0x07;       // lfo waveform
    unpacked[143] = (packed[116] >> 4) & 0x07;       // pitch mod sensitivity

    // Transpose (byte 117 → offset 144)
    unpacked[144] = packed[117];

    // Voice name (bytes 118-127 → offsets 145-154)
    for (int i = 0; i < 10; ++i)
        unpacked[145 + i] = packed[118 + i];

    // Operator on/off (not in VMEM) — default all on
    unpacked[155] = 0x3F;
}

std::vector<Dx7Voice> parseCartridgeSysex(const uint8_t* data, size_t size) {
    if (size < 6 || data[0] != 0xF0 || data[1] != 0x43)
        return {};

    // Byte 3: format number. 0x09 = 32 voices (VMEM bulk dump).
    if (data[3] != 0x09)
        return {};

    size_t dataLen = (static_cast<size_t>(data[4]) << 7) | data[5];
    if (dataLen != 4096)
        return {};

    size_t totalExpected = 6 + dataLen + 1; // header(6) + data(4096) + checksum(1)
    if (size < totalExpected)
        return {};

    if (!verifyChecksum(data + 6, dataLen))
        return {};

    std::vector<Dx7Voice> voices;
    voices.reserve(32);

    for (int v = 0; v < 32; ++v) {
        Dx7Voice voice;
        unpackVmemVoice(data + 6 + v * 128, voice.patchData.data());

        // Extract voice name from unpacked data (offsets 145-154)
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

} // namespace HDAW
```

- [ ] **Step 3: Add to CMakeLists.txt engine sources**

In `CMakeLists.txt`, after line 106 (`src/engine/FmSynthEngine.cpp`), add:
```cmake
    src/engine/Dx7SysexImport.cpp
```

- [ ] **Step 4: Build to verify compilation**

Run: `cmake --build build --config Debug 2>&1 | head -30`
Expected: compiles without errors (the .cpp is now in the build but not yet called from any translation unit, so it should compile cleanly as a standalone TU).

---

## Task 2: Unit tests for Dx7SysexImport

**Files:**
- Create: `tests/unit/engine/dx7_sysex_import_test.cpp`
- Modify: `tests/CMakeLists.txt:45` (add new test file)

- [ ] **Step 1: Add test file to CMakeLists.txt**

In `tests/CMakeLists.txt`, after line 45 (`unit/engine/fm_synth_test.cpp`), add:
```cmake
    unit/engine/dx7_sysex_import_test.cpp
```

- [ ] **Step 2: Write the failing tests**

`tests/unit/engine/dx7_sysex_import_test.cpp`:
```cpp
#include <gtest/gtest.h>
#include "engine/Dx7SysexImport.h"
#include <cstring>
#include <vector>

using namespace HDAW;

// --- Helper: build a minimal valid single-voice sysex (163 bytes) ---
static std::vector<uint8_t> makeSingleVoiceSysex(const uint8_t voiceData[155]) {
    std::vector<uint8_t> sysex;
    sysex.reserve(163);
    sysex.push_back(0xF0);  // SysEx start
    sysex.push_back(0x43);  // Yamaha
    sysex.push_back(0x00);  // sub-status 0, channel 0
    sysex.push_back(0x00);  // format: 1 voice
    sysex.push_back(0x00);  // data length MSB (155 = 0x009B)
    sysex.push_back(0x9B);  // data length LSB
    // Data bytes
    int sum = 0;
    for (int i = 0; i < 155; ++i) {
        sysex.push_back(voiceData[i]);
        sum += voiceData[i];
    }
    // Checksum: (~sum + 1) & 0x7F
    sysex.push_back(static_cast<uint8_t>((~sum + 1) & 0x7F));
    sysex.push_back(0xF7);  // SysEx end
    return sysex;
}

// --- Helper: build a minimal valid 32-voice cartridge sysex (4104 bytes) ---
static std::vector<uint8_t> makeCartridgeSysex() {
    std::vector<uint8_t> sysex;
    sysex.reserve(4104);
    sysex.push_back(0xF0);
    sysex.push_back(0x43);
    sysex.push_back(0x00);
    sysex.push_back(0x09);  // format: 32 voices
    sysex.push_back(0x20);  // data length MSB (4096 = 0x1000)
    sysex.push_back(0x00);  // data length LSB

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

// --- Tests: parseSingleVoiceSysex ---

TEST(Dx7SysexImport, SingleVoiceValidPatch) {
    uint8_t voice[155] = {};
    voice[134] = 5;   // algorithm
    voice[135] = 3;   // feedback
    voice[145] = 'E'; // name: "E.PIANO"
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
    // All operators should be on by default
    EXPECT_EQ(result->patchData[155], 0x3F);
}

TEST(Dx7SysexImport, SingleVoiceChecksumError) {
    uint8_t voice[155] = {};
    auto sysex = makeSingleVoiceSysex(voice);
    sysex[10] ^= 0x01; // corrupt a data byte → bad checksum

    auto result = parseSingleVoiceSysex(sysex.data(), sysex.size());
    EXPECT_FALSE(result.has_value());
}

TEST(Dx7SysexImport, SingleVoiceWrongFormat) {
    uint8_t voice[155] = {};
    auto sysex = makeSingleVoiceSysex(voice);
    sysex[3] = 0x09; // wrong format (32-voice, not 1-voice)

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
    // Fill every byte with a recognizable pattern
    for (int i = 0; i < 155; ++i)
        voice[i] = static_cast<uint8_t>(i);

    auto sysex = makeSingleVoiceSysex(voice);
    auto result = parseSingleVoiceSysex(sysex.data(), sysex.size());

    ASSERT_TRUE(result.has_value());
    for (int i = 0; i < 155; ++i)
        EXPECT_EQ(result->patchData[i], static_cast<uint8_t>(i))
            << "mismatch at offset " << i;
    EXPECT_EQ(result->patchData[155], 0x3F); // op on/off default
}

// --- Tests: unpackVmemVoice ---

TEST(Dx7SysexImport, VmemUnpackEgRates) {
    uint8_t packed[128] = {};
    // OP6 (first in VMEM) EG rates: 90, 80, 70, 60
    packed[0] = 90; packed[1] = 80; packed[2] = 70; packed[3] = 60;
    // OP6 EG levels: 99, 85, 50, 0
    packed[4] = 99; packed[5] = 85; packed[6] = 50; packed[7] = 0;

    uint8_t unpacked[156] = {};
    unpackVmemVoice(packed, unpacked);

    // Should appear at offset 0 (OP6 in engine layout)
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
    // OP6 byte 11: left curve=3, right curve=1 → 0b_001_011 = 0x0B
    packed[11] = 0x0B;
    // OP6 byte 12: rate scaling=5, detune=10 → 0b_1010_101 = 0x55
    packed[12] = 0x55;
    // OP6 byte 13: amp mod sens=2, key vel sens=6 → 0b_110_010 = 0x32
    packed[13] = 0x32;
    // OP6 byte 15: osc mode=1, freq coarse=7 → 0b_00111_1 = 0x0F
    packed[15] = 0x0F;

    uint8_t unpacked[156] = {};
    unpackVmemVoice(packed, unpacked);

    EXPECT_EQ(unpacked[11], 3u);  // left curve
    EXPECT_EQ(unpacked[12], 1u);  // right curve
    EXPECT_EQ(unpacked[13], 5u);  // rate scaling
    EXPECT_EQ(unpacked[14], 10u); // detune
    EXPECT_EQ(unpacked[15], 2u);  // amp mod sensitivity
    EXPECT_EQ(unpacked[16], 6u);  // key velocity sensitivity
    EXPECT_EQ(unpacked[18], 1u);  // osc mode
    EXPECT_EQ(unpacked[19], 7u);  // freq coarse
}

TEST(Dx7SysexImport, VmemUnpackGlobalSection) {
    uint8_t packed[128] = {};
    // Pitch EG rates (VMEM bytes 102-105)
    packed[102] = 99; packed[103] = 80; packed[104] = 60; packed[105] = 40;
    // Pitch EG levels (VMEM bytes 106-109)
    packed[106] = 90; packed[107] = 70; packed[108] = 50; packed[109] = 30;
    // Algorithm (VMEM byte 110): 15
    packed[110] = 15;
    // Feedback (byte 111): fb=5, sync=1 → (5<<1)|1 = 0x0B
    packed[111] = 0x0B;
    // LFO speed=77, delay=33, pitch mod=55, amp mod=11 (bytes 112-115)
    packed[112] = 77; packed[113] = 33; packed[114] = 55; packed[115] = 11;
    // LFO key sync=1, waveform=3, pitch mod sens=5 → (5<<4)|(3<<1)|1 = 0x57
    packed[116] = 0x57;
    // Transpose=24
    packed[117] = 24;
    // Voice name "TESTVOICE " (bytes 118-127)
    packed[118] = 'T'; packed[119] = 'E'; packed[120] = 'S'; packed[121] = 'T';
    packed[122] = 'V'; packed[123] = 'O'; packed[124] = 'I'; packed[125] = 'C';
    packed[126] = 'E'; packed[127] = ' ';

    uint8_t unpacked[156] = {};
    unpackVmemVoice(packed, unpacked);

    EXPECT_EQ(unpacked[126], 99u); // pitch eg rate 1
    EXPECT_EQ(unpacked[127], 80u); // pitch eg rate 2
    EXPECT_EQ(unpacked[130], 90u); // pitch eg level 1
    EXPECT_EQ(unpacked[134], 15u); // algorithm
    EXPECT_EQ(unpacked[135], 5u);  // feedback
    EXPECT_EQ(unpacked[136], 1u);  // osc key sync
    EXPECT_EQ(unpacked[137], 77u); // lfo speed
    EXPECT_EQ(unpacked[138], 33u); // lfo delay
    EXPECT_EQ(unpacked[139], 55u); // lfo pitch mod depth
    EXPECT_EQ(unpacked[140], 11u); // lfo amp mod depth
    EXPECT_EQ(unpacked[141], 1u);  // lfo key sync
    EXPECT_EQ(unpacked[142], 3u);  // lfo waveform
    EXPECT_EQ(unpacked[143], 5u);  // pitch mod sensitivity
    EXPECT_EQ(unpacked[144], 24u); // transpose
    EXPECT_EQ(unpacked[155], 0x3F); // op on/off default
    // Voice name
    char name[11] = {};
    std::memcpy(name, unpacked + 145, 10);
    EXPECT_EQ(std::string(name, 10), "TESTVOICE ");
}

// --- Tests: parseCartridgeSysex ---

TEST(Dx7SysexImport, CartridgeReturns32Voices) {
    auto sysex = makeCartridgeSysex();
    auto voices = parseCartridgeSysex(sysex.data(), sysex.size());
    EXPECT_EQ(voices.size(), 32u);
}

TEST(Dx7SysexImport, CartridgeChecksumError) {
    auto sysex = makeCartridgeSysex();
    sysex[200] ^= 0x01; // corrupt
    auto voices = parseCartridgeSysex(sysex.data(), sysex.size());
    EXPECT_TRUE(voices.empty());
}

TEST(Dx7SysexImport, CartridgeWrongFormat) {
    auto sysex = makeCartridgeSysex();
    sysex[3] = 0x00; // wrong format (1-voice, not 32)
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
    // Set voice name for voice 0 (at packed offset 118-127)
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
    // Recompute checksum
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
    // Set algorithm=31, feedback=7 for voice 0 (packed bytes 110-111)
    sysex[6 + 110] = 31;  // algorithm
    sysex[6 + 111] = (7 << 1) | 0; // feedback=7, sync=0
    // Recompute checksum
    int sum = 0;
    for (size_t i = 6; i < 6 + 4096; ++i)
        sum += sysex[i];
    sysex[6 + 4096] = static_cast<uint8_t>((~sum + 1) & 0x7F);

    auto voices = parseCartridgeSysex(sysex.data(), sysex.size());
    ASSERT_EQ(voices.size(), 32u);
    EXPECT_EQ(voices[0].algorithm, 31);
    EXPECT_EQ(voices[0].feedback, 7);
}
```

- [ ] **Step 3: Build and run tests**

Run: `cmake --build build --config Debug && build\Debug\hdaw_tests.exe --gtest_filter="Dx7SysexImport.*"`
Expected: All 16 tests PASS.

- [ ] **Step 4: Commit**

```bash
git add src/engine/Dx7SysexImport.h src/engine/Dx7SysexImport.cpp tests/unit/engine/dx7_sysex_import_test.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(fm): DX7 SysEx parser — single voice + 32-voice cartridge"
```

---

## Task 3: MCP tool `fm_synth_import_sysex`

**Files:**
- Modify: `src/mcp/McpTools_Audio.cpp` (add new tool registration after `fm_synth_load_preset`, ~line 613)

- [ ] **Step 1: Add the MCP tool**

After the `fm_synth_load_preset` tool registration (line 613), add:

```cpp
    s.registerTool({"fm_synth_import_sysex",
        "Import a DX7 .syx file into an FM synth FX slot. Supports single voice dumps "
        "(163 bytes) and 32-voice cartridge dumps (4104 bytes). For cartridges, loads "
        "voice index 0 (first voice). Returns the voice name if available.",
        objSchema({{"trackId",   QJsonObject{{"type","integer"}}},
                   {"slotIndex", QJsonObject{{"type","integer"}}},
                   {"filePath",  QJsonObject{{"type","string"}}},
                   {"voiceIndex",QJsonObject{{"type","integer"}}}},
                   {"trackId","slotIndex","filePath"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            auto fxSlots = e->getReadModel().getFxSlots(ti);
            if (si < 0 || si >= (int)fxSlots.size())
                return McpToolResult::text("slot not found", true);
            if (fxSlots[si].fxType != "fm_synth")
                return McpToolResult::text("slot is not an FM synth", true);

            QString filePath = a.value("filePath").toString();
            if (filePath.isEmpty())
                return McpToolResult::text("filePath required", true);

            juce::File syxFile(filePath.toStdString());
            if (!syxFile.existsAsFile())
                return McpToolResult::text("file not found: " + filePath, true);

            juce::MemoryBlock raw;
            if (!syxFile.loadFileAsData(raw))
                return McpToolResult::text("failed to read file", true);

            auto* bytes = static_cast<const uint8_t*>(raw.getData());
            size_t fileSize = raw.getSize();

            // Try single voice first, then cartridge
            std::optional<Dx7Voice> voice;
            std::vector<Dx7Voice> voices;

            if (fileSize >= 163 && bytes[0] == 0xF0 && bytes[1] == 0x43 && bytes[3] == 0x00) {
                voice = parseSingleVoiceSysex(bytes, fileSize);
            } else if (fileSize >= 4104 && bytes[0] == 0xF0 && bytes[1] == 0x43 && bytes[3] == 0x09) {
                voices = parseCartridgeSysex(bytes, fileSize);
                int vi = a.value("voiceIndex").toInt(0);
                if (vi >= 0 && vi < (int)voices.size())
                    voice = voices[vi];
            } else {
                return McpToolResult::text(
                    "not a recognized DX7 SysEx file (expected F0 43 00 00 or F0 43 00 09 header)", true);
            }

            if (!voice.has_value())
                return McpToolResult::text("failed to parse SysEx data (bad checksum or size)", true);

            auto* proc = e->getMainProcessor();
            if (!proc) return McpToolResult::text("audio engine not initialized", true);
            auto* track = proc->getTrack(ti);
            if (!track) return McpToolResult::text("track not found", true);
            auto& chain = track->getFXChain();
            if (si >= (int)chain.size() || !chain[si])
                return McpToolResult::text("FX slot not found in chain", true);
            auto* slot = chain[si].get();
            if (!slot->fmSynthEngine())
                return McpToolResult::text("FM synth engine not initialized", true);

            slot->fmSynthEngine()->loadPatch(voice->patchData.data());

            QJsonObject result;
            result["ok"] = true;
            result["voiceName"] = QString::fromStdString(voice->voiceName);
            result["algorithm"] = voice->algorithm;
            result["feedback"] = voice->feedback;
            if (!voices.empty())
                result["totalVoices"] = static_cast<int>(voices.size());

            return McpToolResult::text(
                QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)));
        }});
```

- [ ] **Step 2: Add the include**

At the top of `McpTools_Audio.cpp`, add after the existing includes:
```cpp
#include "engine/Dx7SysexImport.h"
```

- [ ] **Step 3: Build and verify**

Run: `cmake --build build --config Debug 2>&1 | head -30`
Expected: compiles without errors.

- [ ] **Step 4: Run FM synth tests to verify no regression**

Run: `build\Debug\hdaw_tests.exe --gtest_filter="FmSynthTest.*"`
Expected: All 25 existing tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/mcp/McpTools_Audio.cpp
git commit -m "feat(fm): MCP tool fm_synth_import_sysex — load .syx into internal FM synth"
```

---

## Task 4: Frontend — recognize .syx files in file browser

**Files:**
- Modify: `frontend/src/components/FileBrowser.tsx:17-19` (add `.syx` to supported types)

- [ ] **Step 1: Add .syx to file type lists**

In `frontend/src/components/FileBrowser.tsx`, find the file extension constants (around line 17-19):

Change:
```typescript
const AUDIO_EXTS = [".wav", ".aiff", ".aif", ".mp3", ".flac", ".ogg"];
const MIDI_EXTS = [".mid", ".midi"];
const DEVICE_EXTS = [".vst3", ".clap", ".dll"];
const PRESET_EXTS = [".fxp", ".fxb", ".vstpreset"];
```

To:
```typescript
const AUDIO_EXTS = [".wav", ".aiff", ".aif", ".mp3", ".flac", ".ogg"];
const MIDI_EXTS = [".mid", ".midi"];
const DEVICE_EXTS = [".vst3", ".clap", ".dll"];
const PRESET_EXTS = [".fxp", ".fxb", ".vstpreset", ".syx"];
```

- [ ] **Step 2: Run frontend tests**

Run: `cd frontend && npm test`
Expected: All tests PASS.

- [ ] **Step 3: Commit**

```bash
git add frontend/src/components/FileBrowser.tsx
git commit -m "feat(fm): recognize .syx files in file browser presets filter"
```

---

## Task 5: Verification — end-to-end

- [ ] **Step 1: Full engine test suite (excluding plugin isolation)**

Run:
```
build\Debug\hdaw_tests.exe --gtest_filter=-CrashRecovery.*:ProxyHealth.*:IsolatedScanner.*:PluginIsolation.*:McpServer.ExportAudioWithClapPluginDoesNotHang:McpServer.ExportAudioWithMultipleIsolatedInstances:McpServer.DiagnosticClapExportMatrix:RenderSequenceRelease.*
```
Expected: All tests PASS.

- [ ] **Step 2: Frontend test suite**

Run: `cd frontend && npm test`
Expected: All tests PASS.

- [ ] **Step 3: Frontend build**

Run: `cd frontend && npm run build`
Expected: Clean production build.

---

## Pitfall Gates Triggered

- **Gate 2 (Unimplemented code path):** The MCP tool `fm_synth_import_sysex` calls `loadPatch()` which has a proven path to the audio engine. The parser is a pure function with no SPSC dependency. No gap.
- **Gate 4 (Stale binary):** Full rebuild required after C++ changes. Verified in Task 5.
- **Gate 5 (Frontend stale closures):** Only adding `.syx` to a static array — no hooks/memos affected.
- **Gate 15 (Stale binary):** Must verify the binary after building — check timestamp of `build/Debug/hdaw_tests.exe`.

## Anti-Pattern Scan

- No N separate `await rpc.call()` — single MCP tool call.
- No raw hex in CSS — no CSS changes.
- No `syncSnapshot` — no frontend state mutation beyond adding a file extension.
- The parser is a pure function — no audio-thread concerns.
