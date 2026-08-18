#include <gtest/gtest.h>
#include <iostream>
#include <algorithm>
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

} // namespace

TEST(ExportVolumeBypass, DISABLED_RealProjectVolumeSensitivity)
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