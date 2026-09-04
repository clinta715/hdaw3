#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstring>
#include <iostream>
#include <vector>
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/ExportManager.h"
#include "engine/Track.h"
#include "engine/TrackFXSlot.h"
#include "engine/FmSynthEngine.h"
#include "engine/Dx7SysexImport.h"
#include "model/ProjectModel.h"

// End-to-end refutation of the historical note "offline export renders a fixed
// FM tone invariant to imported patch bytes" (sweep_dx7_patches.py /
// docs/superpowers/plans/2026-09-04-patch-libraries-sidecars.md). The
// mechanism fix already landed: FmSynthEngine::prepare seeds the init patch
// exactly once via initPatchSeeded_, TrackFXSlot::loadFmPatchFromTree restores
// fmPatchData on every rebuildFXChain, and RoutingManager::buildTrackProcessor
// calls prepareToPlay BEFORE rebuildFXChain so fxSpec.sampleRate > 0 on the
// offline path. These tests exercise the FULL chain: command -> slot ValueTree
// fmPatchData -> ExportManager tree-copy -> RoutingManager rebuild ->
// Track::rebuildFXChain -> loadFmPatchFromTree -> prepare -> render. If any
// link is a no-op the export must be byte-identical to the init tone — the
// wavBytesDiffer assertion is the invariant detector.

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

// cwd-independent fixture resolution (mirrors file_library_patch_test.cpp's
// virusFixture()): testdata/dx7 is relative to this source file.
juce::File dx7Fixture(const char* name)
{
    juce::File self(__FILE__);
    juce::File dir = juce::File::isAbsolutePath(__FILE__)
        ? self.getParentDirectory().getChildFile("testdata/dx7")
        : juce::File::getCurrentWorkingDirectory().getChildFile(
            "tests/unit/engine/testdata/dx7");
    return dir.getChildFile(name);
}

std::vector<HDAW::Dx7Voice> loadCartridge()
{
    juce::FileInputStream in(dx7Fixture("cartridge.syx"));
    if (!in.openedOk()) return {};
    const juce::int64 size = in.getTotalLength();
    if (size <= 0) return {};
    std::vector<uint8_t> data(static_cast<size_t>(size));
    in.read(data.data(), static_cast<int>(size));
    return HDAW::parseCartridgeSysex(data.data(), data.size());
}

std::string base64Of(const HDAW::Dx7Voice& v)
{
    juce::MemoryBlock block(v.patchData.data(), v.patchData.size());
    return block.toBase64Encoding().toStdString();
}

// THE invariant check: the historical bug produced byte-identical WAVs
// regardless of patch. Any differing byte (or size) refutes it.
bool wavBytesDiffer(const juce::File& a, const juce::File& b)
{
    if (a.getSize() != b.getSize()) return true;
    juce::FileInputStream ia(a), ib(b);
    if (!ia.openedOk() || !ib.openedOk()) return true;
    const int n = static_cast<int>(a.getSize());
    std::vector<uint8_t> ba(static_cast<size_t>(n)), bb(static_cast<size_t>(n));
    ia.read(ba.data(), n);
    ib.read(bb.data(), n);
    return std::memcmp(ba.data(), bb.data(), static_cast<size_t>(n)) != 0;
}

} // namespace

TEST(FmPatchOfflineExport, TwoImportedPatchesRenderDifferently)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    // Deterministic content: internal fm_synth part, fixed seed (no plugins),
    // exactly like master_gain_test.RenderAttenuation.
    ProjectCommands::InstrumentPartParams params;
    params.trackName = "FM Lead";
    params.style = "Lead";
    params.lengthBeats = 4.0;
    params.placement = "region";
    params.count = 1;
    params.seed = 42;
    auto res = cmds.addInstrumentPart(params);
    ASSERT_TRUE(res.error.empty()) << res.error;
    ASSERT_GE(res.trackIndex, 0);

    // The slot is fm_synth on the LIVE processor (not just the ReadModel).
    auto* track = engine.getMainProcessor()->getTrack(res.trackIndex);
    ASSERT_NE(track, nullptr);
    auto& chain = track->getFXChain();
    ASSERT_GE(chain.size(), 1u);
    ASSERT_NE(chain[0], nullptr);
    ASSERT_NE(chain[0]->fmSynthEngine(), nullptr);

    auto voices = loadCartridge();
    ASSERT_GE(voices.size(), 2u);
    int b = 1;
    while (b < static_cast<int>(voices.size()) && voices[b].patchData == voices[0].patchData)
        ++b;
    ASSERT_LT(b, static_cast<int>(voices.size()));

    juce::AudioFormatManager exportFm;
    exportFm.registerBasicFormats();
    auto& em = engine.getMainProcessor()->getExportManager();
    const double duration = HDAW::ExportManager::calculateProjectDuration(engine.getProjectModel());

    auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    const juce::File outA = tempDir.getChildFile("hdaw_fm_patch_A.wav");
    const juce::File outB = tempDir.getChildFile("hdaw_fm_patch_B.wav");
    outA.deleteFile();
    outB.deleteFile();

    // Export 1: cartridge voice 0.
    cmds.setFmPatch(res.trackIndex, 0, base64Of(voices[0]));
    ASSERT_TRUE(em.startExport(engine.getProjectModel().getTree(), exportFm, nullptr,
                               outA, 44100.0, 0.0, duration,
                               HDAW::ExportManager::WAV, 24));
    ASSERT_TRUE(waitForExport(em));

    // Export 2: a different cartridge voice (voice b).
    cmds.setFmPatch(res.trackIndex, 0, base64Of(voices[b]));
    ASSERT_TRUE(em.startExport(engine.getProjectModel().getTree(), exportFm, nullptr,
                               outB, 44100.0, 0.0, duration,
                               HDAW::ExportManager::WAV, 24));
    ASSERT_TRUE(waitForExport(em));

    const float peakA = peakOf(outA);
    const float peakB = peakOf(outB);
    std::cout << "[FmPatchOfflineExport] peakA=" << peakA << " peakB=" << peakB << std::endl;

    ASSERT_GT(peakA, 0.01f) << "patch A offline render is silent";
    ASSERT_GT(peakB, 0.01f) << "patch B offline render is silent";
    ASSERT_TRUE(wavBytesDiffer(outA, outB))
        << "offline FM render is invariant to imported patch bytes — the documented caveat is still real";

    outA.deleteFile();
    outB.deleteFile();
}

TEST(FmPatchOfflineExport, ImportedPatchDiffersFromDefault)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    ProjectCommands::InstrumentPartParams params;
    params.trackName = "FM Lead";
    params.style = "Lead";
    params.lengthBeats = 4.0;
    params.placement = "region";
    params.count = 1;
    params.seed = 42;
    auto res = cmds.addInstrumentPart(params);
    ASSERT_TRUE(res.error.empty()) << res.error;
    ASSERT_GE(res.trackIndex, 0);

    auto voices = loadCartridge();
    ASSERT_GE(voices.size(), 1u);

    juce::AudioFormatManager exportFm;
    exportFm.registerBasicFormats();
    auto& em = engine.getMainProcessor()->getExportManager();
    const double duration = HDAW::ExportManager::calculateProjectDuration(engine.getProjectModel());

    auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    const juce::File outInit = tempDir.getChildFile("hdaw_fm_patch_init.wav");
    const juce::File outImported = tempDir.getChildFile("hdaw_fm_patch_imported.wav");
    outInit.deleteFile();
    outImported.deleteFile();

    // Export 1: NO setFmPatch — the slot holds the DX7 init patch default.
    ASSERT_TRUE(em.startExport(engine.getProjectModel().getTree(), exportFm, nullptr,
                               outInit, 44100.0, 0.0, duration,
                               HDAW::ExportManager::WAV, 24));
    ASSERT_TRUE(waitForExport(em));

    // Export 2: cartridge voice 0 imported into the same slot.
    cmds.setFmPatch(res.trackIndex, 0, base64Of(voices[0]));
    ASSERT_TRUE(em.startExport(engine.getProjectModel().getTree(), exportFm, nullptr,
                               outImported, 44100.0, 0.0, duration,
                               HDAW::ExportManager::WAV, 24));
    ASSERT_TRUE(waitForExport(em));

    const float peakInit = peakOf(outInit);
    const float peakImported = peakOf(outImported);
    std::cout << "[FmPatchOfflineExport] peakInit=" << peakInit
              << " peakImported=" << peakImported << std::endl;

    ASSERT_GT(peakInit, 0.01f) << "default init offline render is silent";
    ASSERT_GT(peakImported, 0.01f) << "imported-patch offline render is silent";
    ASSERT_TRUE(wavBytesDiffer(outInit, outImported))
        << "imported DX7 patch does not change the offline render from the default init tone";

    outInit.deleteFile();
    outImported.deleteFile();
}