#include <gtest/gtest.h>
#include "engine/MidiAnalyzer.h"
#include "engine/PhraseGenerator.h"
#include <juce_core/juce_core.h>
#include <fstream>
#include <windows.h>

namespace
{

void writeVarLen(std::vector<uint8_t>& buf, uint32_t value)
{
    if (value < 0x80) {
        buf.push_back(static_cast<uint8_t>(value));
        return;
    }
    std::vector<uint8_t> bytes;
    bytes.push_back(static_cast<uint8_t>(value & 0x7F));
    value >>= 7;
    while (value > 0) {
        bytes.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    for (auto it = bytes.rbegin(); it != bytes.rend(); ++it)
        buf.push_back(*it);
}

std::string writeTestMidiFile(const std::vector<std::tuple<int, int, double, double>>& notes,
                               double bpm = 120.0)
{
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    char path[MAX_PATH];
    GetTempFileNameA(tempPath, "mid", 0, path);

    const int ticksPerQuarter = 480;

    // Build note-on/off events with proper delta times
    struct MidiEvent { uint32_t tick; bool isNoteOn; int pitch; int velocity; };
    std::vector<MidiEvent> events;
    for (const auto& [pitch, vel, start, dur] : notes) {
        uint32_t onTick = static_cast<uint32_t>(start * ticksPerQuarter);
        uint32_t offTick = static_cast<uint32_t>((start + dur) * ticksPerQuarter);
        events.push_back({ onTick, true, pitch, vel });
        events.push_back({ offTick, false, pitch, 0 });
    }
    std::sort(events.begin(), events.end(),
        [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });

    std::vector<uint8_t> data;

    const uint8_t header[] = {
        'M','T','h','d', 0,0,0,6, 0,0, 0,1,
        static_cast<uint8_t>((ticksPerQuarter >> 8) & 0xFF),
        static_cast<uint8_t>(ticksPerQuarter & 0xFF)
    };
    data.insert(data.end(), header, header + sizeof(header));

    std::vector<uint8_t> trackData;

    // Tempo meta event
    uint32_t usPerQuarter = static_cast<uint32_t>(60000000.0 / bpm);
    writeVarLen(trackData, 0);
    trackData.insert(trackData.end(), { 0xFF, 0x51, 0x03 });
    trackData.push_back((usPerQuarter >> 16) & 0xFF);
    trackData.push_back((usPerQuarter >> 8) & 0xFF);
    trackData.push_back(usPerQuarter & 0xFF);

    // Note events with proper delta times
    uint32_t lastTick = 0;
    for (const auto& ev : events) {
        uint32_t delta = ev.tick - lastTick;
        lastTick = ev.tick;
        writeVarLen(trackData, delta);
        if (ev.isNoteOn) {
            trackData.push_back(0x90);
            trackData.push_back(static_cast<uint8_t>(ev.pitch));
            trackData.push_back(static_cast<uint8_t>(ev.velocity));
        } else {
            trackData.push_back(0x80);
            trackData.push_back(static_cast<uint8_t>(ev.pitch));
            trackData.push_back(0);
        }
    }

    // End of track
    writeVarLen(trackData, 0);
    trackData.insert(trackData.end(), { 0xFF, 0x2F, 0 });

    data.insert(data.end(), { 'M','T','r','k' });
    uint32_t trackLen = static_cast<uint32_t>(trackData.size());
    data.push_back((trackLen >> 24) & 0xFF);
    data.push_back((trackLen >> 16) & 0xFF);
    data.push_back((trackLen >> 8) & 0xFF);
    data.push_back(trackLen & 0xFF);
    data.insert(data.end(), trackData.begin(), trackData.end());

    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()), data.size());
    f.close();
    return std::string(path);
}

TEST(MidiAnalyzerTest, AnalyzeNonexistentFileReturnsEmpty)
{
    auto result = HDAW::MidiAnalyzer::analyze(juce::File("C:/nonexistent/path.mid"));
    EXPECT_TRUE(result.fileName.isEmpty());
    EXPECT_EQ(result.trackCount, 0);
}

TEST(MidiAnalyzerTest, AnalyzeSimpleScaleReturnsFingerprint)
{
    std::vector<std::tuple<int, int, double, double>> notes = {
        {60, 100, 0.0, 0.5},
        {62, 100, 0.5, 0.5},
        {64, 100, 1.0, 0.5},
        {65, 100, 1.5, 0.5},
        {67, 100, 2.0, 0.5},
        {69, 100, 2.5, 0.5},
        {71, 100, 3.0, 0.5},
        {72, 100, 3.5, 0.5},
    };

    auto path = writeTestMidiFile(notes);
    auto file = juce::File(path);
    auto result = HDAW::MidiAnalyzer::analyze(file);

    EXPECT_EQ(result.trackCount, 1);
    EXPECT_GT(result.fingerprint.avgNoteDensity, 0.0);
    EXPECT_GT(result.fingerprint.pitchRange, 0);
    EXPECT_GE(result.fingerprint.scaleType, 0);
    EXPECT_NEAR(result.fingerprint.avgVelocity, 100.0 / 127.0, 0.05);

    file.deleteFile();
}

TEST(MidiAnalyzerTest, AnalyzeChordReturnsHighPolyphony)
{
    std::vector<std::tuple<int, int, double, double>> notes = {
        {60, 100, 0.0, 1.0},
        {64, 100, 0.0, 1.0},
        {67, 100, 0.0, 1.0},
        {60, 100, 1.0, 1.0},
        {64, 100, 1.0, 1.0},
        {67, 100, 1.0, 1.0},
    };

    auto path = writeTestMidiFile(notes);
    auto result = HDAW::MidiAnalyzer::analyze(juce::File(path));

    EXPECT_GE(result.fingerprint.avgPolyphony, 2.5);
    EXPECT_EQ(result.fingerprint.voiceCount, 1);

    juce::File(path).deleteFile();
}

TEST(MidiAnalyzerTest, RepeatedBarsAppearAsPatterns)
{
    std::vector<std::tuple<int, int, double, double>> notes;
    auto addBar = [&](int barStart, int pitch) {
        notes.push_back({pitch, 100, barStart + 0.0, 0.25});
        notes.push_back({pitch + 4, 100, barStart + 0.5, 0.25});
        notes.push_back({pitch + 7, 100, barStart + 1.0, 0.25});
        notes.push_back({pitch + 12, 80, barStart + 1.5, 0.5});
    };

    addBar(0.0, 60);
    addBar(4.0, 62);
    addBar(8.0, 60);
    addBar(12.0, 65);

    auto path = writeTestMidiFile(notes);
    auto result = HDAW::MidiAnalyzer::analyze(juce::File(path));

    bool foundRepeated = false;
    for (const auto& p : result.patterns) {
        if (!p.isMotif && p.frequency >= 2.0) {
            foundRepeated = true;
            break;
        }
    }
    EXPECT_TRUE(foundRepeated);

    juce::File(path).deleteFile();
}

TEST(MidiAnalyzerTest, StyleClassificationForHighDensity)
{
    HDAW::MidiFingerprint fp;
    fp.avgNoteDensity = 12.0;
    fp.pitchRange = 5;
    fp.avgNoteDuration = 0.1;
    fp.rhythmComplexity = 0.7;

    int style = HDAW::MidiAnalyzer::classifyStyle(fp);
    EXPECT_EQ(style, PhraseGenerator::TrapHiHat);
}

TEST(MidiAnalyzerTest, StyleClassificationForPad)
{
    HDAW::MidiFingerprint fp;
    fp.avgNoteDensity = 1.5;
    fp.pitchRange = 36;
    fp.avgNoteDuration = 4.0;

    int style = HDAW::MidiAnalyzer::classifyStyle(fp);
    EXPECT_EQ(style, PhraseGenerator::Pad);
}

TEST(MidiAnalyzerTest, StyleClassificationForWalkingBass)
{
    HDAW::MidiFingerprint fp;
    fp.avgNoteDensity = 4.0;
    fp.pitchRange = 18;
    fp.swingAmount = 0.5;
    fp.rhythmComplexity = 0.2;
    fp.avgPolyphony = 1.0;

    int style = HDAW::MidiAnalyzer::classifyStyle(fp);
    EXPECT_EQ(style, PhraseGenerator::WalkingBass);
}

TEST(MidiAnalyzerTest, ToPatternJsonReturnsNonEmpty)
{
    std::vector<std::tuple<int, int, double, double>> notes = {
        {60, 100, 0.0, 0.5},
        {64, 100, 0.5, 0.5},
        {67, 100, 1.0, 0.5},
        {72, 100, 1.5, 0.5},
    };
    auto path = writeTestMidiFile(notes);
    auto result = HDAW::MidiAnalyzer::analyze(juce::File(path));

    auto [params, styleParams] = HDAW::MidiAnalyzer::toPatternJson(result);
    EXPECT_FALSE(params.isEmpty());
    EXPECT_FALSE(styleParams.isEmpty());

    juce::File(path).deleteFile();
}

} // namespace
