#include <gtest/gtest.h>
#include "engine/StretchRenderer.h"
#include "engine/StretchCache.h"
#include "engine/AudioEngine.h"
#include "model/ProjectModel.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <QSignalSpy>
#include <QCoreApplication>
#include <thread>
#include <chrono>

namespace {

// Creates a temporary mono WAV of `seconds` length at `sampleRate`, filled
// with a 220 Hz sine at -6 dBFS. Returns the absolute path. Cleans itself
// up when the returned juce::File goes out of scope via the caller's logic.
juce::File makeSineWav(double seconds, int sampleRate = 44100)
{
    auto tempDir = juce::File::getSpecialLocation(
        juce::File::SpecialLocationType::tempDirectory);
    auto f = tempDir.getNonexistentChildFile("hdaw_stretch_test", ".wav", false);

    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::FileOutputStream> fos(f.createOutputStream());
    jassert(fos != nullptr);
    std::unique_ptr<juce::AudioFormatWriter> w(
        fmt.createWriterFor(fos.get(), sampleRate, 1, 16, {}, 0));
    fos.release(); // writer owns it now

    const int total = static_cast<int>(seconds * sampleRate);
    juce::AudioBuffer<float> buf(1, total);
    const double amp = 0.5; // -6 dBFS
    for (int i = 0; i < total; ++i)
        buf.setSample(0, i, static_cast<float>(amp * std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * i / sampleRate)));
    w->writeFromAudioSampleBuffer(buf, 0, total);
    w.reset();
    return f;
}

} // namespace

// --- StretchRenderer: a ratio of 2.0 produces roughly 2x the samples ---
TEST(StretchRenderer, DoubleRatioApproximatelyDoublesLength)
{
    auto f = makeSineWav(1.0, 44100);
    const int srcSamples = 44100;

    HDAW::StretchRenderer r;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    // Capture only metadata (Result is non-copyable due to HeapBlock members).
    struct Captured { std::atomic<bool> success{ false }; std::atomic<int64_t> length{ 0 }; } cap;
    std::atomic<bool> done{ false };
    r.onComplete = [&](const HDAW::StretchRenderer::Result& res)
    {
        cap.success.store(res.success);
        cap.length.store(res.length);
        done.store(true);
    };

    ASSERT_TRUE(r.startRender(f.getFullPathName(), 2.0, 44100.0, fm));

    // Spin until done (bounded).
    for (int i = 0; i < 1000 && !done.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    EXPECT_TRUE(done.load()) << "render did not complete in time";
    EXPECT_TRUE(cap.success.load());
    // Pitch-preserving stretch at ratio 2.0: expect ~2x samples within
    // SoundTouch's processing latency (a few hundred samples slop).
    const int64_t out = cap.length.load();
    EXPECT_GT(out, static_cast<int64_t>(srcSamples) * 19 / 10);
    EXPECT_LT(out, static_cast<int64_t>(srcSamples) * 21 / 10);

    f.deleteFile();
}

// --- StretchRenderer: cancellation returns cleanly ---
TEST(StretchRenderer, CancelCompletesCleanly)
{
    // A longer source so the render has time to be cancelled mid-flight.
    auto f = makeSineWav(10.0, 44100);

    HDAW::StretchRenderer r;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    std::atomic<bool> done{ false };
    r.onComplete = [&](const HDAW::StretchRenderer::Result&) { done.store(true); };

    ASSERT_TRUE(r.startRender(f.getFullPathName(), 3.0, 44100.0, fm));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    r.cancel();

    // The renderer's destructor joins the worker; if we reach here without
    // deadlock the cancellation path is healthy. (We don't assert on whether
    // the render finished or was cancelled — both are acceptable outcomes
    // depending on timing — only that join() returns.)
    f.deleteFile();
}

// --- StretchCache: miss, request, complete, hit ---
TEST(StretchCache, RequestThenLookup)
{
    auto f = makeSineWav(0.5, 44100);
    HDAW::StretchCache cache;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    // Initially: no entry.
    EXPECT_EQ(cache.lookup(/*clipID=*/1, 2.0, 44100.0), nullptr);

    QSignalSpy readySpy(&cache, &HDAW::StretchCache::entryReady);
    cache.requestRender(/*clipID=*/1, f.getFullPathName(), 2.0, 44100.0, fm);

    // Wait for the signal (bounded). entryReady is emitted on the Qt
    // message thread via QueuedConnection, so the event loop must spin.
    for (int i = 0; i < 1000 && readySpy.count() == 0; ++i)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_GE(readySpy.count(), 1);

    // Now a matching lookup must succeed.
    const HDAW::StretchCache::Entry* e = cache.lookup(1, 2.0, 44100.0);
    ASSERT_NE(e, nullptr);
    EXPECT_TRUE(e->ready);
    EXPECT_GT(e->length, 0);

    f.deleteFile();
}

// --- Commands: fit-to-loop math ---
TEST(StretchCommands, FitToLoopComputesRatio)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    // Set a 4-second loop region on the transport tree.
    cmds.setLoopStart(1.0);
    cmds.setLoopEnd(5.0);

    // Create an audio clip on track 0 with a known sourceDuration.
    int trackIdx = 0;
    auto trackList = engine.getProjectModel().getTrackListTree();
    auto trackTree = trackList.getChild(trackIdx);
    auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
    // Use an empty CLIP_LIST (the default project may or may not have one;
    // create one if missing).
    if (!clipList.isValid())
    {
        clipList = juce::ValueTree(IDs::CLIP_LIST);
        trackTree.addChild(clipList, -1, nullptr);
    }
    auto clip = ProjectModel::createAudioClip("t", 0.0, 2.0, "/nonexistent.wav");
    // sourceDuration defaults to the duration passed in (2.0s).
    clipList.addChild(clip, -1, nullptr);
    int clipId = static_cast<int>(clip.getProperty(IDs::clipID));

    cmds.fitClipToLoop(clipId);

    auto snap = engine.getReadModel().getClip(clipId);
    EXPECT_EQ(snap.stretchMode, 2);          // ManualRatio
    EXPECT_NEAR(snap.stretchRatio, 2.0, 1e-6); // 4s loop / 2s source
    EXPECT_NEAR(snap.durationBeats, 4.0, 1e-6);
}

// --- Commands: tempo match math ---
TEST(StretchCommands, TempoMatchDerivesRatio)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    // Project tempo defaults to 120 in createDefaultProject.
    auto trackList = engine.getProjectModel().getTrackListTree();
    auto trackTree = trackList.getChild(0);
    auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
    if (!clipList.isValid())
    {
        clipList = juce::ValueTree(IDs::CLIP_LIST);
        trackTree.addChild(clipList, -1, nullptr);
    }
    auto clip = ProjectModel::createAudioClip("t", 0.0, 2.0, "/nonexistent.wav");
    clipList.addChild(clip, -1, nullptr);
    int clipId = static_cast<int>(clip.getProperty(IDs::clipID));

    cmds.setClipSourceBpm(clipId, 100.0);
    cmds.tempoMatchClip(clipId);

    auto snap = engine.getReadModel().getClip(clipId);
    EXPECT_EQ(snap.stretchMode, 1);             // TempoMatch
    EXPECT_NEAR(snap.stretchRatio, 100.0 / 120.0, 1e-6);
    EXPECT_NEAR(snap.durationBeats, 2.0 * 100.0 / 120.0, 1e-6);
}
