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
                       .getChildFile("hdaw_clip_streaming_test.wav");
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
        out.release();
        writer->writeFromAudioSampleBuffer(buf, 0, lengthSamples);
    }
    return f;
}

} // namespace

// The streamer is created fresh per rebuild (rebuildClipsForTrack makes a new
// ClipSourceProcessor each time, RoutingManager.cpp:507). The contract to
// assert is: a LONG clip that must stream is actually created in streaming
// mode (isWholeFileResident() == false) and a SHORT one preloads. We assert
// on the processor the RoutingManager creates by reaching into a rebuilt
// project. Rather than duplicating the full routing fixture, assert the
// decision logic at the unit level: a >8s file must NOT be whole-file
// resident in a freshly-prepared StreamingClipSource.
TEST(ClipStreamingE2E, LongFileNotWholeFileResident)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    auto file = writeSineWav(static_cast<int>(44100.0 * 30), 44100.0);
    HDAW::StreamingClipSource src;
    src.prepare(file, fm, 44100.0, 512);
    EXPECT_FALSE(src.isWholeFileResident());
    EXPECT_GT(src.sourceLength(), 0);
    file.deleteFile();
}