#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "engine/MidiImport.h"
#include "model/ProjectModel.h"
#include <juce_core/juce_core.h>
#include <fstream>

namespace
{

std::string writeTestMidiFile()
{
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    char path[MAX_PATH];
    GetTempFileNameA(tempPath, "mid", 0, path);

    const uint8_t midiData[] = {
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 1,0xE0,
        'M','T','r','k', 0,0,0,20,
        0, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,
        0, 0x90, 60, 100,
        0x01,0xE0, 0x80, 60, 0,
        0, 0xFF, 0x2F, 0
    };

    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(midiData), sizeof(midiData));
    f.close();
    return std::string(path);
}

TEST(MidiImportTest, ImportIntoExistingTrackCreatesClipWithNotes)
{
    AudioEngine engine;
    engine.initialize();

    int trackIdx = engine.getProjectCommands().addTrack("Target", -1, -1, 1);
    ASSERT_GE(trackIdx, 0);

    auto midiPath = writeTestMidiFile();
    auto clipIds = HDAW::importMidiFile(engine, QString::fromStdString(midiPath), trackIdx);

    ASSERT_EQ(clipIds.size(), 1u);

    auto& model = engine.getProjectModel();
    auto trackList = model.getTrackListTree();
    auto track = trackList.getChild(trackIdx);
    auto clipList = track.getChildWithName(IDs::CLIP_LIST);
    ASSERT_EQ(clipList.getNumChildren(), 1);

    auto clip = clipList.getChild(0);
    auto notes = clip.getChildWithName(IDs::MIDI_NOTE_LIST);
    EXPECT_GT(notes.getNumChildren(), 0);

    juce::File(midiPath).deleteFile();
}

TEST(MidiImportTest, ImportIntoNewTracksCreatesTracksAndClips)
{
    AudioEngine engine;
    engine.initialize();

    auto midiPath = writeTestMidiFile();
    auto clipIds = HDAW::importMidiFile(engine, QString::fromStdString(midiPath), -1);

    ASSERT_EQ(clipIds.size(), 1u);

    auto& model = engine.getProjectModel();
    auto trackList = model.getTrackListTree();
    EXPECT_GE(trackList.getNumChildren(), 1);

    juce::File(midiPath).deleteFile();
}

TEST(MidiImportTest, ImportNonexistentFileReturnsEmpty)
{
    AudioEngine engine;
    engine.initialize();

    auto clipIds = HDAW::importMidiFile(engine, "C:/nonexistent/path.mid", 0);
    EXPECT_TRUE(clipIds.empty());
}

} // namespace
