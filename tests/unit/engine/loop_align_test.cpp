#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "engine/AudioEngineCommands.h"
#include "engine/AudioImport.h"
#include "model/ProjectModel.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <QString>
#include <cmath>

namespace {

// Writes a 4-bar 120 BPM 4-on-floor percussion loop (8 s total, 16 kicks at
// 0.5 s intervals) to a temporary mono WAV. Returns the temp file; the caller
// deletes it when done.
juce::File makePercussionLoopWav()
{
    auto tempDir = juce::File::getSpecialLocation(
        juce::File::SpecialLocationType::tempDirectory);
    auto f = tempDir.getNonexistentChildFile("hdaw_loop_align_test", ".wav", false);

    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::FileOutputStream> fos(f.createOutputStream());
    jassert(fos != nullptr);
    std::unique_ptr<juce::AudioFormatWriter> w(
        fmt.createWriterFor(fos.get(), 44100, 1, 16, {}, 0));
    fos.release(); // writer owns it now

    const double sampleRate = 44100.0;
    const double beat = 0.5; // 120 BPM
    const int total = static_cast<int>(8.0 * sampleRate);
    juce::AudioBuffer<float> buf(1, total);
    buf.clear();
    const double amp = 0.5;
    const double freq = 55.0;
    const double decay = 0.012;
    const int burstSamples = static_cast<int>(0.04 * sampleRate);
    for (int b = 0; b < 16; ++b)
    {
        const int start = static_cast<int>(b * beat * sampleRate);
        for (int i = 0; i < burstSamples && start + i < total; ++i)
        {
            const double t = static_cast<double>(i) / sampleRate;
            buf.setSample(0, start + i, static_cast<float>(
                amp * std::sin(2.0 * juce::MathConstants<double>::pi * freq * t)
                    * std::exp(-t / decay)));
        }
    }
    w->writeFromAudioSampleBuffer(buf, 0, total);
    w.reset();
    return f;
}

} // namespace

TEST(LoopAlign, AlignClipToGridWritesStretchProps)
{
    auto wav = makePercussionLoopWav();
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    // addAudioClip takes beats; 16 beats at 120 BPM = 8 s (the loop length).
    int clipId = cmds.addAudioClip(0, 0, 16.0, wav.getFullPathName().toStdString(), "loop");
    ASSERT_GE(clipId, 0);

    auto res = cmds.alignClipToGrid(clipId);
    EXPECT_TRUE(res.ok);
    EXPECT_GE(res.bars, 1);
    EXPECT_GT(res.ratio, 0.0);
    EXPECT_GT(res.duration, 0.0);

    auto snap = engine.getReadModel().getClip(clipId);
    EXPECT_EQ(snap.stretchMode, 2);          // ManualRatio
    EXPECT_GT(snap.stretchRatio, 0.0);
    EXPECT_GT(snap.durationBeats, 0.0);

    wav.deleteFile();
}

TEST(LoopAlign, AlignClipToGridMissingFileFails)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    int clipId = cmds.addAudioClip(0, 0, 4.0, "C:/nonexistent/hdaw_loop_align_xyz.wav", "bad");
    ASSERT_GE(clipId, 0);

    auto res = cmds.alignClipToGrid(clipId);
    EXPECT_FALSE(res.ok);
    EXPECT_FALSE(res.error.empty());

    // The clip must be left untouched on failure.
    auto snap = engine.getReadModel().getClip(clipId);
    EXPECT_EQ(snap.stretchMode, 0);
}

TEST(LoopAlign, ImportAudioFileAlignsToGrid)
{
    auto wav = makePercussionLoopWav();
    AudioEngine engine;
    engine.initialize();

    int clipId = HDAW::importAudioFile(
        engine, QString::fromUtf8(wav.getFullPathName().toRawUTF8()), /*trackIdx=*/0);
    ASSERT_GE(clipId, 0);

    auto snap = engine.getReadModel().getClip(clipId);
    EXPECT_GT(snap.sourceBpm, 0.0);
    EXPECT_EQ(snap.stretchMode, 2);          // grid-aligned import
    EXPECT_GT(snap.stretchRatio, 0.0);

    wav.deleteFile();
}