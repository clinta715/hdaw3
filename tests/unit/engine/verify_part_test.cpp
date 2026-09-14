#include <gtest/gtest.h>
#include <string>
#include "engine/AudioEngine.h"
#include "model/ProjectModel.h"
#include <juce_audio_formats/juce_audio_formats.h>

// verifyPart — composer self-verification: solo + full-mix renders of the
// track's window via the shared renderTrackWindow, reporting levels,
// clipping, audibility and spectral band presence. Internal fm_synth only —
// no real plugins, no env guards.

TEST(VerifyPart, ComposedPartPasses)
{
    AudioEngine engine;
    engine.initialize();

    ProjectCommands::InstrumentPartParams params;
    params.trackName = "Verify";
    params.style = "Standard";
    params.lengthBeats = 4.0;
    params.seed = 7;
    params.targetRms = 0.15f;
    params.windowSeconds = 4.0;

    auto res = engine.getProjectCommands().addInstrumentPart(params);
    ASSERT_TRUE(res.error.empty()) << res.error;
    ASSERT_GE(res.trackIndex, 0);

    auto v = engine.getProjectCommands().verifyPart(res.trackIndex, 4.0);
    EXPECT_TRUE(v.ok) << v.error;
    EXPECT_TRUE(v.audible);
    EXPECT_TRUE(v.nonClipping);
    EXPECT_GT(v.soloPeak, 1e-4f);
    EXPECT_LT(v.mixPeak, 1.0f);
    // Default FM patch (v0.24.0 gain restructure, op output level 60, all ops
    // ratio 0.5) produces ~88% low-band, ~11.5% mid-band, ~0.015% high-band
    // energy — the 0.5% high-band threshold is not met. This is a true
    // positive: the part genuinely lacks HF content.
    EXPECT_FALSE(v.bandsPresent);
    EXPECT_TRUE(v.bandLow);
    EXPECT_TRUE(v.bandMid);
    EXPECT_FALSE(v.bandHigh);
}

TEST(VerifyPart, TwoPartsMixAtLeastSolo)
{
    AudioEngine engine;
    engine.initialize();
    auto& pc = engine.getProjectCommands();

    ProjectCommands::InstrumentPartParams p1;
    p1.trackName = "PartA";
    p1.style = "Standard";
    p1.lengthBeats = 4.0;
    p1.seed = 1;
    auto r1 = pc.addInstrumentPart(p1);
    ASSERT_TRUE(r1.error.empty()) << r1.error;

    ProjectCommands::InstrumentPartParams p2;
    p2.trackName = "PartB";
    p2.style = "BassLine";
    p2.lengthBeats = 4.0;
    p2.seed = 2;
    auto r2 = pc.addInstrumentPart(p2);
    ASSERT_TRUE(r2.error.empty()) << r2.error;

    auto v = pc.verifyPart(r1.trackIndex, 4.0);
    ASSERT_TRUE(v.ok) << v.error;
    EXPECT_GE(v.mixRms, v.soloRms * 0.999f);
}

TEST(VerifyPart, InvalidTrack)
{
    AudioEngine engine;
    engine.initialize();

    auto v = engine.getProjectCommands().verifyPart(99, 4.0);
    EXPECT_FALSE(v.ok);
    EXPECT_FALSE(v.error.empty());
}

TEST(VerifyPart, EmptyTrack)
{
    AudioEngine engine;
    engine.initialize();

    // Zero-track default (v0.33+, lesson 9): create an explicitly EMPTY track,
    // then verify the "track has no clips" contract on it.
    auto& pc = engine.getProjectCommands();
    const int trackIdx = pc.addTrack("EmptyTrack");
    ASSERT_GE(trackIdx, 0);
    engine.drainPendingRoutingRebuild();

    auto v = engine.getProjectCommands().verifyPart(trackIdx, 4.0);
    EXPECT_FALSE(v.ok);
    EXPECT_EQ(v.error, "track has no clips");
}

TEST(VerifyPart, AudioClipWithHfContentHasBandsPresent)
{
    AudioEngine engine;
    engine.initialize();

    // Write a temp WAV with 100 Hz (low) + 1000 Hz (mid) + 5000 Hz (high)
    // content so verifyPart's band analysis sees all three bands.
    auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    auto wavFile = tempDir.getChildFile("hdaw_band_test_" + juce::String(juce::Random::getSystemRandom().nextInt()) + ".wav");
    {
        const double sr = 48000.0;
        const int len = static_cast<int>(sr * 4.0); // 4 seconds
        juce::AudioBuffer<float> buf(2, len);
        for (int s = 0; s < len; ++s)
        {
            float val = 0.2f * std::sin(2.0f * juce::MathConstants<float>::pi * 100.0f * s / sr)
                      + 0.2f * std::sin(2.0f * juce::MathConstants<float>::pi * 1000.0f * s / sr)
                      + 0.2f * std::sin(2.0f * juce::MathConstants<float>::pi * 5000.0f * s / sr);
            buf.setSample(0, s, val);
            buf.setSample(1, s, val);
        }
        juce::WavAudioFormat wavFmt;
        std::unique_ptr<juce::AudioFormatWriter> w(wavFmt.createWriterFor(
            new juce::FileOutputStream(wavFile), sr, 2, 24, {}, 0));
        if (w != nullptr)
            w->writeFromAudioSampleBuffer(buf, 0, len);
    }
    ASSERT_TRUE(wavFile.existsAsFile());

    // Zero-track default (v0.33+, lesson 9): seed the track the clip and the
    // band verification use.
    auto& pc = engine.getProjectCommands();
    const int trackIdx = pc.addTrack("BandTest");
    ASSERT_GE(trackIdx, 0);
    engine.drainPendingRoutingRebuild();

    auto clipId = pc.addAudioClip(trackIdx, 0.0, 4.0, wavFile.getFullPathName().toStdString(), "BandTest");
    ASSERT_GT(clipId, 0);

    auto v = pc.verifyPart(trackIdx, 4.0);
    EXPECT_TRUE(v.ok) << v.error;
    EXPECT_TRUE(v.audible);
    EXPECT_TRUE(v.bandLow);
    EXPECT_TRUE(v.bandMid);
    EXPECT_TRUE(v.bandHigh);
    EXPECT_TRUE(v.bandsPresent);

    wavFile.deleteFile();
}

TEST(VerifyPart, SamplerSoloRenderWithOffsetNotesIsAudible)
{
    // B7 regression (Modular Dawn audit): verify_part solo-rendered a
    // sampler track SILENT (soloRms=0, audible=0) while the full-mix render
    // showed real contribution. The notes here sit at a NON-ZERO beat offset
    // inside the clip (the audit's kick pattern), so a broken window or a
    // broken note->sampler seam shows up as hard silence.
    AudioEngine engine;
    engine.initialize();
    auto& pc = engine.getProjectCommands();

    // Small sine WAV as the sampler's sample.
    auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    auto wavFile = tempDir.getChildFile("hdaw_b7_sampler_" + juce::String(juce::Random::getSystemRandom().nextInt()) + ".wav");
    {
        const double sr = 44100.0;
        const int len = static_cast<int>(sr * 1.0);
        juce::AudioBuffer<float> buf(1, len);
        for (int s = 0; s < len; ++s)
            buf.setSample(0, s, 0.8f * std::sin(2.0f * juce::MathConstants<float>::pi * 220.0f * s / sr));
        juce::WavAudioFormat wavFmt;
        std::unique_ptr<juce::AudioFormatWriter> w(wavFmt.createWriterFor(
            new juce::FileOutputStream(wavFile), sr, 1, 16, {}, 0));
        ASSERT_NE(w, nullptr);
        w->writeFromAudioSampleBuffer(buf, 0, len);
    }

    const int trackIdx = pc.addTrack("Kick");
    ASSERT_GE(trackIdx, 0);
    engine.drainPendingRoutingRebuild();

    pc.addFxSlot(trackIdx, "sampler", 0, "");
    pc.setSamplerSample(trackIdx, 0, wavFile.getFullPathName().toStdString(), 60);
    engine.drainPendingRoutingRebuild();

    // 4-bar clip starting at beat 8 (a NON-zero start — the audit's shape).
    const int clipId = pc.addMidiClip(trackIdx, 8.0, 16.0, "Kick");
    ASSERT_GT(clipId, 0);
    for (int b = 0; b < 16; b += 4)
        ASSERT_GT(pc.addNote(clipId, 60, 100, b + 0.5, 0.25), 0);
    engine.drainPendingRoutingRebuild();

    auto v = pc.verifyPart(trackIdx, 4.0);
    EXPECT_TRUE(v.ok) << v.error;
    EXPECT_GT(v.soloPeak, 1e-4f) << "sampler solo render must be audible (B7)";
    EXPECT_TRUE(v.audible);

    wavFile.deleteFile();
}

TEST(VerifyPart, PsyFmSoloRenderWithOffsetNotesIsAudible)
{
    // B7 companion (Modular Dawn audit: acidvar, psy_fm, same silent-solo
    // evidence): an internal psy_fm slot with notes at a non-zero beat offset
    // must solo-render audibly through the same export path.
    AudioEngine engine;
    engine.initialize();
    auto& pc = engine.getProjectCommands();

    const int trackIdx = pc.addTrack("Acidvar");
    ASSERT_GE(trackIdx, 0);
    engine.drainPendingRoutingRebuild();

    pc.addFxSlot(trackIdx, "psy_fm", 0, "");
    engine.drainPendingRoutingRebuild();

    const int clipId = pc.addMidiClip(trackIdx, 4.0, 8.0, "Acid");
    ASSERT_GT(clipId, 0);
    for (int b = 0; b < 8; ++b)
        ASSERT_GT(pc.addNote(clipId, 45 + (b % 5), 100, b + 0.25, 0.5), 0);
    engine.drainPendingRoutingRebuild();

    auto v = pc.verifyPart(trackIdx, 4.0);
    EXPECT_TRUE(v.ok) << v.error;
    EXPECT_GT(v.soloPeak, 1e-4f) << "psy_fm solo render must be audible (B7)";
    EXPECT_TRUE(v.audible);
}

TEST(VerifyPart, MissingSamplerSampleFailsLoud)
{
    // B7 root cause (Modular Dawn): a sampler whose sampleFile no longer
    // exists renders SILENT offline (the live engine keeps the pooled decode,
    // so the mix context still shows contribution) — silence masquerading as
    // a broken part. The render path must fail LOUDLY instead.
    AudioEngine engine;
    engine.initialize();
    auto& pc = engine.getProjectCommands();

    const int trackIdx = pc.addTrack("GhostKick");
    ASSERT_GE(trackIdx, 0);
    engine.drainPendingRoutingRebuild();

    pc.addFxSlot(trackIdx, "sampler", 0, "");
    pc.setSamplerSample(trackIdx, 0, "Q:/nonexistent/gone.wav", 60);
    engine.drainPendingRoutingRebuild();

    const int clipId = pc.addMidiClip(trackIdx, 0.0, 4.0, "Kick");
    ASSERT_GT(clipId, 0);
    ASSERT_GT(pc.addNote(clipId, 60, 100, 0.5, 0.25), 0);
    engine.drainPendingRoutingRebuild();

    auto v = pc.verifyPart(trackIdx, 2.0);
    EXPECT_FALSE(v.ok) << "missing sample must fail loudly, not render silence";
    EXPECT_NE(v.error.find("sample file missing"), std::string::npos) << v.error;
}

TEST(VerifyPart, ExplicitBeatWindowRendersLateNotes)
{
    // B8 (Modular Dawn audit): a whole-arrangement clip (beat 0, notes only at
    // beats 12+) could not be solo-verified — the default window renders from
    // the earliest clip start (beat 0) and misses the notes entirely.
    // startBeat/endBeat pin the window onto the late notes.
    AudioEngine engine;
    engine.initialize();
    auto& pc = engine.getProjectCommands();

    const int trackIdx = pc.addTrack("LateLayer");
    ASSERT_GE(trackIdx, 0);
    engine.drainPendingRoutingRebuild();

    pc.addFxSlot(trackIdx, "fm_synth", 0, "");
    engine.drainPendingRoutingRebuild();

    // One clip spanning beats 0..16 (the psytrance score shape), notes ONLY
    // at beats 12..14.
    const int clipId = pc.addMidiClip(trackIdx, 0.0, 16.0, "Whole");
    ASSERT_GT(clipId, 0);
    for (int b = 12; b < 14; ++b)
        ASSERT_GT(pc.addNote(clipId, 60, 100, b + 0.0, 0.5), 0);
    engine.drainPendingRoutingRebuild();

    // Default window (earliest clip start = beat 0): 2 s of intro silence.
    auto def = pc.verifyPart(trackIdx, 2.0);
    ASSERT_TRUE(def.ok) << def.error;
    EXPECT_DOUBLE_EQ(def.windowStart, 0.0);
    EXPECT_FALSE(def.audible) << "notes live at beats 12+; the default window must miss them";

    // Explicit window pinned onto the notes (beats -> seconds at 120 BPM).
    auto v = pc.verifyPart(trackIdx, 2.0, 12.0, 14.0);
    ASSERT_TRUE(v.ok) << v.error;
    EXPECT_GT(v.soloPeak, 1e-4f) << "explicit window must capture the late notes (B8)";
    EXPECT_TRUE(v.audible);
    EXPECT_DOUBLE_EQ(v.windowStart, 6.0);          // 12 beats * 60/120
    EXPECT_DOUBLE_EQ(v.durationSeconds, 1.0);      // (14-12) beats * 60/120
    EXPECT_DOUBLE_EQ(v.startBeat, 12.0);
    EXPECT_DOUBLE_EQ(v.endBeat, 14.0);
}

TEST(VerifyPart, ExplicitStartBeatUsesWindowSeconds)
{
    // startBeat without endBeat: window spans [startBeat, startBeat + windowSeconds).
    AudioEngine engine;
    engine.initialize();
    auto& pc = engine.getProjectCommands();

    const int trackIdx = pc.addTrack("StartOnly");
    ASSERT_GE(trackIdx, 0);
    engine.drainPendingRoutingRebuild();
    pc.addFxSlot(trackIdx, "fm_synth", 0, "");
    engine.drainPendingRoutingRebuild();

    const int clipId = pc.addMidiClip(trackIdx, 0.0, 16.0, "Whole");
    ASSERT_GT(clipId, 0);
    ASSERT_GT(pc.addNote(clipId, 60, 100, 8.0, 0.5), 0);
    engine.drainPendingRoutingRebuild();

    auto v = pc.verifyPart(trackIdx, 2.0, 8.0, 0.0);
    ASSERT_TRUE(v.ok) << v.error;
    EXPECT_GT(v.soloPeak, 1e-4f);
    EXPECT_DOUBLE_EQ(v.windowStart, 4.0);      // 8 beats * 60/120
    EXPECT_DOUBLE_EQ(v.durationSeconds, 2.0);  // windowSeconds kept
    EXPECT_DOUBLE_EQ(v.startBeat, 8.0);
}

TEST(VerifyPart, ExplicitWindowRejectsInvertedRange)
{
    AudioEngine engine;
    engine.initialize();
    auto& pc = engine.getProjectCommands();

    const int trackIdx = pc.addTrack("BadWindow");
    ASSERT_GE(trackIdx, 0);
    engine.drainPendingRoutingRebuild();
    const int clipId = pc.addMidiClip(trackIdx, 0.0, 4.0, "C");
    ASSERT_GT(clipId, 0);
    engine.drainPendingRoutingRebuild();

    auto v = pc.verifyPart(trackIdx, 2.0, 6.0, 4.0);
    EXPECT_FALSE(v.ok);
    EXPECT_NE(v.error.find("startBeat/endBeat invalid"), std::string::npos) << v.error;
}

TEST(VerifyPart, OfflineRenderDoesNotClobberClipIds)
{
    // Regression: ExportManager's local ProjectModel ctor used to reset the
    // process-global clip-id counter, causing addMidiClip after any offline
    // render to reuse id 1 → notes silently land on the wrong clip.
    AudioEngine engine;
    engine.initialize();
    auto& pc = engine.getProjectCommands();

    // Add part A and verify (triggers offline render via renderTrackWindow).
    ProjectCommands::InstrumentPartParams pA;
    pA.trackName = "PartA";
    pA.style = "Standard";
    pA.lengthBeats = 4.0;
    pA.seed = 7;
    auto rA = pc.addInstrumentPart(pA);
    ASSERT_TRUE(rA.error.empty()) << rA.error;
    ASSERT_FALSE(rA.clipIds.empty());
    const int clipIdA = rA.clipIds[0];

    auto vA = pc.verifyPart(rA.trackIndex, 4.0);
    ASSERT_TRUE(vA.ok) << vA.error;
    EXPECT_GT(vA.soloRms, 0.0f) << "part A should be audible";

    // Add part B AFTER the offline render.
    ProjectCommands::InstrumentPartParams pB;
    pB.trackName = "PartB";
    pB.style = "Standard";
    pB.lengthBeats = 4.0;
    pB.seed = 42;
    auto rB = pc.addInstrumentPart(pB);
    ASSERT_TRUE(rB.error.empty()) << rB.error;
    ASSERT_FALSE(rB.clipIds.empty());
    const int clipIdB = rB.clipIds[0];

    // Clip ids must be unique (no collision from stale global counter).
    EXPECT_NE(clipIdA, clipIdB) << "offline render must not clobber clip-id allocator";

    // Part B's notes must be on ITS clip, not part A's.
    auto trackList = engine.getProjectModel().getTrackListTree();
    auto clipListB = trackList.getChild(rB.trackIndex).getChildWithName(IDs::CLIP_LIST);
    ASSERT_GT(clipListB.getNumChildren(), 0);
    int notesOnBClip = clipListB.getChild(0).getChildWithName(IDs::MIDI_NOTE_LIST).getNumChildren();
    EXPECT_GT(notesOnBClip, 0) << "part B's notes must land on its own clip";

    // Part B should be audible when solo-rendered.
    auto vB = pc.verifyPart(rB.trackIndex, 4.0);
    EXPECT_TRUE(vB.ok) << vB.error;
    EXPECT_GT(vB.soloRms, 0.0f) << "part B must be audible after prior offline render";
}
