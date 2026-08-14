#include <gtest/gtest.h>
#include "engine/StreamingClipSource.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <memory>

namespace {

// Writes a stereo sine .wav to a temp file and returns its path.
juce::File writeSineWav(int lengthSamples, double sr)
{
    juce::File f = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("hdaw_stream_test.wav");
    f.deleteFile();
    juce::AudioBuffer<float> buf(2, lengthSamples);
    for (int s = 0; s < lengthSamples; ++s)
    {
        float v = static_cast<float>(std::sin(2.0 * 3.14159 * 440.0 * s / sr));
        buf.setSample(0, s, v);
        buf.setSample(1, s, v);
    }
    {
        std::unique_ptr<juce::FileOutputStream> out(f.createOutputStream());
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(out.get(), sr, 2, 16, {}, 0));
        // AudioFormatWriter takes ownership of the stream.
        out.release();
        writer->writeFromAudioSampleBuffer(buf, 0, lengthSamples);
    }
    return f;
}

} // namespace

TEST(StreamingClipSource, PreloadHeadCoversFirstBlocks)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    const double sr = 44100.0;
    auto file = writeSineWav(static_cast<int>(sr * 30), sr); // 30 s file

    HDAW::StreamingClipSource src;
    src.prepare(file, fm, sr, 512);
    src.setNonRealtime(true); // deterministic: read synchronously
    src.startPlayback();

    const int blockSize = 512;
    juce::AudioBuffer<float> out(2, blockSize);
    for (int i = 0; i < 8; ++i)
    {
        out.clear();
        src.readNextBlock(out, static_cast<int64_t>(i) * blockSize);
        // Preload head alone must have covered the first blocks.
        EXPECT_GT(out.getMagnitude(0, 0, blockSize), 0.05f)
            << "block " << i << " is silent (starved too early)";
        if (out.getMagnitude(0, 0, blockSize) <= 0.05f)
            break;
    }
    file.deleteFile();
}

TEST(StreamingClipSource, BackgroundFillKeepsStreamingAhead)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    const double sr = 44100.0;
    auto file = writeSineWav(static_cast<int>(sr * 60), sr); // 60 s file

    HDAW::StreamingClipSource src;
    src.prepare(file, fm, sr, 512);
    src.startPlayback(); // background reader ON (fills preload head, then runs)

    const int blockSize = 512;
    juce::AudioBuffer<float> out(2, blockSize);
    const int totalBlocks = 500; // ~5.7 s > 4 s preload head => must refill from disk
    bool lastBlockHadSignal = false;
    for (int i = 0; i < totalBlocks; ++i)
    {
        out.clear();
        src.readNextBlock(out, static_cast<int64_t>(i) * blockSize);
        // Pace reads at ~real-time consumption so the background reader gets
        // scheduled between blocks (real playback advances 1 block per 11.6 ms;
        // this test runs far faster than that without the sleep).
        if (i % 8 == 0)
            juce::Thread::sleep(2);
        if (i == totalBlocks - 1)
            lastBlockHadSignal = out.getMagnitude(0, 0, blockSize) > 0.05f;
    }
    // If the background reader never refilled past the head, the tail blocks
    // would be silent. This assertion is what proves the reader keeps ahead.
    EXPECT_TRUE(lastBlockHadSignal) << "background reader failed to refill past the preload head";
    EXPECT_GE(src.getDiskUsagePercent(), 0.0f);
    EXPECT_LE(src.getDiskUsagePercent(), 1.0f);
    src.stopPlayback();
    file.deleteFile();
}

TEST(StreamingClipSource, ShortFilePromotesToWholeFile)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    const double sr = 44100.0;
    auto file = writeSineWav(static_cast<int>(sr * 2), sr); // 2 s file

    HDAW::StreamingClipSource src;
    src.prepare(file, fm, sr, 512);
    EXPECT_TRUE(src.isWholeFileResident());
    file.deleteFile();
}

TEST(StreamingClipSource, MissingFileYieldsSilenceNotCrash)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::StreamingClipSource src;
    src.prepare(juce::File("C:/definitely/not/here.wav"), fm, 44100.0, 512);
    src.setNonRealtime(true);
    src.startPlayback();
    juce::AudioBuffer<float> out(2, 512);
    out.clear();
    src.readNextBlock(out, 0);
    EXPECT_EQ(out.getMagnitude(0, 0, 512), 0.0f); // silence, no crash
}

TEST(StreamingClipSource, PositionDrivenSeekReadsCorrectWindow)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    const double sr = 44100.0;
    auto file = writeSineWav(static_cast<int>(sr * 30), sr); // 30 s file

    HDAW::StreamingClipSource src;
    src.prepare(file, fm, sr, 512);
    src.setNonRealtime(true); // deterministic synchronous refill
    src.startPlayback();

    // Seek deep into the file — past the 4 s preload head, so the active side
    // must be refilled at the requested position (not auto-advanced).
    const int64_t deep = static_cast<int64_t>(sr * 20); // 20 s in
    juce::AudioBuffer<float> out(2, 512);
    out.clear();
    src.readNextBlock(out, deep);
    // A 440 Hz sine at 20 s is mid-cycle — expect a non-trivial signal, not
    // silence (silence would mean the window was never refilled for the seek).
    EXPECT_GT(out.getMagnitude(0, 0, 512), 0.05f)
        << "seek into [20s,20s+512) returned silence";
    src.stopPlayback();
    file.deleteFile();
}