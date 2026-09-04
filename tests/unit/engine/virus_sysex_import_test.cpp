#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include "engine/VirusSysexImport.h"
#include <stdexcept>
#include <vector>

using namespace HDAW;

namespace {

// Fixtures are copied from timbre-lib/testdata/virus/ (proven against the
// real preset library in slice 1): a 267-byte B/C single and a single 524-byte
// TI block. Resolve via __FILE__ so the test is independent of the runner's
// working directory (Ninja/MSVC pass absolute source paths).
juce::File fixtureFile(const char* name)
{
    juce::File self(__FILE__);
    juce::File dir = juce::File::isAbsolutePath(__FILE__)
        ? self.getParentDirectory().getChildFile("testdata/virus")
        : juce::File::getCurrentWorkingDirectory().getChildFile(
            "tests/unit/engine/testdata/virus");
    return dir.getChildFile(name);
}

std::vector<uint8_t> readFixture(const char* name, size_t expectedSize)
{
    juce::File f = fixtureFile(name);
    if (!f.existsAsFile())
        throw std::runtime_error("missing fixture: " + f.getFullPathName().toStdString());
    juce::MemoryBlock raw;
    if (!f.loadFileAsData(raw))
        throw std::runtime_error("failed to read fixture: " + f.getFullPathName().toStdString());
    if (raw.getSize() != expectedSize)
        throw std::runtime_error("fixture size mismatch: " + f.getFullPathName().toStdString());
    const auto* bytes = static_cast<const uint8_t*>(raw.getData());
    return std::vector<uint8_t>(bytes, bytes + raw.getSize());
}

// Expected REAL-UNIT mapping for bcsingle.syx, derived once from
// `python timbre-lib/virus_patch.py --dump timbre-lib/testdata/virus/bcsingle.syx`
// (raw bytes) + the real-unit converters in VirusSysexImport.cpp. The raw
// bytes are the Python-verified ground truth; the converters are pinned by
// this test so a future formula change is caught.
struct ExpectedMapping {
    int index;
    float value;
};

} // namespace

TEST(VirusSysexImport, ParseBcSingleFixture)
{
    auto data = readFixture("bcsingle.syx", 267);
    auto patch = parseBcSingle(data.data(), data.size());

    ASSERT_TRUE(patch.has_value());
    EXPECT_TRUE(patch->isValid);
    EXPECT_EQ(patch->name, "~WELCOME");  // leading '~' (0x7E) is kept
    EXPECT_EQ(patch->bank, 1);
    EXPECT_EQ(patch->program, 0);

    int mappedCount = 0;
    for (const auto& v : patch->mapped)
        if (v.has_value())
            ++mappedCount;
    EXPECT_EQ(mappedCount, 23);          // params 0..22; 23 (Pitch Bend) reserved

    ASSERT_EQ(patch->unmapped.size(), 12u);
    EXPECT_EQ(patch->unmapped[0], "osc2_fm_amount");
    EXPECT_EQ(patch->unmapped[1], "ring_mod");
    EXPECT_EQ(patch->unmapped[2], "lfo1");
    EXPECT_EQ(patch->unmapped[3], "lfo2");
    EXPECT_EQ(patch->unmapped[4], "keytrack");
    EXPECT_EQ(patch->unmapped[5], "filter_slope_24db");
    EXPECT_EQ(patch->unmapped[6], "osc_sync");
    EXPECT_EQ(patch->unmapped[7], "fx_chorus");
    EXPECT_EQ(patch->unmapped[8], "fx_delay");
    EXPECT_EQ(patch->unmapped[9], "fx_reverb");
    EXPECT_EQ(patch->unmapped[10], "mod_matrix");
    EXPECT_EQ(patch->unmapped[11], "noise_level");
}

TEST(VirusSysexImport, ParseTiBankSlice)
{
    auto block = readFixture("tiblock0.syx", 524);

    // A single self-contained TI block parses to one patch.
    auto single = parseTiBank(block.data(), block.size());
    ASSERT_EQ(single.size(), 1u);
    EXPECT_TRUE(single[0].isValid);
    EXPECT_EQ(single[0].name, "WCOG");
    EXPECT_TRUE(single[0].mapped[7].has_value());  // cutoff read from the page

    // A full 128-block bank (128 x 524 = 67072 bytes) slices to 128 patches.
    std::vector<uint8_t> bank;
    bank.reserve(128 * 524);
    for (int i = 0; i < 128; ++i)
        bank.insert(bank.end(), block.begin(), block.end());
    ASSERT_EQ(bank.size(), 67072u);

    auto patches = parseTiBank(bank.data(), bank.size());
    ASSERT_EQ(patches.size(), 128u);
    for (size_t i = 0; i < patches.size(); ++i)
    {
        EXPECT_TRUE(patches[i].isValid) << "patch " << i << " invalid";
        EXPECT_EQ(patches[i].name, "WCOG") << "patch " << i << " name";
    }
}

TEST(VirusSysexImport, RejectsBadSize)
{
    auto data = readFixture("bcsingle.syx", 267);
    EXPECT_FALSE(parseBcSingle(data.data(), 266).has_value());
    EXPECT_FALSE(parseBcSingle(data.data(), 268).has_value());

    // TI bank size must be a positive multiple of 524.
    std::vector<uint8_t> bad(525, 0);
    EXPECT_TRUE(parseTiBank(bad.data(), bad.size()).empty());
    EXPECT_TRUE(parseTiBank(nullptr, 0).empty());
}

TEST(VirusSysexImport, RejectsBadHeader)
{
    auto data = readFixture("bcsingle.syx", 267);
    data[1] = 0x01;  // corrupt the Access manufacturer header
    EXPECT_FALSE(parseBcSingle(data.data(), data.size()).has_value());

    // Not a single dump (cmd byte must be 0x10).
    auto cmd = readFixture("bcsingle.syx", 267);
    cmd[6] = 0x11;
    EXPECT_FALSE(parseBcSingle(cmd.data(), cmd.size()).has_value());

    // Missing F7 terminator.
    auto f7 = readFixture("bcsingle.syx", 267);
    f7[266] = 0x00;
    EXPECT_FALSE(parseBcSingle(f7.data(), f7.size()).has_value());
}

TEST(VirusSysexImport, RejectsBadChecksum)
{
    auto data = readFixture("bcsingle.syx", 267);
    data[9 + 40] ^= 0x01;  // flip a payload byte -> stored checksum mismatches
    EXPECT_FALSE(parseBcSingle(data.data(), data.size()).has_value());

    auto block = readFixture("tiblock0.syx", 524);
    block[9 + 40] ^= 0x01;
    EXPECT_TRUE(parseTiBank(block.data(), block.size()).empty());

    // A corrupt block inside a bank rejects the WHOLE bank (never partial).
    std::vector<uint8_t> bank;
    bank.reserve(2 * 524);
    auto good = readFixture("tiblock0.syx", 524);
    bank.insert(bank.end(), good.begin(), good.end());
    auto bad = good;
    bad[9 + 20] ^= 0x01;
    bank.insert(bank.end(), bad.begin(), bad.end());
    EXPECT_TRUE(parseTiBank(bank.data(), bank.size()).empty());
}

// Gate 5: the C++ real-unit mapping must match the Python decoder's raw bytes
// (from `python virus_patch.py --dump`) converted to real units per the
// documented formulas. Hard-coded below within tolerance.
TEST(VirusSysexImport, MappingMatchesPythonDecoder)
{
    auto data = readFixture("bcsingle.syx", 267);
    auto patch = parseBcSingle(data.data(), data.size());
    ASSERT_TRUE(patch.has_value());

    const ExpectedMapping kExpected[] = {
        { 0,  1.0f        },  // osc1_wave    raw 0   -> Saw (sub wave 1)
        { 1,  0.503937f   },  // osc1_level   raw 64  -> 64/127
        { 2,  1.0f        },  // osc2_wave    raw 0   -> Saw
        { 3,  0.503937f   },  // osc2_level   raw 64  -> 64/127
        { 4,  98.4375f    },  // osc2_detune  raw 127 -> (127-64)*100/64
        { 5,  0.0f        },  // sub_level    raw 0
        { 6,  -1.0f       },  // sub_octave   raw 0   -> -1 (Virus sub sits 1 oct below)
        { 7,  86.8611f    },  // cutoff       raw 27  -> 20*pow(1000, 27/127) Hz
        { 8,  0.196850f   },  // resonance    raw 25  -> 25/127
        { 9,  0.0f        },  // drive        raw 0   -> 0/6
        { 10, 0.001f      },  // amp_attack   raw 0   -> 0.001*pow(5000, 0/127) s
        { 11, 5.0f        },  // amp_decay    raw 127 -> 0.001*pow(5000, 1) s
        { 12, 1.0f        },  // amp_sustain  raw 127 -> 127/127
        { 13, 0.052290f   },  // amp_release  raw 59  -> 0.001*pow(5000, 59/127) s
        { 14, 1.181102f   },  // output       raw 100 -> 100/127*1.5
        { 15, 0.0f        },  // legato       raw 0   -> key_mode==0 -> 0
        { 16, 0.0f        },  // portamento   raw 0   -> 0/127*5
        { 17, 0.0f        },  // filter_type  raw 0   -> min(0,3)
        { 18, 14.25f      },  // filter_env_amt raw 83 -> (83-64)/64*48 semis
        { 19, 0.213826f   },  // filter_env_attack raw 80 -> 0.001*pow(5000, 80/127)
        { 20, 0.764626f   },  // filter_env_decay  raw 99 -> 0.001*pow(5000, 99/127)
        { 21, 0.0f        },  // filter_env_sustain raw 0 -> 0/127
        { 22, 5.0f        },  // filter_env_release raw 127 -> 0.001*pow(5000, 1)
    };

    for (const auto& e : kExpected)
    {
        ASSERT_TRUE(patch->mapped[static_cast<size_t>(e.index)].has_value())
            << "param " << e.index << " not mapped";
        EXPECT_NEAR(*patch->mapped[static_cast<size_t>(e.index)], e.value, 1e-3f)
            << "param " << e.index << " mismatch";
    }

    // Param 23 (Pitch Bend Range) is reserved — never written by the loader.
    EXPECT_FALSE(patch->mapped[23].has_value());
}

// Gate 9: a bad file must fail cleanly at the parser boundary (covered above);
// the command-level validation is exercised by the TrackFxRebuildRace suite.
TEST(VirusSysexImport, ParseDoesNotReadOutOfRange)
{
    // A page shorter than the smallest source offset (5) still parses — the
    // offset guard never reads out of range.
    uint8_t tiny[4] = { 0 };
    auto patch = mapToSubSynth(tiny, sizeof(tiny), "tiny");
    EXPECT_TRUE(patch.isValid);
    EXPECT_EQ(patch.name, "tiny");
    bool anyMapped = false;
    for (const auto& v : patch.mapped)
        if (v.has_value())
            anyMapped = true;
    EXPECT_FALSE(anyMapped);  // no source offset < 4 exists
    EXPECT_EQ(patch.unmapped.size(), 12u);
}