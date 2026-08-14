#include <gtest/gtest.h>
#include "engine/DecodedSoundPool.h"
#include "engine/ClipSourceProcessor.h"
#include "engine/TransportManager.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <fstream>

namespace {

juce::File writeSineWav(const char* tag, int lengthSamples, double sr = 44100.0)
{
    const int numChannels = 1;
    const int bitsPerSample = 16;
    const int bytesPerSample = bitsPerSample / 8;
    const int dataSize = lengthSamples * numChannels * bytesPerSample;

    juce::File file = juce::File::getCurrentWorkingDirectory()
        .getChildFile(juce::String("pool_test_") + tag + ".wav");
    file.deleteFile();
    std::ofstream out(file.getFullPathName().toStdString(), std::ios::binary);

    auto writeChunk = [&](const char* id, const void* data, int size)
    {
        out.write(id, 4);
        out.write(reinterpret_cast<const char*>(&size), 4);
        out.write(static_cast<const char*>(data), size);
    };

    int sampleRate = static_cast<int>(sr);
    int byteRate = sampleRate * numChannels * bytesPerSample;
    int blockAlign = numChannels * bytesPerSample;
    out.write("RIFF", 4);
    int riffSize = 36 + dataSize;
    out.write(reinterpret_cast<const char*>(&riffSize), 4);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    int fmtSize = 16;
    short audioFormat = 1;
    short channels = static_cast<short>(numChannels);
    out.write(reinterpret_cast<const char*>(&fmtSize), 4);
    out.write(reinterpret_cast<const char*>(&audioFormat), 2);
    out.write(reinterpret_cast<const char*>(&channels), 2);
    out.write(reinterpret_cast<const char*>(&sampleRate), 4);
    out.write(reinterpret_cast<const char*>(&byteRate), 4);
    out.write(reinterpret_cast<const char*>(&blockAlign), 2);
    short bits = bitsPerSample;
    out.write(reinterpret_cast<const char*>(&bits), 2);
    out.write("data", 4);
    out.write(reinterpret_cast<const char*>(&dataSize), 4);
    for (int i = 0; i < lengthSamples; ++i)
    {
        short v = static_cast<short>(std::sin(2.0 * 3.14159 * 440.0 * i / sampleRate) * 32000.0);
        out.write(reinterpret_cast<const char*>(&v), 2);
    }
    out.close();
    return file;
}

} // namespace

TEST(AudioPoolDedup, SameFileReturnsSameSoundAndDecodesOnce)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::DecodedSoundPool pool(fm);

    auto file = writeSineWav("share", 44100);
    auto a = pool.acquire(file.getFullPathName());
    auto b = pool.acquire(file.getFullPathName());

    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(a.get(), b.get());            // shared_ptr equality — one DecodedSound
    EXPECT_EQ(pool.getDecodeCount(), 1);    // decode-count == 1
    EXPECT_EQ(pool.getEntryCount(), 1);
    file.deleteFile();
}

TEST(AudioPoolDedup, DifferentFilesDecodeSeparately)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::DecodedSoundPool pool(fm);

    auto fileA = writeSineWav("diff_a", 44100);
    auto fileB = writeSineWav("diff_b", 44100);
    auto a = pool.acquire(fileA.getFullPathName());
    auto b = pool.acquire(fileB.getFullPathName());

    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_NE(a.get(), b.get());
    EXPECT_EQ(pool.getDecodeCount(), 2);
    fileA.deleteFile();
    fileB.deleteFile();
}

TEST(AudioPoolDedup, MissingFileReturnsNull)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::DecodedSoundPool pool(fm);

    auto sound = pool.acquire("C:/definitely/not/here.wav");
    EXPECT_EQ(sound, nullptr);
    EXPECT_EQ(pool.getDecodeCount(), 0);
}

TEST(AudioPoolDedup, RefcountDropsAndEntryEvictsWhenUnreferenced)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::DecodedSoundPool pool(fm);

    auto file = writeSineWav("refcount", 44100);
    {
        auto a = pool.acquire(file.getFullPathName());
        ASSERT_NE(a, nullptr);
        EXPECT_EQ(pool.getEntryCount(), 1);
    } // last consumer released
    pool.pruneUnreferenced();
    EXPECT_EQ(pool.getEntryCount(), 0);     // evicted — nothing references it

    // Re-acquire after eviction re-decodes (genuinely unused in between).
    auto b = pool.acquire(file.getFullPathName());
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(pool.getDecodeCount(), 2);
    file.deleteFile();
}

TEST(AudioPoolDedup, ReferencedEntrySurvivesPrune)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::DecodedSoundPool pool(fm);

    auto file = writeSineWav("keep", 44100);
    auto a = pool.acquire(file.getFullPathName());
    ASSERT_NE(a, nullptr);
    pool.pruneUnreferenced();
    EXPECT_EQ(pool.getEntryCount(), 1);     // still referenced → not evicted
    EXPECT_EQ(pool.getDecodeCount(), 1);
    file.deleteFile();
}

TEST(AudioPoolDedup, MonoDataMatchesDecodedSamples)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::DecodedSoundPool pool(fm);

    auto file = writeSineWav("data", 1024);
    auto sound = pool.acquire(file.getFullPathName());
    ASSERT_NE(sound, nullptr);
    EXPECT_EQ(sound->numChannels, 1);
    EXPECT_EQ(sound->length, 1024);
    // Sine starts at phase zero, so sample 0 is exactly 0.0f by construction;
    // scan for a nonzero sample to prove the decode carries real audio.
    bool anyNonZero = false;
    for (int64_t i = 0; i < sound->length && !anyNonZero; ++i)
        anyNonZero = std::abs(sound->data[0][i]) > 0.0f;
    EXPECT_TRUE(anyNonZero);
    file.deleteFile();
}

TEST(AudioPoolDedup, TwoClipProcessorsShareOneDecode)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::DecodedSoundPool pool(fm);
    HDAW::TransportManager tm;

    auto file = writeSineWav("proc_share", 44100);
    auto path = file.getFullPathName();

    HDAW::ClipSourceProcessor a(tm, fm, &pool);
    HDAW::ClipSourceProcessor b(tm, fm, &pool);
    a.setSourceFile(path);
    b.setSourceFile(path);
    a.prepareToPlay(44100.0, 512);
    b.prepareToPlay(44100.0, 512);

    EXPECT_EQ(pool.getDecodeCount(), 1);    // one decode, two consumers
    EXPECT_EQ(pool.getEntryCount(), 1);

    // Both processors read the SAME pooled buffer (pointer identity).
    EXPECT_EQ(a.getPreloadedDataForTest(0), b.getPreloadedDataForTest(0));
    file.deleteFile();
}

TEST(AudioPoolDedup, ProcessorWithoutPoolFallsBackToDirectDecode)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::TransportManager tm;

    auto file = writeSineWav("proc_fallback", 44100);
    auto path = file.getFullPathName();

    HDAW::ClipSourceProcessor a(tm, fm); // no pool
    a.setSourceFile(path);
    a.prepareToPlay(44100.0, 512);
    EXPECT_NE(a.getPreloadedDataForTest(0), nullptr);
    file.deleteFile();
}
