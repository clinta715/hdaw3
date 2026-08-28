#include <gtest/gtest.h>
#include "engine/DecodedSoundPool.h"
#include "engine/ClipSourceProcessor.h"
#include "engine/TransportManager.h"
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/TrackFXSlot.h"
#include "model/ProjectModel.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <fstream>

namespace {

juce::File writeSineWav(const char* tag, int lengthSamples, double sr = 44100.0, int numChannels = 1)
{
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
        for (int ch = 0; ch < numChannels; ++ch)
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

// Two clips + one sampler slot all referencing the same file → exactly one
// decode, and a routing rebuild reacquires the pool entry (no re-decode).
TEST(AudioPoolDedup, ClipAndSamplerShareOneDecodeAcrossRebuild)
{
    AudioEngine engine;
    engine.initialize();

    auto file = writeSineWav("engine_share", 44100);
    const juce::String path = file.getFullPathName();

    // Two audio clips on tracks 0 and 2 (track 1 is the MIDI "Synth" track).
    auto& cmds = engine.getProjectCommands();
    cmds.addAudioClip(0, 0.0, 1.0, path.toStdString(), "clipA");
    cmds.addAudioClip(2, 0.0, 1.0, path.toStdString(), "clipB");

    // The clip adds schedule a coalesced async routing rebuild on the message
    // pump thread (AudioEngine::valueTreeChildAdded → triggerAsyncUpdate).
    // Let it land BEFORE rebuilding track FX from this thread: the pump's
    // rebuildRoutingGraph destroys the RoutingManager (and its Tracks) that
    // the synchronous rebuildTrackFX below mutates, so without this the test
    // races a use-after-free (crash inside Track::rebuildFXChain).
    // This drain closes the FIRST window. addFxSlot/setSamplerSample (below)
    // queue their OWN async rebuild — drained again just before the explicit
    // rebuildRoutingGraph() below, closing that second window too, so the
    // live-processor read cannot race a RoutingManager swap.
    // Drain the coalesced async routing rebuild (deterministic; see AudioEngine::drainPendingRoutingRebuild).
    engine.drainPendingRoutingRebuild();

    // Sampler slot on track 0 with the same file.
    cmds.addFxSlot(0, "sampler", 0, "");
    cmds.setSamplerSample(0, 0, path.toStdString(), 60);

    auto& pool = engine.getProjectPool().getDecodedSoundPool();
    EXPECT_EQ(pool.getDecodeCount(), 1);    // one decode, three consumers
    EXPECT_EQ(pool.getEntryCount(), 1);

    // Drain the coalesced async routing rebuild (deterministic; see AudioEngine::drainPendingRoutingRebuild).
    engine.drainPendingRoutingRebuild();
    // Rebuild the routing graph — the new processors must reacquire the
    // pool entry, not re-decode (Gate 1/10).
    engine.getMainProcessor()->rebuildRoutingGraph();
    EXPECT_EQ(pool.getDecodeCount(), 1);
    EXPECT_EQ(pool.getEntryCount(), 1);

    // And the sampler slot still has its sound on the LIVE processor.
    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    auto* slot = track->getFXChain()[0].get();
    ASSERT_NE(slot, nullptr);
    EXPECT_FALSE(slot->getSamplerSoundForTest() == nullptr);

    file.deleteFile();
}

TEST(AudioPoolDedup, SamplerWithoutPoolStillDecodesLocally)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::DecodedSoundPool pool(fm);

    auto file = writeSineWav("sampler_local", 44100);
    juce::ValueTree slotTree(IDs::FX_SLOT);
    slotTree.setProperty(IDs::fxType, "sampler", nullptr);
    slotTree.setProperty(juce::Identifier("sampleFile"), file.getFullPathName(), nullptr);
    slotTree.setProperty(juce::Identifier("rootNote"), 60, nullptr);

    // No pool passed → existing local-decode fallback must still work.
    HDAW::TrackFXSlot slot("sampler");
    slot.loadSamplerState(slotTree); // no format manager, no pool
    // The sampler engine only exists after prepare() (the slot ctor stages
    // the sound otherwise); prepare() adopts the staged sound into the engine.
    slot.prepare({ 44100.0, 512, 2 });
    EXPECT_FALSE(slot.getSamplerSoundForTest() == nullptr);
    file.deleteFile();
}

TEST(AudioPoolDedup, EngineWiresPoolAndRebuildReacquiresWithoutRedecode)
{
    AudioEngine engine;
    engine.initialize();

    auto file = writeSineWav("engine_wire", 44100);
    const juce::String path = file.getFullPathName();
    engine.getProjectCommands().addAudioClip(0, 0.0, 1.0, path.toStdString(), "clipA");

    // Clip adds only mutate the ValueTree; the routing rebuild is scheduled
    // asynchronously (AudioEngine::valueTreeChildAdded → triggerAsyncUpdate)
    // and AudioProcessorGraph prepares its nodes on ANOTHER async pass on the
    // message-pump thread (the render-sequence bake). The test thread is not
    // the message thread, so both passes must land before any engine-state
    // assertion. The drain flushes the coalesced rebuild ON the message
    // thread, where graph.prepareToPlay's topology pass runs synchronously
    // as well — both passes settle before it returns.
    // Drain the coalesced async routing rebuild (deterministic; see AudioEngine::drainPendingRoutingRebuild).
    engine.drainPendingRoutingRebuild();

    auto& pool = engine.getProjectPool().getDecodedSoundPool();
    EXPECT_EQ(pool.getDecodeCount(), 1);

    // Second clip, same file → still one decode.
    engine.getProjectCommands().addAudioClip(2, 0.0, 1.0, path.toStdString(), "clipB");
    // Drain the coalesced async routing rebuild (deterministic; see AudioEngine::drainPendingRoutingRebuild).
    engine.drainPendingRoutingRebuild();
    EXPECT_EQ(pool.getDecodeCount(), 1);

    // Rebuild the graph twice — decode count must NOT grow (Gate 1/10). The
    // synchronous rebuilds swap in fresh processors, but their prepareToPlay
    // (which acquires the pooled decode) runs in the graph's async bake on
    // the pump thread — drain again so the bake and any queued updates land
    // before the live processors are inspected.
    engine.getMainProcessor()->rebuildRoutingGraph();
    engine.getMainProcessor()->rebuildRoutingGraph();
    // Drain the coalesced async routing rebuild (deterministic; see AudioEngine::drainPendingRoutingRebuild).
    engine.drainPendingRoutingRebuild();
    EXPECT_EQ(pool.getDecodeCount(), 1);

    // Live processors actually hold the shared buffer (not just the model).
    auto* rm = engine.getMainProcessor()->getRoutingManager();
    ASSERT_NE(rm, nullptr);
    EXPECT_EQ(rm->getAudioClipSources().size(), 2u);
    for (const auto& [unused, clip] : rm->getAudioClipSources())
    {
        ASSERT_NE(clip, nullptr);
        EXPECT_NE(clip->getPreloadedDataForTest(0), nullptr);
    }

    file.deleteFile();
}

TEST(AudioPoolDedup, StereoDataMatchesDecodedSamples)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::DecodedSoundPool pool(fm);

    auto file = writeSineWav("stereo_data", 44100, 44100.0, 2);
    auto sound = pool.acquire(file.getFullPathName());
    ASSERT_NE(sound, nullptr);
    EXPECT_EQ(sound->numChannels, 2);
    EXPECT_EQ(sound->length, 44100);
    ASSERT_NE(sound->data[0], nullptr);
    ASSERT_NE(sound->data[1], nullptr);
    bool anyNonZero0 = false;
    bool anyNonZero1 = false;
    for (int64_t i = 0; i < sound->length && (!anyNonZero0 || !anyNonZero1); ++i)
    {
        if (std::abs(sound->data[0][i]) > 0.0f) anyNonZero0 = true;
        if (std::abs(sound->data[1][i]) > 0.0f) anyNonZero1 = true;
    }
    EXPECT_TRUE(anyNonZero0);
    EXPECT_TRUE(anyNonZero1);
    file.deleteFile();
}

TEST(AudioPoolDedup, TwoClipProcessorsShareOneStereoDecode)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::DecodedSoundPool pool(fm);
    HDAW::TransportManager tm;
    tm.setSampleRate(44100.0);
    tm.setBPM(120.0);

    auto file = writeSineWav("stereo_share", 44100, 44100.0, 2);
    auto path = file.getFullPathName();

    HDAW::ClipSourceProcessor a(tm, fm, &pool);
    HDAW::ClipSourceProcessor b(tm, fm, &pool);
    a.setSourceFile(path);
    b.setSourceFile(path);
    a.prepareToPlay(44100.0, 512);
    b.prepareToPlay(44100.0, 512);

    EXPECT_EQ(pool.getDecodeCount(), 1);
    EXPECT_EQ(pool.getEntryCount(), 1);

    EXPECT_NE(a.getPreloadedDataForTest(0), nullptr);
    EXPECT_EQ(a.getPreloadedDataForTest(0), b.getPreloadedDataForTest(0));

    tm.setCurrentSample(0);
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    juce::MidiBuffer midi;
    a.processBlock(buffer, midi);
    float maxAbs = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        for (int s = 0; s < 512; ++s)
            maxAbs = std::max(maxAbs, std::abs(buffer.getSample(ch, s)));
    EXPECT_GT(maxAbs, 0.05f);

    file.deleteFile();
}


// Repro for handoff §2: a SECOND sampler_set_sample on the same slot must
// reach the LIVE processor. The tree carries the truth (save/export use it),
// but the live slot was observed stuck on the first sound / soundless after a
// re-set. First set (fresh slot) works; any subsequent set must also rebuild
// the live slot with the new file + root note.
TEST(AudioPoolDedup, SamplerResampleUpdatesLiveProcessor)
{
    AudioEngine engine;
    engine.initialize();

    auto fileA = writeSineWav("resample_a", 44100);
    auto fileB = writeSineWav("resample_b", 44100);
    auto& cmds = engine.getProjectCommands();

    cmds.addFxSlot(0, "sampler", 0, "");
    cmds.setSamplerSample(0, 0, fileA.getFullPathName().toStdString(), 60);
    engine.drainPendingRoutingRebuild();

    // Re-fetch the live slot each time (async routing rebuilds swap the
    // RoutingManager / Track objects).
    auto liveSlot = [&](int t, int s) -> HDAW::TrackFXSlot* {
        auto* trk = engine.getMainProcessor()->getTrack(t);
        if (trk == nullptr || s >= static_cast<int>(trk->getFXChain().size()))
            return nullptr;
        return trk->getFXChain()[static_cast<size_t>(s)].get();
    };

    const auto* soundA = liveSlot(0, 0) != nullptr ? liveSlot(0, 0)->getSamplerSoundForTest() : nullptr;
    ASSERT_NE(soundA, nullptr) << "first set must load a sound";
    EXPECT_EQ(soundA->rootNote, 60);

    // Second set, DIFFERENT file + root note: the live processor must now
    // carry B (and it must not keep silently reporting the old state).
    cmds.setSamplerSample(0, 0, fileB.getFullPathName().toStdString(), 62);
    const auto* soundB = liveSlot(0, 0) != nullptr ? liveSlot(0, 0)->getSamplerSoundForTest() : nullptr;
    EXPECT_NE(soundB, nullptr) << "second set (different file) must load a sound immediately";
    if (soundB != nullptr)
        EXPECT_EQ(soundB->rootNote, 62) << "second set must apply the NEW file/root, not leave the old sound";

    // Same path once settled (async routing rebuild landed).
    engine.drainPendingRoutingRebuild();
    soundB = liveSlot(0, 0) != nullptr ? liveSlot(0, 0)->getSamplerSoundForTest() : nullptr;
    EXPECT_NE(soundB, nullptr) << "second set must still have a sound after the routing rebuild lands";
    if (soundB != nullptr)
        EXPECT_EQ(soundB->rootNote, 62);

    // Re-set the SAME file (ValueTree setProperty is a no-op on unchanged
    // values, lesson 2): the explicit rebuild must still keep the sound live.
    cmds.setSamplerSample(0, 0, fileA.getFullPathName().toStdString(), 60);
    engine.drainPendingRoutingRebuild();
    const auto* soundA2 = liveSlot(0, 0) != nullptr ? liveSlot(0, 0)->getSamplerSoundForTest() : nullptr;
    EXPECT_NE(soundA2, nullptr) << "same-file re-set must keep a live sound";
    if (soundA2 != nullptr)
        EXPECT_EQ(soundA2->rootNote, 60);

    fileA.deleteFile();
    fileB.deleteFile();
}
