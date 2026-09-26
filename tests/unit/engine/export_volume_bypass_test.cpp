#include <gtest/gtest.h>
#include <iostream>
#include <algorithm>
#include <cmath>
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/ExportManager.h"
#include "engine/Track.h"
#include "model/ProjectModel.h"
#include <juce_audio_formats/juce_audio_formats.h>

// Reproduction harness for the export volume-bypass bug: with the real
// polywave_shift.hdaw project loaded, exporting at track volume 0.001 and at
// volume 1.0 must produce peaks ~60 dB apart. Historically BOTH peaked at 1.0
// (volume loop bypassed in the offline render path). Kept as a standalone
// diagnostic test so it can be extended with per-track buffer diagnostics.

namespace {

bool waitForExport(HDAW::ExportManager& em, int timeoutMs = 180000)
{
    const auto deadline = juce::Time::getMillisecondCounter() + timeoutMs;
    while (em.isExporting() && juce::Time::getMillisecondCounter() < deadline)
        juce::Thread::sleep(10);
    return !em.isExporting();
}

float peakOf(const juce::File& wav)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(wav));
    if (!r) return -1.0f;
    juce::AudioBuffer<float> buf(2, static_cast<int>(r->lengthInSamples));
    r->read(&buf, 0, static_cast<int>(r->lengthInSamples), 0, true, true);
    float peak = 0.0f;
    for (int c = 0; c < 2; ++c)
        for (int s = 0; s < buf.getNumSamples(); ++s)
            peak = std::max(peak, std::abs(buf.getSample(c, s)));
    return peak;
}

// Full-scale RMS over both channels. RMS (not peak) is what the two
// live-tree/automation pins assert on, so a partially-silent render cannot
// read as a pass on one lone spike.
float rmsOf(const juce::File& wav)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(wav));
    if (!r) return -1.0f;
    const int n = static_cast<int>(r->lengthInSamples);
    juce::AudioBuffer<float> buf(2, n);
    r->read(&buf, 0, n, 0, true, true);
    double sumSq = 0.0;
    for (int c = 0; c < 2; ++c)
        for (int s = 0; s < n; ++s)
        {
            const double v = buf.getSample(c, s);
            sumSq += v * v;
        }
    return n > 0 ? static_cast<float>(std::sqrt(sumSq / (2.0 * n))) : 0.0f;
}

// Render-level bands. The audio floor is well above a DC/denormal residue but
// far below the ~0.1+-RMS an internal sub_synth part produces at unity; the
// silence ceiling is below one LSB of a 24-bit render (-144 dBFS = 6e-8).
constexpr float kAudioFloorRms    = 1.0e-3f;
constexpr float kSilenceCeilingRms = 1.0e-5f;

} // namespace

TEST(ExportVolumeBypass, RealProjectVolumeSensitivity)
{
    // The render-sequence bake for a 771-clip graph exceeds the 15 s default
    // bake window in Debug builds; the production export scripts set this to
    // 120 s. Set it here too so the render can actually start.
    _putenv_s("HDAW_EXPORT_BAKE_TIMEOUT_MS", "120000");
    const juce::File proj("D:\\pdf\\roo projects\\hdaw3\\projects\\polywave_shift.hdaw");
    if (!proj.existsAsFile())
        GTEST_SKIP() << "project file not present";

    AudioEngine engine;
    engine.initialize();
    ASSERT_TRUE(engine.getProjectCommands().loadProject(proj.getFullPathName().toStdString()));

    auto* mp = engine.getMainProcessor();
    ASSERT_NE(mp, nullptr);

    auto trackList = engine.getProjectModel().getTrackListTree();
    const int numTracks = trackList.getNumChildren();
    ASSERT_EQ(numTracks, 13);

    juce::AudioFormatManager exportFm;
    exportFm.registerBasicFormats();
    const double duration = 30.0; // first 30 s: Sub Bass (15.36s) + drums
    auto& em = mp->getExportManager();

    auto setAllVolumes = [&](float v)
    {
        for (int i = 0; i < numTracks; ++i)
        {
            auto track = trackList.getChild(i);
            track.setProperty(IDs::volume, static_cast<double>(v), nullptr);
            // Volume automation (when ENABLED) overrides the manual fader
            // during playback/export. This project has ENABLED volume
            // automation on tracks 8 (Perc Low) and 10 (DX7 Pad) that drives
            // 0.57-0.85 regardless of the fader — disabling all volume lanes
            // is what makes the fader authoritative for this test.
            auto autoList = track.getChildWithName(IDs::AUTOMATION_LIST);
            if (autoList.isValid())
            {
                for (int a = 0; a < autoList.getNumChildren(); ++a)
                {
                    auto lane = autoList.getChild(a);
                    if (static_cast<int>(lane.getProperty(IDs::paramID)) == 1)
                        lane.setProperty(IDs::automationEnabled, false, nullptr);
                }
            }
        }
    };

    const juce::File outQuiet =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("hdaw_export_quiet.wav");
    const juce::File outLoud =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("hdaw_export_loud.wav");
    outQuiet.deleteFile();
    outLoud.deleteFile();

    setAllVolumes(0.001f);
    ASSERT_TRUE(em.startExport(engine.getProjectModel().getTree(), exportFm, nullptr,
                               outQuiet, 48000.0, 0.0, duration,
                               HDAW::ExportManager::WAV, 24));
    ASSERT_TRUE(waitForExport(em));

    setAllVolumes(1.0f);
    ASSERT_TRUE(em.startExport(engine.getProjectModel().getTree(), exportFm, nullptr,
                               outLoud, 48000.0, 0.0, duration,
                               HDAW::ExportManager::WAV, 24));
    ASSERT_TRUE(waitForExport(em));

    const float peakQuiet = peakOf(outQuiet);
    const float peakLoud  = peakOf(outLoud);
    std::cout << "[ExportVolumeBypass] peak at volume 0.001 = " << peakQuiet << std::endl;
    std::cout << "[ExportVolumeBypass] peak at volume 1.0   = " << peakLoud << std::endl;

    EXPECT_LT(peakQuiet, peakLoud * 0.02f)
        << "volume bypassed: 0.001-volume export is not ~60 dB quieter than 1.0-volume export";
}

// Regression pin for the 2026-09-24 report ("exports 3+ in one session ignore
// live tree changes"), disproven 2026-09-25. The stem audits in that report
// isolated tracks with a static `setTrackVolume` fader, but an ENABLED
// paramID-1 "Volume" automation lane OWNS the parameter in the offline render:
// Track::processBlock's playback branch evaluates `am->getValueAtTime(timeSec)`
// (Track.cpp:557-561) and, for pid == 1, drives
// `volumeGain.setTargetValue(...)` every block, overwriting the sampled fader.
// Before the first lane point the value is `points.front().second`
// (AutomationManager.h:59-77), which is why the audit lane's first point
// (beat 672, 1.0) sat at exactly 1.0 across the audit window.
// This test pins that ownership on rendered audio.
TEST(ExportVolumeBypass, VolumeAutomationOverridesTreeFader)
{
    // The render-sequence bake can exceed the 15 s default bake window; the
    // production export scripts set 120 s.
    _putenv_s("HDAW_EXPORT_BAKE_TIMEOUT_MS", "120000");

    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    // Two internal-synth tracks (no sample files, no external plugins).
    for (int t = 0; t < 2; ++t)
    {
        ASSERT_GE(cmds.addTrack("Synth " + std::to_string(t + 1)), 0);
        cmds.addFxSlot(t, "sub_synth", 0, "");
        const int clip = cmds.addMidiClip(t, 0.0, 4.0, "pattern");
        ASSERT_GE(clip, 0);
        for (int b = 0; b < 4; ++b)
            ASSERT_GT(cmds.addNote(clip, 52 + t * 12, 100, static_cast<double>(b), 0.9), 0);
    }
    // Track 1 is muted in BOTH cases so only the track under test contributes;
    // otherwise the case-B "silent" render would still carry track 1.
    cmds.setTrackMuted(1, true);
    engine.drainPendingRoutingRebuild();

    auto* mp = engine.getMainProcessor();
    ASSERT_NE(mp, nullptr);
    auto& em = mp->getExportManager();

    juce::AudioFormatManager exportFm;
    exportFm.registerBasicFormats();

    // %TEMP% writes are denied to spawned processes on this box; the repo
    // .tmp_dnb_theme directory is the export scratch the harness owns.
    const juce::File outDir("D:/pdf/roo projects/hdaw3/.tmp_dnb_theme");
    outDir.createDirectory();
    const juce::File outOn  = outDir.getChildFile("tmp_volauto_on.wav");
    const juce::File outOff = outDir.getChildFile("tmp_volauto_off.wav");

    const double duration = 2.0;

    // Case A: the Volume lane holds 1.0 across the window (a single point is a
    // constant curve) and is ENABLED, so it must override the 0.0 static fader.
    cmds.addAutomationPoint(0, "Volume", 0.0, 1.0f);
    cmds.setAutomationEnabled(0, "Volume", true);
    cmds.setTrackVolume(0, 0.0f);

    outOn.deleteFile();
    ASSERT_TRUE(em.startExport(engine.getProjectModel().getTree(), exportFm, nullptr,
                               outOn, 48000.0, 0.0, duration,
                               HDAW::ExportManager::WAV, 24));
    ASSERT_TRUE(waitForExport(em));
    ASSERT_GT(outOn.getSize(), 1000);

    // Case B (control): same fader, lane disabled -> the fader owns the
    // parameter and a 0.0 fader must render silence.
    cmds.setAutomationEnabled(0, "Volume", false);

    outOff.deleteFile();
    ASSERT_TRUE(em.startExport(engine.getProjectModel().getTree(), exportFm, nullptr,
                               outOff, 48000.0, 0.0, duration,
                               HDAW::ExportManager::WAV, 24));
    ASSERT_TRUE(waitForExport(em));
    ASSERT_GT(outOff.getSize(), 1000);

    const float rmsOn   = rmsOf(outOn);
    const float rmsOff  = rmsOf(outOff);
    const float peakOn  = peakOf(outOn);
    const float peakOff = peakOf(outOff);
    std::cout << "[ExportVolumeBypass] automation ON : rms=" << rmsOn
              << " peak=" << peakOn << std::endl;
    std::cout << "[ExportVolumeBypass] automation OFF: rms=" << rmsOff
              << " peak=" << peakOff << std::endl;

    ASSERT_GT(rmsOn, kAudioFloorRms)
        << "enabled Volume automation did NOT override the 0.0 static fader - render is silent";
    ASSERT_LT(rmsOff, kSilenceCeilingRms)
        << "fader 0.0 with the lane disabled still produced sound";

    const float dbApart = 20.0f * std::log10(rmsOn / std::max(rmsOff, 1.0e-9f));
    std::cout << "[ExportVolumeBypass] automation on/off separation = " << dbApart << " dB"
              << std::endl;
    EXPECT_GT(dbApart, 20.0f)
        << "automation-on and automation-off renders are not >20 dB apart";
}

// The property the false report doubted: consecutive exports made in one
// session DO re-read the live project tree. Export #1 (both tracks), #2 (track
// 1 muted), #3 (track 1 unmuted again) must show #3 == #1 and #2 well below #1.
TEST(ExportVolumeBypass, MultiExportRereadsLiveTree)
{
    _putenv_s("HDAW_EXPORT_BAKE_TIMEOUT_MS", "120000");

    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    // Two internal-synth tracks, no automation lanes touched. Track 1 is the
    // LOUDER one (Output Level 1.0 vs 0.25) so muting it drops the mix far past
    // the 3 dB floor rather than landing on the 1/sqrt(2) two-equal-tracks edge.
    for (int t = 0; t < 2; ++t)
    {
        ASSERT_GE(cmds.addTrack("Synth " + std::to_string(t + 1)), 0);
        cmds.addFxSlot(t, "sub_synth", 0, "");
        cmds.setFxSlotParam(t, 0, 14, t == 1 ? 1.0f : 0.25f); // paramID 14 = Output Level
        const int clip = cmds.addMidiClip(t, 0.0, 4.0, "pattern");
        ASSERT_GE(clip, 0);
        for (int b = 0; b < 4; ++b)
            ASSERT_GT(cmds.addNote(clip, 52 + t * 12, 100, static_cast<double>(b), 0.9), 0);
    }
    engine.drainPendingRoutingRebuild();

    auto* mp = engine.getMainProcessor();
    ASSERT_NE(mp, nullptr);
    auto& em = mp->getExportManager();

    juce::AudioFormatManager exportFm;
    exportFm.registerBasicFormats();

    const juce::File outDir("D:/pdf/roo projects/hdaw3/.tmp_dnb_theme");
    outDir.createDirectory();
    const juce::File outFull1 = outDir.getChildFile("tmp_multi_full_1.wav");
    const juce::File outSolo2 = outDir.getChildFile("tmp_multi_solo_2.wav");
    const juce::File outFull3 = outDir.getChildFile("tmp_multi_full_3.wav");

    const double duration = 2.0;

    // Export #1: both tracks unmuted.
    cmds.setTrackMuted(0, false);
    cmds.setTrackMuted(1, false);
    outFull1.deleteFile();
    ASSERT_TRUE(em.startExport(engine.getProjectModel().getTree(), exportFm, nullptr,
                               outFull1, 48000.0, 0.0, duration,
                               HDAW::ExportManager::WAV, 24));
    ASSERT_TRUE(waitForExport(em));
    ASSERT_GT(outFull1.getSize(), 1000);

    // Export #2: mute track 1 - the live tree changed between exports.
    cmds.setTrackMuted(1, true);
    outSolo2.deleteFile();
    ASSERT_TRUE(em.startExport(engine.getProjectModel().getTree(), exportFm, nullptr,
                               outSolo2, 48000.0, 0.0, duration,
                               HDAW::ExportManager::WAV, 24));
    ASSERT_TRUE(waitForExport(em));
    ASSERT_GT(outSolo2.getSize(), 1000);

    // Export #3: unmute track 1 again -> must match export #1.
    cmds.setTrackMuted(1, false);
    outFull3.deleteFile();
    ASSERT_TRUE(em.startExport(engine.getProjectModel().getTree(), exportFm, nullptr,
                               outFull3, 48000.0, 0.0, duration,
                               HDAW::ExportManager::WAV, 24));
    ASSERT_TRUE(waitForExport(em));
    ASSERT_GT(outFull3.getSize(), 1000);

    const float rmsFull1 = rmsOf(outFull1);
    const float rmsSolo2 = rmsOf(outSolo2);
    const float rmsFull3 = rmsOf(outFull3);
    std::cout << "[MultiExportRereadsLiveTree] full#1 rms=" << rmsFull1
              << "  solo#2 rms=" << rmsSolo2
              << "  full#3 rms=" << rmsFull3 << std::endl;

    ASSERT_GT(rmsFull1, kAudioFloorRms) << "full mix export is silent";

    // #3 re-read the live tree (track 1 unmuted): same mix as #1, within 1%.
    EXPECT_NEAR(rmsFull3, rmsFull1, 0.01f * rmsFull1)
        << "export #3 did not repeat export #1 within 1% RMS - stale tree";

    // #2 honoured the live mute: at least 3 dB below the full mix (-3.0103 dB is
    // exactly 1/sqrt(2)).
    const float dropDb = 20.0f * std::log10(rmsFull1 / std::max(rmsSolo2, 1.0e-9f));
    std::cout << "[MultiExportRereadsLiveTree] muted-mix drop = " << dropDb << " dB" << std::endl;
    EXPECT_LT(rmsSolo2, rmsFull1 * 0.7079458f)
        << "export #2 is not at least 3 dB below export #1 - live mute ignored";
}
