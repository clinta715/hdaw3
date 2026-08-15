#include <gtest/gtest.h>
#include "engine/StreamingSoundPool.h"
#include "engine/StreamingClipSource.h"
#include "engine/ClipSourceProcessor.h"
#include "engine/TransportManager.h"
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <fstream>

namespace {

juce::File writeSineWav(const char* tag, int lengthSamples, double sr = 44100.0,
                        int numChannels = 1)
{
    const int bitsPerSample = 16;
    const int bytesPerSample = bitsPerSample / 8;
    const int dataSize = lengthSamples * numChannels * bytesPerSample;

    juce::File file = juce::File::getCurrentWorkingDirectory()
        .getChildFile(juce::String("stream_pool_") + tag + ".wav");
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
        for (int ch = 0; ch < numChannels; ++ch)
            out.write(reinterpret_cast<const char*>(&v), 2);
    }
    out.close();
    return file;
}

} // namespace

TEST(StreamingSoundPool, SameFileReturnsSameHandleAndOpensOnce)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::StreamingSoundPool pool(fm);

    auto file = writeSineWav("share", 44100);
    auto a = pool.acquire(file.getFullPathName());
    auto b = pool.acquire(file.getFullPathName());

    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(a.get(), b.get());
    EXPECT_EQ(pool.getOpenCount(), 1);
    EXPECT_EQ(pool.getEntryCount(), 1);
    file.deleteFile();
}

TEST(StreamingSoundPool, DifferentFilesOpenSeparately)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::StreamingSoundPool pool(fm);

    auto fileA = writeSineWav("diff_a", 44100);
    auto fileB = writeSineWav("diff_b", 44100);
    auto a = pool.acquire(fileA.getFullPathName());
    auto b = pool.acquire(fileB.getFullPathName());

    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_NE(a.get(), b.get());
    EXPECT_EQ(pool.getOpenCount(), 2);
    fileA.deleteFile();
    fileB.deleteFile();
}

TEST(StreamingSoundPool, MissingFileReturnsNull)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::StreamingSoundPool pool(fm);

    auto handle = pool.acquire("C:/definitely/not/here.wav");
    EXPECT_EQ(handle, nullptr);
    EXPECT_EQ(pool.getOpenCount(), 0);
}

TEST(StreamingSoundPool, RefcountDropsAndEntryEvictsWhenUnreferenced)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::StreamingSoundPool pool(fm);

    auto file = writeSineWav("refcount", 44100);
    {
        auto a = pool.acquire(file.getFullPathName());
        ASSERT_NE(a, nullptr);
        EXPECT_EQ(pool.getEntryCount(), 1);
    }
    pool.pruneUnreferenced();
    EXPECT_EQ(pool.getEntryCount(), 0);

    auto b = pool.acquire(file.getFullPathName());
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(pool.getOpenCount(), 2);
    file.deleteFile();
}

TEST(StreamingSoundPool, ReferencedEntrySurvivesPrune)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::StreamingSoundPool pool(fm);

    auto file = writeSineWav("keep", 44100);
    auto a = pool.acquire(file.getFullPathName());
    ASSERT_NE(a, nullptr);
    pool.pruneUnreferenced();
    EXPECT_EQ(pool.getEntryCount(), 1);
    EXPECT_EQ(pool.getOpenCount(), 1);
    file.deleteFile();
}

TEST(StreamingSoundPool, ConcurrentDivergentReadersThroughSharedHandle)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::StreamingSoundPool pool(fm);

    auto file = writeSineWav("divergent", 44100 * 30);
    auto handle = pool.acquire(file.getFullPathName());
    ASSERT_NE(handle, nullptr);

    HDAW::StreamingClipSource a;
    HDAW::StreamingClipSource b;
    a.prepare(handle, 44100.0, 512);
    b.prepare(handle, 44100.0, 512);
    ASSERT_TRUE(a.isOk());
    ASSERT_TRUE(b.isOk());
    EXPECT_FALSE(a.isWholeFileResident());
    EXPECT_FALSE(b.isWholeFileResident());
    a.setNonRealtime(true);
    b.setNonRealtime(true);
    a.startPlayback();
    b.startPlayback();

    const int blockSize = 512;
    const int totalBlocks = 200;
    const int64_t offset = static_cast<int64_t>(44100) * 15;
    juce::AudioBuffer<float> outA(2, blockSize);
    juce::AudioBuffer<float> outB(2, blockSize);
    for (int i = 0; i < totalBlocks; ++i)
    {
        outA.clear();
        outB.clear();
        a.readNextBlock(outA, static_cast<int64_t>(i) * blockSize);
        b.readNextBlock(outB, offset + static_cast<int64_t>(i) * blockSize);
        EXPECT_GT(outA.getMagnitude(0, 0, blockSize), 0.05f) << "reader A silent at block " << i;
        EXPECT_GT(outB.getMagnitude(0, 0, blockSize), 0.05f) << "reader B silent at block " << i;
    }
    file.deleteFile();
}

TEST(StreamingSoundPool, PrivateFallbackWithoutPoolMatches)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::StreamingSoundPool pool(fm);

    auto file = writeSineWav("fallback", 44100 * 9);
    auto handle = pool.acquire(file.getFullPathName());
    ASSERT_NE(handle, nullptr);

    HDAW::StreamingClipSource pooled;
    HDAW::StreamingClipSource priv;
    pooled.prepare(handle, 44100.0, 512);
    priv.prepare(file, fm, 44100.0, 512);
    ASSERT_TRUE(pooled.isOk());
    ASSERT_TRUE(priv.isOk());
    EXPECT_FALSE(pooled.isWholeFileResident());
    EXPECT_FALSE(priv.isWholeFileResident());
    pooled.setNonRealtime(true);
    priv.setNonRealtime(true);
    pooled.startPlayback();
    priv.startPlayback();

    const int blockSize = 512;
    const int totalBlocks = 8;
    juce::AudioBuffer<float> outP(2, blockSize);
    juce::AudioBuffer<float> outF(2, blockSize);
    for (int i = 0; i < totalBlocks; ++i)
    {
        outP.clear();
        outF.clear();
        pooled.readNextBlock(outP, static_cast<int64_t>(i) * blockSize);
        priv.readNextBlock(outF, static_cast<int64_t>(i) * blockSize);
        for (int s = 0; s < blockSize; ++s)
            EXPECT_NEAR(outP.getSample(0, s), outF.getSample(0, s), 4.0 / 32768.0)
                << "mismatch at block " << i << " sample " << s;
    }
    file.deleteFile();
}

TEST(StreamingPoolDedup, TwoClipProcessorsShareOneStreamingHandle)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::StreamingSoundPool pool(fm);
    HDAW::TransportManager tm;

    auto file = writeSineWav("proc_share", 44100 * 9);
    auto path = file.getFullPathName();

    HDAW::ClipSourceProcessor a(tm, fm, nullptr, &pool);
    HDAW::ClipSourceProcessor b(tm, fm, nullptr, &pool);
    a.setSourceFile(path);
    b.setSourceFile(path);
    a.prepareToPlay(44100.0, 512);
    b.prepareToPlay(44100.0, 512);

    EXPECT_EQ(pool.getOpenCount(), 1);
    EXPECT_EQ(pool.getEntryCount(), 1);

    auto ha = a.getStreamingHandleForTest();
    auto hb = b.getStreamingHandleForTest();
    ASSERT_NE(ha, nullptr);
    ASSERT_NE(hb, nullptr);
    EXPECT_EQ(ha.get(), hb.get());
    file.deleteFile();
}

TEST(StreamingPoolDedup, ProcessorWithoutStreamingPoolFallsBackToPrivateHandle)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::TransportManager tm;

    auto file = writeSineWav("priv_fallback", 44100 * 9);
    auto path = file.getFullPathName();

    HDAW::ClipSourceProcessor proc(tm, fm);
    proc.setSourceFile(path);
    proc.prepareToPlay(44100.0, 512);

    auto h = proc.getStreamingHandleForTest();
    ASSERT_NE(h, nullptr);
    file.deleteFile();
}

TEST(StreamingPoolDedup, ShortFileBypassesStreamingPool)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::DecodedSoundPool dpool(fm);
    HDAW::StreamingSoundPool spool(fm);
    HDAW::TransportManager tm;

    auto file = writeSineWav("short", 44100);
    auto path = file.getFullPathName();

    HDAW::ClipSourceProcessor proc(tm, fm, &dpool, &spool);
    proc.setSourceFile(path);
    proc.prepareToPlay(44100.0, 512);

    EXPECT_EQ(spool.getOpenCount(), 0);
    EXPECT_EQ(dpool.getDecodeCount(), 1);
    EXPECT_EQ(proc.getStreamingHandleForTest(), nullptr);
    EXPECT_NE(proc.getPreloadedDataForTest(0), nullptr);
    file.deleteFile();
}

TEST(StreamingPoolDedup, EngineWiresStreamingPoolAndRebuildReacquiresWithoutReopen)
{
    AudioEngine engine;
    engine.initialize();

    auto file = writeSineWav("engine_stream", 44100 * 9);
    const juce::String path = file.getFullPathName();

    engine.getProjectCommands().addAudioClip(0, 0.0, 1.0, path.toStdString(), "clipA");
    engine.drainPendingRoutingRebuild();

    auto& spool = engine.getProjectPool().getStreamingSoundPool();
    EXPECT_EQ(spool.getOpenCount(), 1);

    engine.getProjectCommands().addAudioClip(2, 0.0, 1.0, path.toStdString(), "clipB");
    engine.drainPendingRoutingRebuild();
    EXPECT_EQ(spool.getOpenCount(), 1);

    engine.getMainProcessor()->rebuildRoutingGraph();
    engine.getMainProcessor()->rebuildRoutingGraph();
    engine.drainPendingRoutingRebuild();
    EXPECT_EQ(spool.getOpenCount(), 1);

    auto* rm = engine.getMainProcessor()->getRoutingManager();
    ASSERT_NE(rm, nullptr);
    EXPECT_EQ(rm->getAudioClipSources().size(), 2u);

    std::shared_ptr<HDAW::StreamingSoundHandle> first;
    for (const auto& [unused, clip] : rm->getAudioClipSources())
    {
        ASSERT_NE(clip, nullptr);
        auto h = clip->getStreamingHandleForTest();
        ASSERT_NE(h, nullptr);
        if (!first)
            first = h;
        else
            EXPECT_EQ(h.get(), first.get());
    }

    file.deleteFile();
}
