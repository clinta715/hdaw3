#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/ExportManager.h"
#include "engine/AudioEngineCommands.h"
#include "engine/ProjectSerializer.h"
#include "engine/TrackFXSlot.h"
#include "model/ProjectModel.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

// §3-context stress with the NEW psytrance packs (E:\samples, indexed
// 2026-08-27): generate a psytrance arrangement from pack samples — sampler
// tracks + phrases + automation lanes driving internal FX (EQ freq, phaser
// centre-freq/depth, flanger rate), the exact lane set of the 8/27 crash —
// then run long renders with save/load accumulation. Run via
// run_with_capture.ps1 so a debug-CRT abort yields a procdump.
namespace {

bool waitForExport(HDAW::ExportManager& em, int timeoutMs = 600000)
{
    const auto deadline = juce::Time::getMillisecondCounter() + timeoutMs;
    while (em.isExporting() && juce::Time::getMillisecondCounter() < deadline)
        juce::Thread::sleep(10);
    return !em.isExporting();
}

void addNotes(ProjectCommands& cmds, int clipId,
              const std::vector<std::pair<int, double>>& pattern,
              int velocity, double durBeats)
{
    for (const auto& [pitch, start] : pattern)
        cmds.addNote(clipId, pitch, velocity, start, durBeats);
}

// Load the per-role sample selection produced by the timbre-lib pipeline
// (select_psy_samples.py -> psy_sample_selection.tsv). Entries are files
// from the registered HDAW psytrance libraries that were indexed + analyzed
// (per-file .timbre.json sidecars). skipPerRole drops the first N files per
// role so a second run can use a different set.
static std::vector<std::pair<juce::String, juce::String>> loadSelection(int skipPerRole)
{
    std::vector<std::pair<juce::String, juce::String>> out;
    const juce::File tsv("D:/pdf/roo projects/hdaw3/timbre-lib/psy_sample_selection.tsv");
    if (!tsv.existsAsFile()) return out;
    const juce::StringArray lines = juce::StringArray::fromLines(tsv.loadFileAsString());
    std::map<juce::String, int> perRole;
    for (const auto& line : lines)
    {
        if (line.trim().isEmpty()) continue;
        const int tab = line.indexOfChar('\t');
        if (tab <= 0) continue;
        const juce::String role = line.substring(0, tab);
        const juce::String path = line.substring(tab + 1).upToFirstOccurrenceOf("\t", false, false).trim();
        if (path.isEmpty()) continue;
        if (perRole[role]++ < skipPerRole) continue;
        out.push_back({ role, path });
    }
    return out;
}

// P1-2 analysis helper (2026-08-29): band-limited (2k-8k) energy of channel 0
// in dB. 4096-point FFT; bin k covers k*sr/N Hz; magnitudes come from
// performFrequencyOnlyForwardTransform (first N/2+1 entries valid). Used by
// the filter tests to prove a lowpass really attenuates the high band.
static double bandEnergyDb(const juce::AudioBuffer<float>& buf, double sr)
{
    constexpr int order = 12;
    const int n = 1 << order; // 4096
    juce::dsp::FFT fft(order);
    std::vector<float> data(static_cast<size_t>(2 * n), 0.0f);
    const float* src = buf.getReadPointer(0);
    const int m = std::min(n, buf.getNumSamples());
    for (int i = 0; i < m; ++i)
        data[static_cast<size_t>(i)] = src[i];
    fft.performFrequencyOnlyForwardTransform(data.data(), true);
    const double binHz = sr / n;
    const int k0 = static_cast<int>(std::ceil(2000.0 / binHz));
    const int k1 = std::min(static_cast<int>(std::floor(8000.0 / binHz)), n / 2);
    double energy = 0.0;
    for (int k = k0; k <= k1; ++k)
    {
        const double mag = data[static_cast<size_t>(k)];
        energy += mag * mag;
    }
    return 10.0 * std::log10(energy + 1e-12);
}

// Static playhead for Track::processBlock automation reads (P1-2 G3): reports
// a fixed time + tempo so the automation lane value at that time (and the
// per-block tempo feed for tempo-synced delay divisions, P1-3) is deterministic.
class FixedPlayHead : public juce::AudioPlayHead
{
public:
    void setTimeSeconds(double s) { seconds_ = s; }
    void setBpm(double b) { bpm_ = b; }
    double getBpm() const { return bpm_; }
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying(true);
        info.setTimeInSeconds(seconds_);
        info.setTimeInSamples(static_cast<juce::int64>(seconds_ * 48000.0));
        info.setBpm(bpm_);
        return info;
    }
private:
    double seconds_ = 0.0;
    double bpm_ = 120.0;
};

} // namespace


TEST(PsytranceComposition, NewPacksLongRenderWithFxAutomation)
{
    // The new packs (all created 2026-08-27; skip cleanly if this machine
    // lacks them, e.g. CI).
    // Samples come from the HDAW library pipeline (registered psytrance
    // libraries, analyzed sidecars) - see timbre-lib/psy_sample_selection.tsv.
    const auto selection = loadSelection(0);
    if (selection.empty())
        GTEST_SKIP() << "library selection TSV missing";
    ASSERT_GE(selection.size(), 8u);

    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.setTempo(142.0);
    engine.drainPendingRoutingRebuild();

    // Tracks: one per selected sample, sampler slot 0.
    int kickTrack = -1, bassTrack = -1, leadTrack = -1, hatTrack = -1, padTrack = -1;
    for (size_t i = 0; i < selection.size(); ++i)
    {
        const juce::String role = selection[i].first.toLowerCase();
        const int t = cmds.addTrack((juce::String("Psy") + role + juce::String(i)).toStdString(), -1, -1, 0);
        ASSERT_GE(t, 0);
        cmds.addFxSlot(t, "sampler", 0, "");
        // rootNote = the played pitch of the role, so samples play at their
        // NATURAL pitch instead of being transposed (hats at -18 semitones
        // turn to mud; the first render was kick+bass-only for this reason).
        const int root = (role == "kick") ? 36
                        : (role == "bass") ? 36
                        : (role == "hat") ? 44
                        : (role == "pad") ? 52
                        : 60;
        cmds.setSamplerSample(t, 0, selection[i].second.toStdString(), root);
        // Gain staging: psytrance is bass-dominant, but hats/leads must be
        // audible against the kick+bass wall (first renders were ~300:1 sub).
        const double vol = (role == "kick") ? 0.85
                         : (role == "bass") ? 0.80
                         : (role == "hat") ? 1.00
                         : (role == "lead") ? 0.95
                         : 0.75; // pad
        cmds.setTrackVolume(t, vol);
        if (role == "kick" && kickTrack < 0) kickTrack = t;
        if (role == "bass" && bassTrack < 0) bassTrack = t;
        if (role == "lead" && leadTrack < 0) leadTrack = t;
        if (role == "hat" && hatTrack < 0)   hatTrack = t;
        if (role == "pad" && padTrack < 0)   padTrack = t;
    }
    ASSERT_GE(kickTrack, 0); ASSERT_GE(bassTrack, 0);
    ASSERT_GE(leadTrack, 0); ASSERT_GE(hatTrack, 0);

    // Internal FX on the §3 lane set: bass EQ, lead phaser, hats flanger.
    cmds.addFxSlot(bassTrack, "eq", 1, "");
    cmds.addFxSlot(leadTrack, "phaser", 1, "");
    cmds.addFxSlot(hatTrack, "flanger", 1, "");

    // 128-beat (32-bar) arrangements.
    auto buildPattern = [&](int track, const std::vector<std::pair<int, double>>& notes,
                            int velocity) {
        const int clipId = cmds.addMidiClip(track, 0.0, 128.0, "pattern");
        ASSERT_GE(clipId, 0);
        addNotes(cmds, clipId, notes, velocity, 1.0);
    };

    std::vector<std::pair<int, double>> kickNotes;
    for (int b = 0; b < 128; ++b) kickNotes.push_back({ 36, b });
    buildPattern(kickTrack, kickNotes, 120);

    std::vector<std::pair<int, double>> bassNotes;
    const int bassRoots[8] = { 36, 36, 43, 41, 38, 38, 45, 43 };
    for (int bar = 0; bar < 8; ++bar) bassNotes.push_back({ bassRoots[bar], bar * 16.0 });
    buildPattern(bassTrack, bassNotes, 110);

    std::vector<std::pair<int, double>> leadNotes;
    const int arp[4] = { 60, 64, 67, 72 };
    for (int b = 0; b < 128; ++b) leadNotes.push_back({ arp[b % 4], b });
    buildPattern(leadTrack, leadNotes, 90);

    std::vector<std::pair<int, double>> hatNotes;
    for (int b = 1; b < 128; b += 2) hatNotes.push_back({ 42, b });
    buildPattern(hatTrack, hatNotes, 80);

    std::vector<std::pair<int, double>> padNotes;
    for (int bar = 0; bar < 8; ++bar)
    {
        padNotes.push_back({ 52, bar * 16.0 });       // long pad note each bar
        padNotes.push_back({ 55, bar * 16.0 + 0.5 }); // + third for movement
    }
    buildPattern(padTrack, padNotes, 70);

    // Automation lanes driving the internal FX (the §3 context).
    auto addLane = [&](int track, const juce::String& name, int paramID) {
        ASSERT_TRUE(cmds.addAutomationLane(track, name.toStdString(), paramID));
    };
    auto setLane = [&](int track, const juce::String& name,
                       const std::vector<std::pair<double, float>>& points) {
        for (const auto& [t, v] : points)
            cmds.addAutomationPoint(track, name.toStdString(), t, v);
        // Force the lane enabled (source of truth for playback/export).
        auto trackList = engine.getProjectModel().getTrackListTree();
        auto al = trackList.getChild(track).getChildWithName(IDs::AUTOMATION_LIST);
        for (auto lane : al)
            if (lane.getProperty(IDs::name, "").toString() == name)
                lane.setProperty(IDs::automationEnabled, true,
                                 &engine.getProjectModel().getUndoManager());
    };

    addLane(bassTrack, "EqFreq", 200);        // eq slot1 param0
    setLane(bassTrack, "EqFreq", { { 0.0, 0.05f }, { 32.0, 0.35f }, { 64.0, 0.70f }, { 96.0, 0.30f }, { 127.0, 0.60f } });
    addLane(leadTrack, "PhaserDepth", 201);   // phaser slot1 param1
    setLane(leadTrack, "PhaserDepth", { { 0.0, 0.3f }, { 32.0, 0.8f }, { 64.0, 0.4f }, { 96.0, 0.7f } });
    addLane(leadTrack, "PhaserCF", 202);      // phaser slot1 param2
    setLane(leadTrack, "PhaserCF", { { 0.0, 0.15f }, { 32.0, 0.85f }, { 64.0, 0.2f }, { 96.0, 0.5f } });
    addLane(hatTrack, "FlangerRate", 200);    // flanger slot1 param0
    setLane(hatTrack, "FlangerRate", { { 0.0, 0.15f }, { 32.0, 0.6f }, { 64.0, 0.3f }, { 96.0, 0.5f } });

    auto* mp = engine.getMainProcessor();
    auto& em = mp->getExportManager();
    juce::AudioFormatManager exportFm;
    exportFm.registerBasicFormats();
    const juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    // HDAW_KEEP_PSY_RENDERS=1 keeps the renders as deliverables in
    // .tmp_dnb_theme/ (default: temp + delete, guarding disk space).
    static const bool keep = std::getenv("HDAW_KEEP_PSY_RENDERS") != nullptr;
    const juce::File outBase = keep
        ? juce::File("D:/pdf/roo projects/hdaw3/.tmp_dnb_theme")
        : tempDir;

    // 3 iterations: render 90 s, save+load (full rebuilds) between — the
    // session-accumulation pattern from the 8/27 crash.
    for (int i = 1; i <= 3; ++i)
    {
        engine.drainPendingRoutingRebuild();
        const juce::File proj = tempDir.getChildFile("hdaw_psy_cmp_iter" + juce::String(i) + ".hdaw");
        proj.deleteFile();
        ASSERT_TRUE(cmds.saveProject(proj.getFullPathName().toStdString()));
        ASSERT_TRUE(cmds.loadProject(proj.getFullPathName().toStdString()));
        proj.deleteFile();
        engine.drainPendingRoutingRebuild();

        const juce::File out = outBase.getChildFile(
            keep ? juce::String("psytrance_newpacks_v1_iter") + juce::String(i) + ".wav"
                 : juce::String("hdaw_psy_cmp_render") + juce::String(i) + ".wav");
        out.deleteFile();
        // Render the ACTUAL arrangement length (+3 s tail) - a fixed 90 s
        // window left ~35 s of dead tail after the 54 s pattern.
        const double renderDur = HDAW::ExportManager::calculateProjectDuration(engine.getProjectModel());
        ASSERT_TRUE(em.startExport(engine.getProjectModel().getTree(), exportFm,
                                   &engine.getPluginManager(), out,
                                   48000.0, 0.0, renderDur,
                                   HDAW::ExportManager::WAV, 24))
            << "iter " << i;
        ASSERT_TRUE(waitForExport(em, 600000)) << "iter " << i << " timed out";
        EXPECT_GT(out.getSize(), 1000) << "iter " << i << " empty render";
        if (keep)
            juce::Logger::writeToLog("PsytranceComposition: KEPT render " + out.getFullPathName());
        else
            out.deleteFile();
        juce::Logger::writeToLog("PsytranceComposition: iter " + juce::String(i) + " done");
    }
}


// Second new-pack arrangement (deliverable variant): different pack bias and
// patterns so it is a genuinely different track; renders are SAVED to
// .tmp_dnb_theme/psytrance_newpacks_v2_*.wav (deliverables, not stress-only).
TEST(PsytranceComposition, VariantTwoRendersToDisk)
{
    const juce::File samplesRoot("E:/samples");
    if (!samplesRoot.isDirectory())
        GTEST_SKIP() << "E:/samples not present";

    // Same pipeline selection, but drop the first file of each role so the
    // second arrangement uses DIFFERENT library samples than variant 1.
    const auto selection = loadSelection(1);
    if (selection.empty())
        GTEST_SKIP() << "library selection TSV missing";
    ASSERT_GE(selection.size(), 8u);

    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.setTempo(145.0);
    engine.drainPendingRoutingRebuild();

    int kickTrack = -1, bassTrack = -1, leadTrack = -1, hatTrack = -1, padTrack = -1;
    for (size_t i = 0; i < selection.size(); ++i)
    {
        const juce::String role = selection[i].first.toLowerCase();
        const int t = cmds.addTrack((juce::String("Psy2") + role + juce::String(i)).toStdString(), -1, -1, 0);
        ASSERT_GE(t, 0);
        cmds.addFxSlot(t, "sampler", 0, "");
        // Natural-pitch rootNotes (see variant 1).
        const int root = (role == "kick") ? 35
                        : (role == "bass") ? 38
                        : (role == "hat") ? 44
                        : (role == "pad") ? 52
                        : 62;
        cmds.setSamplerSample(t, 0, selection[i].second.toStdString(), root);
        const double vol2 = (role == "kick") ? 0.85
                          : (role == "bass") ? 0.80
                          : (role == "hat") ? 1.00
                          : (role == "lead") ? 0.95
                          : 0.75;
        cmds.setTrackVolume(t, vol2);
        if (role == "kick" && kickTrack < 0) kickTrack = t;
        if (role == "bass" && bassTrack < 0) bassTrack = t;
        if (role == "lead" && leadTrack < 0) leadTrack = t;
        if (role == "hat" && hatTrack < 0)   hatTrack = t;
        if (role == "pad" && padTrack < 0)   padTrack = t;
    }
    ASSERT_GE(kickTrack, 0); ASSERT_GE(bassTrack, 0);
    ASSERT_GE(leadTrack, 0); ASSERT_GE(hatTrack, 0);

    // Different FX pairing: bass EQ, lead FLANGER, hats PHASER (rotated).
    cmds.addFxSlot(bassTrack, "eq", 1, "");
    cmds.addFxSlot(leadTrack, "flanger", 1, "");
    cmds.addFxSlot(hatTrack, "phaser", 1, "");

    auto buildPattern = [&](int track, const std::vector<std::pair<int, double>>& notes,
                            int velocity) {
        const int clipId = cmds.addMidiClip(track, 0.0, 160.0, "pattern");
        ASSERT_GE(clipId, 0);
        addNotes(cmds, clipId, notes, velocity, 1.0);
    };

    // Halftime kick, busier lead (chord arp), swung-ish hats.
    std::vector<std::pair<int, double>> kickNotes;
    for (int b = 0; b < 160; b += 2) kickNotes.push_back({ 35, b });
    buildPattern(kickTrack, kickNotes, 120);

    std::vector<std::pair<int, double>> bassNotes;
    const int bassRoots2[10] = { 38, 38, 33, 41, 36, 36, 45, 43, 38, 41 };
    for (int bar = 0; bar < 10; ++bar) bassNotes.push_back({ bassRoots2[bar], bar * 16.0 });
    buildPattern(bassTrack, bassNotes, 112);

    std::vector<std::pair<int, double>> leadNotes;
    const int arp2[8] = { 62, 65, 69, 74, 65, 69, 77, 74 };
    for (int b = 0; b < 160; ++b) leadNotes.push_back({ arp2[b % 8], b });
    buildPattern(leadTrack, leadNotes, 95);

    std::vector<std::pair<int, double>> hatNotes;
    for (int b = 1; b < 160; b += 2) hatNotes.push_back({ 44, b });
    for (int b = 0; b < 160; b += 4) hatNotes.push_back({ 40, b }); // open hat
    buildPattern(hatTrack, hatNotes, 84);

    std::vector<std::pair<int, double>> padNotes;
    for (int bar = 0; bar < 10; ++bar)
    {
        padNotes.push_back({ 50, bar * 16.0 });
        padNotes.push_back({ 53, bar * 16.0 + 0.5 });
    }
    buildPattern(padTrack, padNotes, 68);

    // Automation (the §3 lane set, rotated): bass EQ freq, lead flanger rate+
    // depth, hats phaser centre-freq.
    auto addLane = [&](int track, const juce::String& name, int paramID) {
        ASSERT_TRUE(cmds.addAutomationLane(track, name.toStdString(), paramID));
    };
    auto setLane = [&](int track, const juce::String& name,
                       const std::vector<std::pair<double, float>>& points) {
        for (const auto& [t, v] : points)
            cmds.addAutomationPoint(track, name.toStdString(), t, v);
        auto trackList = engine.getProjectModel().getTrackListTree();
        auto al = trackList.getChild(track).getChildWithName(IDs::AUTOMATION_LIST);
        for (auto lane : al)
            if (lane.getProperty(IDs::name, "").toString() == name)
                lane.setProperty(IDs::automationEnabled, true,
                                 &engine.getProjectModel().getUndoManager());
    };
    addLane(bassTrack, "EqFreq", 200);
    setLane(bassTrack, "EqFreq", { { 0.0, 0.05f }, { 40.0, 0.4f }, { 80.0, 0.75f }, { 120.0, 0.25f }, { 159.0, 0.6f } });
    addLane(leadTrack, "FlangerRate", 200);
    setLane(leadTrack, "FlangerRate", { { 0.0, 0.2f }, { 40.0, 0.7f }, { 80.0, 0.3f }, { 120.0, 0.55f } });
    addLane(leadTrack, "FlangerDepth", 201);
    setLane(leadTrack, "FlangerDepth", { { 0.0, 0.3f }, { 80.0, 0.9f }, { 159.0, 0.4f } });
    addLane(hatTrack, "PhaserCF", 202);
    setLane(hatTrack, "PhaserCF", { { 0.0, 0.2f }, { 40.0, 0.8f }, { 80.0, 0.35f }, { 120.0, 0.65f } });

    auto* mp = engine.getMainProcessor();
    auto& em = mp->getExportManager();
    juce::AudioFormatManager exportFm;
    exportFm.registerBasicFormats();
    const juce::File outDir("D:/pdf/roo projects/hdaw3/.tmp_dnb_theme");
    ASSERT_TRUE(outDir.isDirectory() || outDir.createDirectory());

    // 2 full-length renders (160 beats @145 BPM ~ 66 s of audio per clip
    // loop; render the whole arrangement twice), saved as deliverables.
    for (int i = 1; i <= 2; ++i)
    {
        engine.drainPendingRoutingRebuild();
        const juce::File out = outDir.getChildFile("psytrance_newpacks_v2_iter" + juce::String(i) + ".wav");
        out.deleteFile();
        const double renderDur = HDAW::ExportManager::calculateProjectDuration(engine.getProjectModel());
        ASSERT_TRUE(em.startExport(engine.getProjectModel().getTree(), exportFm,
                                   &engine.getPluginManager(), out,
                                   48000.0, 0.0, renderDur,
                                   HDAW::ExportManager::WAV, 24))
            << "iter " << i;
        ASSERT_TRUE(waitForExport(em, 600000)) << "iter " << i << " timed out";
        EXPECT_GT(out.getSize(), 1000) << "iter " << i << " empty render";
        juce::Logger::writeToLog("PsytranceComposition: KEPT render " + out.getFullPathName());
    }
}


// Diagnostic: render each role in ISOLATION (mute all other tracks before
// each export - startExport copies the tree, so the live model is intact)
// and report per-role render paths, so mix balance can be judged per source.
TEST(PsytranceComposition, RoleIsolationDiag)
{
    const auto selection = loadSelection(0);
    if (selection.empty())
        GTEST_SKIP() << "library selection TSV missing";

    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.setTempo(142.0);
    engine.drainPendingRoutingRebuild();

    std::map<juce::String, int> anchor;
    juce::StringArray roles;
    for (size_t i = 0; i < selection.size(); ++i)
    {
        const juce::String role = selection[i].first.toLowerCase();
        const int t = cmds.addTrack((juce::String("Diag") + role + juce::String(i)).toStdString(), -1, -1, 0);
        ASSERT_GE(t, 0);
        cmds.addFxSlot(t, "sampler", 0, "");
        const int root = (role == "kick") ? 36 : (role == "bass") ? 36 : (role == "hat") ? 44 : (role == "pad") ? 52 : 60;
        cmds.setSamplerSample(t, 0, selection[i].second.toStdString(), root);
        if (!anchor.count(role)) { anchor[role] = t; roles.add(role); }
    }
    engine.drainPendingRoutingRebuild();

    auto buildPattern = [&](int track, const std::vector<std::pair<int, double>>& notes, int velocity) {
        const int clipId = cmds.addMidiClip(track, 0.0, 128.0, "pattern");
        ASSERT_GE(clipId, 0);
        addNotes(cmds, clipId, notes, velocity, 1.0);
    };
    std::vector<std::pair<int, double>> kickNotes;
    for (int b = 0; b < 128; ++b) kickNotes.push_back({ 36, b });
    buildPattern(anchor["kick"], kickNotes, 120);
    std::vector<std::pair<int, double>> bassNotes;
    for (int bar = 0; bar < 8; ++bar) bassNotes.push_back({ 36 + (bar % 3), bar * 16.0 });
    buildPattern(anchor["bass"], bassNotes, 110);
    std::vector<std::pair<int, double>> leadNotes;
    const int arp[4] = { 60, 64, 67, 72 };
    for (int b = 0; b < 128; ++b) leadNotes.push_back({ arp[b % 4], b });
    buildPattern(anchor["lead"], leadNotes, 90);
    std::vector<std::pair<int, double>> hatNotes;
    for (int b = 1; b < 128; b += 2) hatNotes.push_back({ 44, b });
    buildPattern(anchor["hat"], hatNotes, 80);
    std::vector<std::pair<int, double>> padNotes;
    for (int bar = 0; bar < 8; ++bar)
    {
        padNotes.push_back({ 52, bar * 16.0 });
        padNotes.push_back({ 55, bar * 16.0 + 0.5 });
    }
    buildPattern(anchor["pad"], padNotes, 70);

    auto* mp = engine.getMainProcessor();
    auto& em = mp->getExportManager();
    juce::AudioFormatManager exportFm;
    exportFm.registerBasicFormats();
    const juce::File outDir("D:/pdf/roo projects/hdaw3/.tmp_dnb_theme");

    for (const auto& role : roles)
    {
        auto trackList = engine.getProjectModel().getTrackListTree();
        for (int t = 0; t < trackList.getNumChildren(); ++t)
        {
            const bool muted = (t != anchor[role]);
            trackList.getChild(t).setProperty(IDs::isMuted, muted,
                                              &engine.getProjectModel().getUndoManager());
        }
        engine.drainPendingRoutingRebuild();
        const juce::File out = outDir.getChildFile("psytrance_diag_" + role + ".wav");
        out.deleteFile();
        const double dur = HDAW::ExportManager::calculateProjectDuration(engine.getProjectModel());
        ASSERT_TRUE(em.startExport(engine.getProjectModel().getTree(), exportFm,
                                   &engine.getPluginManager(), out, 48000.0, 0.0, dur,
                                   HDAW::ExportManager::WAV, 24));
        ASSERT_TRUE(waitForExport(em, 600000));
        juce::Logger::writeToLog("RoleIsolationDiag: " + role + " -> " + out.getFullPathName());
    }
}


// Full-production psytrance arrangement (style research: Wikipedia
// "Psychedelic trance" -> constant pounding bass, layers every 4-8 bars to
// a climax, atmospheric intro + mid breakdown; production canon -> offbeat
// rolling bass with an LFO'd filter, 4-on-floor kick with per-beat pumping,
// reverbed hats, delayed/flanged arps, reverbed stabs, wide chorused pads,
// per-track compression). Renders ONE long deliverable + collects peaks.
TEST(PsytranceComposition, FullProductionArrangement)
{
    const auto selection = loadSelection(0);
    if (selection.empty())
        GTEST_SKIP() << "library selection TSV missing";

    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.setTempo(138.0);
    engine.drainPendingRoutingRebuild();

    const int totalBeats = 288; // 72 bars @ 4/4, ~125 s at 138 BPM
    const int kickStart = 32, hatStart = 16, bassStart = 32;
    const int leadStart = 64;
    const int breakdownL = 192, breakdownR = 224;

    std::map<juce::String, std::vector<int>> tracks;
    for (size_t i = 0; i < selection.size(); ++i)
    {
        const juce::String role = selection[i].first.toLowerCase();
        const int t = cmds.addTrack((juce::String("Prod") + role + juce::String(i)).toStdString(), -1, -1, 0);
        ASSERT_GE(t, 0);
        cmds.addFxSlot(t, "sampler", 0, "");
        const int root = (role == "kick") ? 36 : (role == "bass") ? 36 : (role == "hat") ? 44
                       : (role == "lead") ? 62 : (role == "pad") ? 52 : 60;
        cmds.setSamplerSample(t, 0, selection[i].second.toStdString(), root);
        // Faders: keep pre-master headroom - psytrance layers + effects sum
        // HOT, and clipping is PRE-master (master gain cannot rescue it).
        // The earlier 'sum too hot' clip was the EQ-gain bug; with it fixed
        // the real sum is fine at healthy faders. Canary+scale handles the rest.
        const double v3vol = (role == "kick") ? 0.85
                           : (role == "bass") ? 0.80
                           : (role == "hat")  ? 1.00
                           : (role == "lead") ? 0.95
                           : 0.75; // pad
        cmds.setTrackVolume(t, v3vol);
        tracks[role].push_back(t);
    }
    for (const auto& r : { "kick", "bass", "hat", "lead", "pad" })
        ASSERT_FALSE(tracks[r].empty()) << "missing role " << r;
    engine.drainPendingRoutingRebuild();

    const int kickT = tracks["kick"][0], bassT = tracks["bass"][0];
    const int hatT = tracks["hat"][0], leadT = tracks["lead"][0];
    const int stabT = (tracks["lead"].size() > 1) ? tracks["lead"][1] : leadT;
    const int padT = tracks["pad"][0];

    auto buildPattern = [&](int track, const std::vector<std::pair<int, double>>& notes,
                            int velocity, double durBeats) {
        const int clipId = cmds.addMidiClip(track, 0.0, totalBeats, "p");
        ASSERT_GE(clipId, 0);
        addNotes(cmds, clipId, notes, velocity, durBeats);
    };
    auto addFx = [&](int track, const juce::String& type) { cmds.addFxSlot(track, type.toStdString(), 1, ""); };
    auto lfo = [&](int track, int idx, const juce::String& pn, double v) {
        cmds.setLfoParam(track, idx, pn.toStdString(), v);
    };

    // KICK: 4-on-floor from bar 8; DROPS in the breakdown (true break) and
    // returns for the relaunch.
    std::vector<std::pair<int, double>> kk;
    for (int b = kickStart; b < totalBeats; ++b)
    {
        if (b >= breakdownL && b < breakdownR) continue;
        kk.push_back({ 36, b });
    }
    buildPattern(kickT, kk, 122, 1.9);
    addFx(kickT, "compressor");   // slot1
    cmds.setFxSlotParam(kickT, 1, 0, -18.0f);
    cmds.setFxSlotParam(kickT, 1, 1, 4.0f);
    cmds.setFxSlotParam(kickT, 1, 2, 8.0f);
    addFx(kickT, "eq");           // slot2
    cmds.setFxSlotParam(kickT, 2, 0, 3600.0f); // freq Hz (RAW units for setFxSlotParam), gain 0

    // BASS: offbeat 8ths, rolling roots, LFO'd filter, pump, comp.
    std::vector<std::pair<int, double>> be;
    const int rootSeq[8] = { 36, 36, 43, 41, 38, 38, 45, 43 };
    for (int bar = 8; bar < 72; ++bar)
    {
        if (bar >= 12 && bar < 14) continue;
        if (bar >= 48 && bar < 56) continue; // breakdown: bass drops
        const int root = rootSeq[bar % 8];
        const int oct = (bar >= 56) ? 12 : 0; // relaunch lifts an octave
        for (int b = bar * 4; b < bar * 4 + 4; ++b)
            be.push_back({ root + oct + (b % 2), b + 0.5 });
    }
    buildPattern(bassT, be, 112, 0.4);
    addFx(bassT, "eq");           // slot1 (cutoff sweep/LFO via param 0)
    cmds.addFxSlot(bassT, "compressor", 2, ""); // slot2 explicit
    cmds.setFxSlotParam(bassT, 2, 0, -20.0f);   // thr
    cmds.setFxSlotParam(bassT, 2, 1, 3.0f);     // ratio
    cmds.addLfo(bassT);
    lfo(bassT, 0, "waveform", 0);
    lfo(bassT, 0, "rateSync", 1);
    lfo(bassT, 0, "rate", 2.0);
    lfo(bassT, 0, "depth", 0.28);
    lfo(bassT, 0, "bipolar", 1);
    lfo(bassT, 0, "targetParamID", 200);
    cmds.addLfo(bassT);
    lfo(bassT, 1, "waveform", 1);
    lfo(bassT, 1, "rateSync", 1);
    lfo(bassT, 1, "rate", 1.0);
    lfo(bassT, 1, "depth", 0.6);
    lfo(bassT, 1, "bipolar", 1);
    lfo(bassT, 1, "phaseOffset", 180.0);
    lfo(bassT, 1, "targetParamID", 1);

    // HATS: offbeat 8ths + 16th rolls at 8-bar boundaries, reverbed.
    std::vector<std::pair<int, double>> hh;
    for (int bar = 4; bar < 72; ++bar)
    {
        if (bar >= 48 && bar < 54) continue;
        if (bar >= 54 && bar < 56) continue; // breakdown: hats fully out
        for (int b = bar * 4; b < bar * 4 + 4; ++b)
        {
            if (bar < 8)
            {
                hh.push_back({ 44, b }); // sparse intro quarters (soft)
                continue;
            }
            hh.push_back({ 44, b + 0.5 });
            if ((b / 4) % 8 == 4 || bar >= 56) // 16th rolls at 8-bar marks + relaunch
            {
                hh.push_back({ 46, b + 0.75 }); hh.push_back({ 46, b + 1.0 });
                hh.push_back({ 46, b + 1.25 });
            }
            if (bar >= 56 && (b / 4) % 2 == 0) // extra drive in relaunch
                hh.push_back({ 46, b + 0.25 });
        }
    }
    buildPattern(hatT, hh, 92, 0.2);
    addFx(hatT, "reverb");        // slot1
    cmds.setFxSlotParam(hatT, 1, 0, 0.75f);
    cmds.setFxSlotParam(hatT, 1, 2, 0.30f);

    // LEAD ARP: 16ths in main sections; flanger + compressor + reverb; LFO on flanger rate.
    std::vector<std::pair<int, double>> la;
    const int arp[8] = { 62, 65, 69, 74, 65, 69, 77, 74 };
    for (int bar = 16; bar < 72; ++bar)
    {
        if (bar >= 48 && bar < 56) continue; // arp out in breakdown
        for (int b = bar * 4; b < bar * 4 + 4; ++b)
            la.push_back({ arp[(b / 4) % 8] + ((b % 4) == 3 ? 12 : 0), b + (b % 4) * 0.25 });
    }
    buildPattern(leadT, la, 90, 0.2);
    // Breakdown melody: slow, reverbed long notes (call phrase over pads).
    std::vector<std::pair<int, double>> bm;
    const int phrase[4] = { 69, 74, 71, 76 };
    for (int k = 0; k < 4; ++k)
    {
        bm.push_back({ phrase[k], 192.0 + k * 8.0 });
        bm.push_back({ phrase[k] + 5, 196.0 + k * 8.0 });
    }
    {
        const int clipId = cmds.addMidiClip(leadT, 0.0, totalBeats, "bm");
        ASSERT_GE(clipId, 0);
        addNotes(cmds, clipId, bm, 95, 3.0);
    }
    addFx(leadT, "flanger");      // slot1
    cmds.setFxSlotParam(leadT, 1, 0, 0.5f);
    cmds.setFxSlotParam(leadT, 1, 1, 0.6f);
    cmds.addFxSlot(leadT, "compressor", 2, ""); // slot2
    cmds.addFxSlot(leadT, "reverb", 3, "");     // slot3
    cmds.setFxSlotParam(leadT, 3, 0, 0.9f);
    cmds.setFxSlotParam(leadT, 3, 2, 0.22f);
    cmds.addLfo(leadT);
    lfo(leadT, 0, "waveform", 2);
    lfo(leadT, 0, "rateSync", 1);
    lfo(leadT, 0, "rate", 0.5);
    lfo(leadT, 0, "depth", 0.4);
    lfo(leadT, 0, "bipolar", 1);
    lfo(leadT, 0, "targetParamID", 200);

    // STAB: chords on the 2nd beat of each bar, reverbed + delayed.
    std::vector<std::pair<int, double>> st;
    const int ch[8][3] = { {50,54,57}, {53,57,60}, {55,59,62}, {48,52,55},
                           {45,49,52}, {50,53,57}, {52,56,59}, {43,47,50} };
    for (int bar = 16; bar < 72; ++bar)
    {
        if (bar >= 48 && bar < 56) continue;
        const auto& c = ch[bar % 8];
        st.push_back({ c[0], bar * 4.0 + 1.0 });
        st.push_back({ c[1], bar * 4.0 + 1.0 });
        st.push_back({ c[2], bar * 4.0 + 1.0 });
    }
    buildPattern(stabT, st, 96, 1.3);
    addFx(stabT, "reverb");       // slot1
    cmds.setFxSlotParam(stabT, 1, 0, 0.85f);
    cmds.setFxSlotParam(stabT, 1, 2, 0.42f);
    addFx(stabT, "delay");        // slot2
    cmds.setFxSlotParam(stabT, 2, 0, 0.19f);
    cmds.setFxSlotParam(stabT, 2, 1, 0.45f);
    cmds.setFxSlotParam(stabT, 2, 2, 0.22f);

    // PAD: whole arrangement, chorus + reverb + slow volume LFO + riser LFO.
    std::vector<std::pair<int, double>> pp;
    for (int bar = 0; bar < 72; ++bar)
    {
        const double b = bar * 4.0;
        pp.push_back({ 52, b });
        pp.push_back({ 55, b + 0.5 });
    }
    buildPattern(padT, pp, 64, 4.0);
    addFx(padT, "chorus");        // slot1
    cmds.setFxSlotParam(padT, 1, 0, 1.4f);
    cmds.setFxSlotParam(padT, 1, 1, 0.65f);
    cmds.setFxSlotParam(padT, 1, 4, 0.55f);
    addFx(padT, "reverb");        // slot2
    cmds.setFxSlotParam(padT, 2, 0, 0.95f);
    cmds.setFxSlotParam(padT, 2, 2, 0.35f);
    cmds.addLfo(padT);
    lfo(padT, 0, "waveform", 1);
    lfo(padT, 0, "rateSync", 1);
    lfo(padT, 0, "rate", 0.5);
    lfo(padT, 0, "depth", 0.22);
    lfo(padT, 0, "bipolar", 1);
    lfo(padT, 0, "targetParamID", 1);
    // Sidechain-style per-beat duck on the pad too (sums with the slow swell).
    cmds.addLfo(padT);
    lfo(padT, 1, "waveform", 1);
    lfo(padT, 1, "rateSync", 1);
    lfo(padT, 1, "rate", 1.0);
    lfo(padT, 1, "depth", 0.4);
    lfo(padT, 1, "bipolar", 1);
    lfo(padT, 1, "phaseOffset", 180.0);
    lfo(padT, 1, "targetParamID", 1);

    // Macro automation: bass EQ freq sweep; pad chorus-rate riser into breakdown.
    auto setLane = [&](int track, const juce::String& name, int paramID,
                       const std::vector<std::pair<double, float>>& pts) {
        ASSERT_TRUE(cmds.addAutomationLane(track, name.toStdString(), paramID));
        for (const auto& [t, v] : pts)
            cmds.addAutomationPoint(track, name.toStdString(), t, v);
        auto trackList = engine.getProjectModel().getTrackListTree();
        auto al = trackList.getChild(track).getChildWithName(IDs::AUTOMATION_LIST);
        for (auto lane : al)
            if (lane.getProperty(IDs::name, "").toString() == name)
                lane.setProperty(IDs::automationEnabled, true,
                                 &engine.getProjectModel().getUndoManager());
    };
    setLane(bassT, "BassFreqSweep", 200,
            { { 32.0, 0.12f }, { 64.0, 0.28f }, { 96.0, 0.45f }, { 128.0, 0.60f },
              { 160.0, 0.42f }, { 192.0, 0.55f }, { 224.0, 0.50f }, { 288.0, 0.65f } });
    setLane(padT, "PadRiser", 200,
            { { 0.0, 0.10f }, { 188.0, 0.12f }, { 192.0, 0.30f }, { 206.0, 0.70f },
              { 224.0, 0.35f }, { 288.0, 0.40f } });

    engine.drainPendingRoutingRebuild();
    auto* mp = engine.getMainProcessor();
    auto& em = mp->getExportManager();
    juce::AudioFormatManager exportFm;
    exportFm.registerBasicFormats();
    const juce::File outDir("D:/pdf/roo projects/hdaw3/.tmp_dnb_theme");
    const double dur = HDAW::ExportManager::calculateProjectDuration(engine.getProjectModel());
    const juce::File out = outDir.getChildFile("psytrance_production_v3.wav");
    cmds.setMasterGain(1.0f);

    auto computePeak = [&](const juce::File& f) {
        std::unique_ptr<juce::AudioFormatReader> rdr(exportFm.createReaderFor(f));
        if (rdr == nullptr) return 1.0f;
        juce::AudioBuffer<float> buf(2, static_cast<int>(rdr->lengthInSamples));
        rdr->read(&buf, 0, static_cast<int>(rdr->lengthInSamples), 0, true, true);
        const float p0 = buf.getMagnitude(0, 0, static_cast<int>(rdr->lengthInSamples));
        const float p1 = buf.getMagnitude(1, 0, static_cast<int>(rdr->lengthInSamples));
        return (std::max)(p0, p1);
    };
    auto render = [&](float masterGain, bool& ok) {
        cmds.setMasterGain(masterGain);
        out.deleteFile();
        ok = em.startExport(engine.getProjectModel().getTree(), exportFm,
                            &engine.getPluginManager(), out, 48000.0, 0.0, dur,
                            HDAW::ExportManager::WAV, 24);
        if (!ok) return 1.0f;
        ok = waitForExport(em, 900000);
        if (!ok) return 1.0f;
        EXPECT_GT(out.getSize(), 1000);
        return computePeak(out);
    };

    // Canary at -12 dB: 24-bit WAVs pin at full scale, so an over-hot mix
    // reads peak=1.0 no matter the master cut. Measure the TRUE peak at a
    // low master, then scale to ~-1 dBFS for the final render.
    bool ok = false;
    const float canaryPeak = render(0.25f, ok);
    ASSERT_TRUE(ok);
    ASSERT_LE(canaryPeak, 0.9f) << "canary still clipped - faders too hot";
    const float truePeak = canaryPeak / 0.25f;
    const float finalGain = (std::min)(0.90f / truePeak, 1.0f);
    const float finalPeak = render(finalGain, ok);
    ASSERT_TRUE(ok);
    EXPECT_LE(finalPeak, 0.95f) << "final render clipped";
    EXPECT_GE(finalPeak, 0.35f) << "final render too quiet";
    juce::Logger::writeToLog("FullProduction: " + out.getFullPathName()
        + " truePeak=" + juce::String(truePeak, 3) + " finalGain=" + juce::String(finalGain, 3)
        + " finalPeak=" + juce::String(finalPeak, 3) + " dur=" + juce::String(dur, 1));
}


// Diagnostic: with the FullProduction FX chains, WHICH role's FX explodes
// the level? Renders each role alone (mute others) at fader 0.3 / master 0.2
// and logs the measured peak - a sane result is ~0.05-0.2, an exploded one
// pins at 1.0 with high RMS.
TEST(PsytranceComposition, FxExplosionDiag)
{
    const auto selection = loadSelection(0);
    if (selection.empty())
        GTEST_SKIP() << "library selection TSV missing";

    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.setTempo(138.0);
    engine.drainPendingRoutingRebuild();

    std::map<juce::String, std::vector<int>> tracks;
    for (size_t i = 0; i < selection.size(); ++i)
    {
        const juce::String role = selection[i].first.toLowerCase();
        const int t = cmds.addTrack((juce::String("FxDiag") + role + juce::String(i)).toStdString(), -1, -1, 0);
        ASSERT_GE(t, 0);
        cmds.addFxSlot(t, "sampler", 0, "");
        const int root = (role == "kick") ? 36 : (role == "bass") ? 36 : (role == "hat") ? 44
                       : (role == "lead") ? 62 : (role == "pad") ? 52 : 60;
        cmds.setSamplerSample(t, 0, selection[i].second.toStdString(), root);
        cmds.setTrackVolume(t, 0.3);
        tracks[role].push_back(t);
    }
    engine.drainPendingRoutingRebuild();
    const int kickT = tracks["kick"][0], bassT = tracks["bass"][0];
    const int hatT = tracks["hat"][0], leadT = tracks["lead"][0];
    const int stabT = (tracks["lead"].size() > 1) ? tracks["lead"][1] : leadT;
    const int padT = tracks["pad"][0];

    // Same FX as FullProduction, on the anchor tracks only.
    cmds.addFxSlot(kickT, "compressor", 1, "");
    cmds.setFxSlotParam(kickT, 1, 0, -18.0f);
    cmds.setFxSlotParam(kickT, 1, 1, 4.0f);
    cmds.addFxSlot(kickT, "eq", 2, "");
    cmds.setFxSlotParam(kickT, 2, 0, 3600.0f);

    cmds.addFxSlot(bassT, "eq", 1, "");
    cmds.addFxSlot(bassT, "compressor", 2, "");
    cmds.setFxSlotParam(bassT, 2, 0, -20.0f);
    cmds.setFxSlotParam(bassT, 2, 1, 3.0f);

    cmds.addFxSlot(hatT, "reverb", 1, "");
    cmds.setFxSlotParam(hatT, 1, 0, 0.75f);
    cmds.setFxSlotParam(hatT, 1, 2, 0.30f);

    cmds.addFxSlot(leadT, "flanger", 1, "");
    cmds.setFxSlotParam(leadT, 1, 0, 0.5f);
    cmds.setFxSlotParam(leadT, 1, 1, 0.6f);
    cmds.addFxSlot(leadT, "compressor", 2, "");
    cmds.addFxSlot(leadT, "reverb", 3, "");
    cmds.setFxSlotParam(leadT, 3, 0, 0.9f);
    cmds.setFxSlotParam(leadT, 3, 2, 0.22f);

    cmds.addFxSlot(stabT, "reverb", 1, "");
    cmds.setFxSlotParam(stabT, 1, 0, 0.85f);
    cmds.setFxSlotParam(stabT, 1, 2, 0.42f);
    cmds.addFxSlot(stabT, "delay", 2, "");
    cmds.setFxSlotParam(stabT, 2, 0, 0.19f);
    cmds.setFxSlotParam(stabT, 2, 1, 0.45f);
    cmds.setFxSlotParam(stabT, 2, 2, 0.22f);

    cmds.addFxSlot(padT, "chorus", 1, "");
    cmds.setFxSlotParam(padT, 1, 0, 1.4f);
    cmds.setFxSlotParam(padT, 1, 1, 0.65f);
    cmds.setFxSlotParam(padT, 1, 4, 0.55f);
    cmds.addFxSlot(padT, "reverb", 2, "");
    cmds.setFxSlotParam(padT, 2, 0, 0.95f);
    cmds.setFxSlotParam(padT, 2, 2, 0.35f);

    engine.drainPendingRoutingRebuild();
    auto* mp = engine.getMainProcessor();
    auto& em = mp->getExportManager();
    juce::AudioFormatManager exportFm;
    exportFm.registerBasicFormats();
    const juce::File outDir("D:/pdf/roo projects/hdaw3/.tmp_dnb_theme");

    struct RoleNote { const char* role; int pitch; int vel; };
    const RoleNote rn[] = { { "kick", 36, 100 }, { "bass", 36, 100 }, { "hat", 44, 90 },
                            { "lead", 62, 90 }, { "pad", 52, 70 } };
    for (const auto& r : rn)
    {
        const juce::String role(r.role);
        int anch = (role == "stab" ? stabT
                   : role == "kick" ? kickT : role == "bass" ? bassT
                   : role == "hat" ? hatT : role == "lead" ? leadT : padT);
        const int clipId = cmds.addMidiClip(anch, 0.0, 64.0, "p");
        ASSERT_GE(clipId, 0);
        std::vector<std::pair<int, double>> notes;
        for (int b = 0; b < 64; ++b) notes.push_back({ r.pitch, b });
        addNotes(cmds, clipId, notes, r.vel, role == "kick" ? 1.0 : 0.5);
    }
    engine.drainPendingRoutingRebuild();

    auto readPeak = [&](const juce::File& f) {
        std::unique_ptr<juce::AudioFormatReader> rdr(exportFm.createReaderFor(f));
        if (rdr == nullptr) return -1.0f;
        juce::AudioBuffer<float> buf(2, static_cast<int>(rdr->lengthInSamples));
        rdr->read(&buf, 0, static_cast<int>(rdr->lengthInSamples), 0, true, true);
        return (std::max)(buf.getMagnitude(0, 0, static_cast<int>(rdr->lengthInSamples)),
                          buf.getMagnitude(1, 0, static_cast<int>(rdr->lengthInSamples)));
    };
    cmds.setMasterGain(0.2f);
    for (const auto& r : rn)
    {
        const juce::String role(r.role);
        int anch = (role == "kick" ? kickT : role == "bass" ? bassT : role == "hat" ? hatT
                   : role == "lead" ? leadT : padT);
        auto trackList = engine.getProjectModel().getTrackListTree();
        for (int t = 0; t < trackList.getNumChildren(); ++t)
            trackList.getChild(t).setProperty(IDs::isMuted, (t != anch),
                                              &engine.getProjectModel().getUndoManager());
        engine.drainPendingRoutingRebuild();
        const juce::File out = outDir.getChildFile("fxdiag_" + role + ".wav");
        out.deleteFile();
        const double dur = HDAW::ExportManager::calculateProjectDuration(engine.getProjectModel());
        ASSERT_TRUE(em.startExport(engine.getProjectModel().getTree(), exportFm,
                                   &engine.getPluginManager(), out, 48000.0, 0.0, dur,
                                   HDAW::ExportManager::WAV, 24));
        ASSERT_TRUE(waitForExport(em, 600000));
        const float p = readPeak(out);
        juce::Logger::writeToLog("FxDiag " + role + ": peak=" + juce::String(p, 3));
    }
    for (const auto& r : rn) (void)r;
}


// Binary search: which FX element kills the kick track (silence)?
// Renders the SAME kick track with: no-fx / +compressor / +eq /
// +compressor+eq at fader 0.3 + master 0.2, logs each peak (sane ~0.05-0.2).
TEST(PsytranceComposition, FxBinarySearchKick)
{
    const auto selection = loadSelection(0);
    if (selection.empty())
        GTEST_SKIP() << "library selection TSV missing";

    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.setTempo(138.0);
    engine.drainPendingRoutingRebuild();

    int kickT = -1;
    for (size_t i = 0; i < selection.size(); ++i)
    {
        const juce::String role = selection[i].first.toLowerCase();
        const int t = cmds.addTrack((juce::String("BS") + role + juce::String(i)).toStdString(), -1, -1, 0);
        cmds.addFxSlot(t, "sampler", 0, "");
        cmds.setSamplerSample(t, 0, selection[i].second.toStdString(),
                              role == "kick" ? 36 : 44);
        cmds.setTrackVolume(t, 0.3);
        if (role == "kick" && kickT < 0) kickT = t;
    }
    ASSERT_GE(kickT, 0);
    engine.drainPendingRoutingRebuild();
    {
        const int clipId = cmds.addMidiClip(kickT, 0.0, 32.0, "k");
        ASSERT_GE(clipId, 0);
        std::vector<std::pair<int, double>> notes;
        for (int b = 0; b < 32; ++b) notes.push_back({ 36, b });
        addNotes(cmds, clipId, notes, 100, 1.0);
    }
    engine.drainPendingRoutingRebuild();

    auto* mp = engine.getMainProcessor();
    auto& em = mp->getExportManager();
    juce::AudioFormatManager exportFm;
    exportFm.registerBasicFormats();
    const juce::File outDir("D:/pdf/roo projects/hdaw3/.tmp_dnb_theme");
    cmds.setMasterGain(0.2f);

    auto readPeak = [&](const juce::File& f) {
        std::unique_ptr<juce::AudioFormatReader> rdr(exportFm.createReaderFor(f));
        if (rdr == nullptr) return -1.0f;
        juce::AudioBuffer<float> buf(2, static_cast<int>(rdr->lengthInSamples));
        rdr->read(&buf, 0, static_cast<int>(rdr->lengthInSamples), 0, true, true);
        return (std::max)(buf.getMagnitude(0, 0, static_cast<int>(rdr->lengthInSamples)),
                          buf.getMagnitude(1, 0, static_cast<int>(rdr->lengthInSamples)));
    };
    auto renderKick = [&](const juce::String& tag) {
        const juce::File out = outDir.getChildFile("bs_kick_" + tag + ".wav");
        out.deleteFile();
        const double dur = HDAW::ExportManager::calculateProjectDuration(engine.getProjectModel());
        ASSERT_TRUE(em.startExport(engine.getProjectModel().getTree(), exportFm,
                                   &engine.getPluginManager(), out, 48000.0, 0.0, dur,
                                   HDAW::ExportManager::WAV, 24));
        ASSERT_TRUE(waitForExport(em, 600000));
        juce::Logger::writeToLog("BS " + tag + ": peak=" + juce::String(readPeak(out), 3));
    };

    // 1) no FX
    renderKick("nofx");
    // 2) + compressor (slot1)
    cmds.addFxSlot(kickT, "compressor", 1, "");
    engine.drainPendingRoutingRebuild();
    renderKick("comp_only");
    // 3) + eq (slot2)
    cmds.addFxSlot(kickT, "eq", 2, "");
    engine.drainPendingRoutingRebuild();
    renderKick("comp_eq");
    (void)0;
}


// Regression: the internal EQ's gain was passed to makePeakFilter as a
// LINEAR factor while the param defs say dB - the default 0 dB went to
// gainFactor 0.0 and SILENCED every track carrying an EQ (found 2026-08-27
// during psytrance v3; fix: decibelsToGain at both call sites).
TEST(InternalFx, EqDefaultGainPassesAudio)
{
    HDAW::TrackFXSlot slot("eq");
    slot.prepare({ 48000.0, 512, 2 });
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buf(2, 512);
    buf.clear();
    for (int i = 0; i < 512; ++i)
    {
        buf.setSample(0, i, 0.4f * std::sin(i * 0.05f));
        buf.setSample(1, i, 0.4f * std::sin(i * 0.05f));
    }
    slot.process(buf, midi);
    // Default EQ (0 dB gain) must pass audio, not zero it.
    EXPECT_GT(buf.getMagnitude(0, 0, 512), 0.05f);
}

// P1-2 (plan 2026-08-29): HONEST filter sweeps. The internal "filter" type is
// a state-variable low/high/bandpass whose Cutoff attenuates a band — unlike
// the "eq" peak filter, whose Frequency param moves a boost/cut centre and
// leaves the band loud (measured trap, handoff B3). G2: a lowpass @ 200 Hz
// must attenuate the 2k-8k band >= 12 dB vs the SAME slot at 20 kHz, AFTER a
// real full routing rebuild (Gate 1/10: the tree-restored slot must exist and
// process identically).
TEST(InternalFx, FilterLowpassAttenuatesAboveCutoff)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    // Command layer: add + set REAL-unit params (Cutoff=200 Hz, Mode=lowpass,
    // Resonance=0.7).
    cmds.addFxSlot(0, "filter");
    cmds.setFxSlotParam(0, 0, 0, 200.0f);
    cmds.setFxSlotParam(0, 0, 1, 0.0f);
    cmds.setFxSlotParam(0, 0, 2, 0.7f);

    // Gate 1/10: a REAL rebuild must re-create the filter from the tree
    // (Track::rebuildFXChain -> TrackFXSlot("filter") -> loadParamsFromTree)
    // and restore the params onto the LIVE slot.
    engine.drainPendingRoutingRebuild();
    engine.getMainProcessor()->rebuildRoutingGraph();
    engine.drainPendingRoutingRebuild();

    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    ASSERT_GE(track->getNumFXSlots(), 1);
    auto* slot = track->getFXChain().at(0).get();
    ASSERT_EQ(slot->getType().toStdString(), "filter");
    const auto restored = slot->getInternalParamValues();
    ASSERT_GE(restored.size(), 3u);
    EXPECT_NEAR(restored[0], 200.0f, 0.01f);
    EXPECT_NEAR(restored[1], 0.0f, 0.01f);
    EXPECT_NEAR(restored[2], 0.7f, 0.01f);

    // Mixed test signal: 200 Hz (below cutoff, passes) + 4 kHz (above, cut).
    constexpr double sr = 48000.0;
    constexpr int n = 4096;
    juce::AudioBuffer<float> input(2, n);
    for (int i = 0; i < n; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(sr);
        const float v = 0.5f * std::sin(juce::MathConstants<float>::twoPi * 200.0f * t)
                      + 0.5f * std::sin(juce::MathConstants<float>::twoPi * 4000.0f * t);
        input.setSample(0, i, v);
        input.setSample(1, i, v);
    }
    juce::MidiBuffer midi;
    const double openDb = bandEnergyDb(input, sr); // unfiltered reference

    // Cutoff 200: the 2k-8k band must be strongly attenuated.
    juce::AudioBuffer<float> lowOut(input);
    slot->reset();
    slot->process(lowOut, midi);
    const double lowDb = bandEnergyDb(lowOut, sr);

    // Cutoff 20000 (live param update on the SAME restored slot).
    cmds.setFxSlotParam(0, 0, 0, 20000.0f);
    juce::AudioBuffer<float> highOut(input);
    slot->reset();
    slot->process(highOut, midi);
    const double highDb = bandEnergyDb(highOut, sr);

    std::cout << "FilterLowpass: 2k-8k band open=" << openDb
              << " cut200=" << lowDb << " cut20k=" << highDb
              << " dB (diff=" << (highDb - lowDb) << ")" << std::endl;
    EXPECT_GE(highDb - lowDb, 12.0) << "lowpass must attenuate the band >= 12 dB";
    EXPECT_LT(lowDb, openDb - 12.0) << "cut 200 must differ from the open band";
    // Fidelity (lesson 8): a wide-open filter must not ADD band energy.
    EXPECT_LE(std::abs(highDb - openDb), 3.0) << "wide-open lowpass altered the band";
}

// P1-2 G3: an automation lane on filter slot 0 param 0 (paramID
// 100 + slot*100 + 0, values normalized 0..1) drives the DSP end-to-end:
// Track::processBlock reads the lane value at the playhead time and calls
// TrackFXSlot::setAutomationParam -> denormalize -> applyInternalParamToDsp
// -> the live TPT filter. LOW value (20 Hz cutoff) must attenuate the high
// band; HIGH value (~20 kHz cutoff) must pass it.
TEST(InternalFx, FilterCutoffAutomationSweeps)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addFxSlot(0, "filter");        // slot 0 on track 0
    cmds.setFxSlotParam(0, 0, 1, 0.0f); // Mode = lowpass

    // Lane bound to paramID 100 (FX slot 0, param 0 = Cutoff).
    ASSERT_TRUE(cmds.addAutomationLane(0, "FilterCutoff", 100));
    cmds.addAutomationPoint(0, "FilterCutoff", 0.0, 0.0f);  // 0 s -> 20 Hz
    cmds.addAutomationPoint(0, "FilterCutoff", 16.0, 1.0f); // 8 s @120 BPM -> 20 kHz

    // Gate 1/10: automation + FX survive a full routing rebuild.
    engine.drainPendingRoutingRebuild();
    engine.getMainProcessor()->rebuildRoutingGraph();
    engine.drainPendingRoutingRebuild();

    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    ASSERT_GE(track->getNumFXSlots(), 1);

    FixedPlayHead ph;
    track->setPlayHead(&ph);

    constexpr double sr = 48000.0;
    constexpr int n = 4096;
    juce::AudioBuffer<float> input(2, n);
    for (int i = 0; i < n; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(sr);
        const float v = 0.5f * std::sin(juce::MathConstants<float>::twoPi * 200.0f * t)
                      + 0.5f * std::sin(juce::MathConstants<float>::twoPi * 4000.0f * t);
        input.setSample(0, i, v);
        input.setSample(1, i, v);
    }
    juce::MidiBuffer midi;

    // Warm-up pass at each position settles the IIR state; the measured pass
    // uses a fresh input copy so both measurements see identical material.
    auto renderAt = [&](double seconds) {
        ph.setTimeSeconds(seconds);
        juce::AudioBuffer<float> warm(input);
        track->processBlock(warm, midi);
        juce::AudioBuffer<float> out(input);
        track->processBlock(out, midi);
        return bandEnergyDb(out, sr);
    };

    const double lowDb = renderAt(0.0);   // lane value 0.0 -> cutoff 20 Hz
    const double highDb = renderAt(10.0); // lane value 1.0 -> cutoff ~20 kHz

    std::cout << "FilterSweep: low=" << lowDb << " high=" << highDb
              << " dB (diff=" << (highDb - lowDb) << ")" << std::endl;
    EXPECT_GE(highDb - lowDb, 12.0) << "automated cutoff must sweep the band >= 12 dB";
}

// ── P1-3 (plan 2026-08-29): tempo-synced delay divisions ──────────────────
// The internal delay derives Delay Time from Division + project BPM when
// SyncToTempo is on: seconds = divisionBeatFraction * 60 / bpm, clamped to
// the param range 0.01..5 s, re-applied automatically when the tempo changes.
// The gate uses the MATH (f*60/bpm), not the handoff's "dotted-8th ≈ 0.1286"
// label (which is actually a dotted-16th-of-a-half). A first-tap probe: feed
// an impulse at sample 0 through a fresh (reset) delay slot and find the
// first output sample with |amp| > threshold — that is the first reflected
// tap, at exactly delaySamps (roundToInt(delaySeconds * sampleRate)).
TEST(InternalFx, DelaySyncDivisionTracksTempo)
{
    HDAW::TrackFXSlot slot("delay");
    constexpr double sr = 48000.0;
    constexpr int blockSize = 8192;
    slot.prepare({ sr, blockSize, 2 });
    slot.setInternalParam(3, 1.0f); // SyncToTempo on
    slot.setInternalParam(4, 0.0f); // Division = 1/8 (beat fraction 0.125)

    auto firstTap = [&]() {
        juce::AudioBuffer<float> buf(2, blockSize);
        buf.clear();
        buf.setSample(0, 0, 1.0f);
        buf.setSample(1, 0, 1.0f);
        juce::MidiBuffer midi;
        slot.process(buf, midi);
        for (int i = 1; i < blockSize; ++i)
            if (std::fabs(buf.getSample(0, i)) > 0.05f)
                return i;
        return -1;
    };

    slot.reset();
    slot.setTempo(120.0);
    const int tap120 = firstTap();
    slot.reset();
    slot.setTempo(175.0);
    const int tap175 = firstTap();
    ASSERT_GT(tap120, 0) << "no reflected tap at 120 BPM";
    ASSERT_GT(tap175, 0) << "no reflected tap at 175 BPM";

    const double sec120 = static_cast<double>(tap120) / sr;
    const double sec175 = static_cast<double>(tap175) / sr;
    const double ratio = sec120 / sec175;
    std::cout << "DelaySyncTempo: bpm120 tap=" << tap120 << " (" << sec120 << " s)  "
              << "bpm175 tap=" << tap175 << " (" << sec175 << " s)  ratio=" << ratio
              << " (expect ~" << (175.0 / 120.0) << ")" << std::endl;

    // Tempo tracking: first-tap spacing must scale by 175/120 (= 1.4583).
    EXPECT_NEAR(ratio, 175.0 / 120.0, 0.05)
        << "tap spacing must scale with tempo (120 vs 175 BPM)";
    // Absolute math: 1/8 division = 0.125 * 60 / bpm seconds, ±10%.
    EXPECT_NEAR(sec120, 0.125 * 60.0 / 120.0, 0.125 * 60.0 / 120.0 * 0.10);
    EXPECT_NEAR(sec175, 0.125 * 60.0 / 175.0, 0.125 * 60.0 / 175.0 * 0.10);
}

// Same probe at 175 BPM for the dotted divisions: dotted-1/8 (4) = 1.5*1/8 =
// 0.1875 beats -> 0.1875*60/175 s; dotted-1/16 (5) = 1.5*1/16 = 0.09375 beats
// -> 0.09375*60/175 s.
TEST(InternalFx, DelaySyncDivisionDottedEighth)
{
    HDAW::TrackFXSlot slot("delay");
    constexpr double sr = 48000.0;
    constexpr int blockSize = 8192;
    slot.prepare({ sr, blockSize, 2 });
    slot.setInternalParam(3, 1.0f); // SyncToTempo on
    slot.setTempo(175.0);

    auto tapForDivision = [&](int division) {
        slot.setInternalParam(4, static_cast<float>(division));
        slot.reset();
        juce::AudioBuffer<float> buf(2, blockSize);
        buf.clear();
        buf.setSample(0, 0, 1.0f);
        buf.setSample(1, 0, 1.0f);
        juce::MidiBuffer midi;
        slot.process(buf, midi);
        for (int i = 1; i < blockSize; ++i)
            if (std::fabs(buf.getSample(0, i)) > 0.05f)
                return i;
        return -1;
    };

    const int tapDot8 = tapForDivision(4);   // dotted-1/8
    const int tapDot16 = tapForDivision(5);  // dotted-1/16
    ASSERT_GT(tapDot8, 0);
    ASSERT_GT(tapDot16, 0);

    const double secDot8 = static_cast<double>(tapDot8) / sr;
    const double secDot16 = static_cast<double>(tapDot16) / sr;
    std::cout << "DelaySyncDotted: @175 dotted-1/8 tap=" << tapDot8 << " (" << secDot8 << " s)  "
              << "dotted-1/16 tap=" << tapDot16 << " (" << secDot16 << " s)" << std::endl;

    EXPECT_NEAR(secDot8, 0.1875 * 60.0 / 175.0, 0.1875 * 60.0 / 175.0 * 0.10)
        << "dotted-1/8 must be 1.5*1/8 of a beat at 175 BPM";
    EXPECT_NEAR(secDot16, 0.09375 * 60.0 / 175.0, 0.09375 * 60.0 / 175.0 * 0.10)
        << "dotted-1/16 must be 1.5*1/16 of a beat at 175 BPM";
}

// Gate 1/10 (save/load leg + Track feed leg): SyncToTempo/Division survive the
// project XML round trip, the Track::rebuildFXChain factory
// (TrackFXSlot(type) + loadParamsFromTree) restores them, and the restored
// DSP taps at the division-derived delay — identical after load. Also covers
// the Track::processBlock -> setTempo feed: with a FixedPlayHead reporting
// 175 BPM, the track's delay slot reacts to the playhead tempo (no manual
// slot.setTempo call here).
TEST(InternalFx, DelaySyncDivisionRoundTripsProjectXml)
{
    ProjectModel model;
    model.createDefaultProject();
    auto& um = model.getUndoManager();
    auto fxChain = model.getTrackListTree().getChild(0).getChildWithName(IDs::FX_CHAIN);
    ASSERT_TRUE(fxChain.isValid());

    juce::ValueTree slot(IDs::FX_SLOT);
    slot.setProperty(IDs::fxType, juce::String("delay"), &um);
    slot.setProperty(IDs::bypassed, false, &um);
    slot.setProperty(juce::Identifier("param_0"), 0.5, &um); // manual seconds
    slot.setProperty(juce::Identifier("param_1"), 0.3, &um); // feedback
    slot.setProperty(juce::Identifier("param_2"), 0.5, &um); // mix
    slot.setProperty(juce::Identifier("param_3"), 1.0, &um); // SyncToTempo on
    slot.setProperty(juce::Identifier("param_4"), 4.0, &um); // dotted-1/8
    fxChain.addChild(slot, -1, &um);

    const juce::File f = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("hdaw_delay_sync_roundtrip.hdap");
    f.deleteFile();
    ASSERT_TRUE(HDAW::ProjectSerializer::save(model, f));
    ASSERT_TRUE(f.existsAsFile());

    ProjectModel loaded;
    ASSERT_TRUE(HDAW::ProjectSerializer::load(loaded, f));
    auto loadedSlot = loaded.getTrackListTree().getChild(0)
                          .getChildWithName(IDs::FX_CHAIN).getChild(0);
    ASSERT_TRUE(loadedSlot.isValid());
    EXPECT_EQ(loadedSlot.getProperty(IDs::fxType).toString().toStdString(), "delay");
    EXPECT_DOUBLE_EQ(static_cast<double>(loadedSlot.getProperty(juce::Identifier("param_3"))), 1.0);
    EXPECT_DOUBLE_EQ(static_cast<double>(loadedSlot.getProperty(juce::Identifier("param_4"))), 4.0);

    // Factory parses the loaded tree back into a working slot (the exact
    // construction Track::rebuildFXChain performs for internal FX).
    HDAW::TrackFXSlot slot2("delay");
    slot2.prepare({ 48000.0, 512, 2 });
    slot2.loadParamsFromTree(loadedSlot);
    auto vals = slot2.getInternalParamValues();
    ASSERT_GE(vals.size(), 5u);
    EXPECT_NEAR(vals[3], 1.0f, 0.01f);
    EXPECT_NEAR(vals[4], 4.0f, 0.01f);

    // The restored slot's DSP taps at the SAME division-derived delay it had
    // before the save (dotted-1/8 at the default 120 BPM = 0.1875*60/120 s).
    juce::AudioBuffer<float> buf(2, 8192);
    buf.clear();
    buf.setSample(0, 0, 1.0f);
    buf.setSample(1, 0, 1.0f);
    juce::MidiBuffer midi;
    slot2.process(buf, midi);
    int tap = -1;
    for (int i = 1; i < 8192; ++i)
        if (std::fabs(buf.getSample(0, i)) > 0.05f) { tap = i; break; }
    const int expectedTap = juce::roundToInt(0.1875 * 60.0 / 120.0 * 48000.0);
    EXPECT_EQ(tap, expectedTap) << "restored slot must tap at the division-derived delay";
    f.deleteFile();
}

// Track feed leg: Track::processBlock must push the playhead BPM into its FX
// slots each block (without any direct slot.setTempo call). A delay slot on a
// real Track node, SyncToTempo on + Division 1/8, fed an impulse while the
// FixedPlayHead reports 120 BPM then 175 BPM, must tap at the corresponding
// division-derived offsets.
TEST(InternalFx, DelaySyncTrackPlayheadFeedsTempo)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addFxSlot(0, "delay");            // slot 0 on track 0
    cmds.setFxSlotParam(0, 0, 3, 1.0f);    // SyncToTempo on
    cmds.setFxSlotParam(0, 0, 4, 0.0f);    // Division = 1/8
    engine.drainPendingRoutingRebuild();
    engine.getMainProcessor()->rebuildRoutingGraph();
    engine.drainPendingRoutingRebuild();

    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    ASSERT_GE(track->getNumFXSlots(), 1);

    FixedPlayHead ph;
    track->setPlayHead(&ph);

    // AudioEngine::initialize() defaults to 44100 Hz; measure in the track's
    // REAL sample rate so the printed seconds match the engine.
    const double trackSr = track->getSampleRate();
    constexpr int n = 8192;
    auto tapAtBpm = [&](double bpm) {
        ph.setBpm(bpm);
        juce::AudioBuffer<float> buf(2, n);
        buf.clear();
        buf.setSample(0, 0, 1.0f);
        buf.setSample(1, 0, 1.0f);
        juce::MidiBuffer midi;
        track->processBlock(buf, midi);
        for (int i = 1; i < n; ++i)
            if (std::fabs(buf.getSample(0, i)) > 0.05f)
                return i;
        return -1;
    };

    const int tap120 = tapAtBpm(120.0);
    const int tap175 = tapAtBpm(175.0);
    ASSERT_GT(tap120, 0) << "no tap at 120 BPM via Track feed";
    ASSERT_GT(tap175, 0) << "no tap at 175 BPM via Track feed";

    const double sec120 = static_cast<double>(tap120) / trackSr;
    const double sec175 = static_cast<double>(tap175) / trackSr;
    std::cout << "DelaySyncTrackFeed: bpm120 tap=" << tap120 << " (" << sec120 << " s)  "
              << "bpm175 tap=" << tap175 << " (" << sec175 << " s)" << std::endl;
    EXPECT_NEAR(sec120 / sec175, 175.0 / 120.0, 0.05)
        << "Track-fed tempo must re-derive the delay on BPM change";
    // Sanity: taps land at the real division math in the engine's own rate.
    EXPECT_NEAR(sec120, 0.125 * 60.0 / 120.0, 0.125 * 60.0 / 120.0 * 0.10);
    EXPECT_NEAR(sec175, 0.125 * 60.0 / 175.0, 0.125 * 60.0 / 175.0 * 0.10);
}

// P1-2 Gate 1/10 (save/load leg): fxType "filter" + its real-unit param_N
// properties round-trip through the project XML, and the factory that
// Track::rebuildFXChain uses for internal FX (TrackFXSlot(type) +
// loadParamsFromTree) parses the loaded tree back into a working slot.
TEST(InternalFx, FilterTypeRoundTripsProjectXml)
{
    ProjectModel model;
    model.createDefaultProject();
    auto& um = model.getUndoManager();
    auto fxChain = model.getTrackListTree().getChild(0).getChildWithName(IDs::FX_CHAIN);
    ASSERT_TRUE(fxChain.isValid());

    juce::ValueTree slot(IDs::FX_SLOT);
    slot.setProperty(IDs::fxType, juce::String("filter"), &um);
    slot.setProperty(IDs::bypassed, false, &um);
    slot.setProperty(juce::Identifier("param_0"), 350.0, &um);
    slot.setProperty(juce::Identifier("param_1"), 1.0, &um);
    slot.setProperty(juce::Identifier("param_2"), 0.9, &um);
    fxChain.addChild(slot, -1, &um);

    const juce::File f = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("hdaw_filter_roundtrip.hdap");
    f.deleteFile();
    ASSERT_TRUE(HDAW::ProjectSerializer::save(model, f));
    ASSERT_TRUE(f.existsAsFile());

    ProjectModel loaded;
    ASSERT_TRUE(HDAW::ProjectSerializer::load(loaded, f));
    auto loadedSlot = loaded.getTrackListTree().getChild(0)
                          .getChildWithName(IDs::FX_CHAIN).getChild(0);
    ASSERT_TRUE(loadedSlot.isValid());
    EXPECT_EQ(loadedSlot.getProperty(IDs::fxType).toString().toStdString(), "filter");
    EXPECT_DOUBLE_EQ(static_cast<double>(loadedSlot.getProperty(juce::Identifier("param_0"))), 350.0);
    EXPECT_DOUBLE_EQ(static_cast<double>(loadedSlot.getProperty(juce::Identifier("param_1"))), 1.0);
    EXPECT_DOUBLE_EQ(static_cast<double>(loadedSlot.getProperty(juce::Identifier("param_2"))), 0.9);

    // Factory parses the loaded type (the exact construction
    // Track::rebuildFXChain performs for internal FX).
    HDAW::TrackFXSlot slot2("filter");
    slot2.prepare({ 48000.0, 512, 2 });
    slot2.loadParamsFromTree(loadedSlot);
    auto vals = slot2.getInternalParamValues();
    ASSERT_GE(vals.size(), 3u);
    EXPECT_NEAR(vals[0], 350.0f, 0.01f);
    EXPECT_NEAR(vals[1], 1.0f, 0.01f);
    EXPECT_NEAR(vals[2], 0.9f, 0.01f);
    f.deleteFile();
}


// v4: long-form full-production psytrance (distinct palette + Wikipedia-style
// structure). 140 BPM, 136 bars (~4 min): atmos intro -> build -> MAIN A ->
// mini-break -> MAIN B (new phrase, octave bass) -> true breakdown w/ melody
// + wash -> finale. Uses the SECOND sample of each role (loadSelection(1)),
// a delay-based arp instead of flanger, hat reverb+delay, reverse-hat
// downlifters at the two big drops, autofilter LFO on the bass, and the
// canary-inverted master scale.
TEST(PsytranceComposition, FullProductionV4)
{
    const auto selection = loadSelection(1);
    if (selection.empty())
        GTEST_SKIP() << "library selection TSV missing";

    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.setTempo(140.0);
    engine.drainPendingRoutingRebuild();

    const int totalBeats = 544; // 136 bars @ 4/4 ~ 3:53
    const int introE = 32, buildE = 64, mainAEnd = 192, miniEnd = 224;
    const int mainBEnd = 352, brkEnd = 384;

    std::map<juce::String, std::vector<int>> tracks;
    for (size_t i = 0; i < selection.size(); ++i)
    {
        const juce::String role = selection[i].first.toLowerCase();
        const int t = cmds.addTrack((juce::String("V4") + role + juce::String(i)).toStdString(), -1, -1, 0);
        ASSERT_GE(t, 0);
        cmds.addFxSlot(t, "sampler", 0, "");
        const int root = (role == "kick") ? 36 : (role == "bass") ? 36 : (role == "hat") ? 44
                       : (role == "lead") ? 62 : (role == "pad") ? 52 : 60;
        cmds.setSamplerSample(t, 0, selection[i].second.toStdString(), root);
        const double v4vol = (role == "kick") ? 1.00
                           : (role == "bass") ? 0.95
                           : (role == "hat")  ? 1.00
                           : (role == "lead") ? 1.00
                           : 0.85;
        cmds.setTrackVolume(t, v4vol);
        tracks[role].push_back(t);
    }
    for (const auto& r : { "kick", "bass", "hat", "lead", "pad" })
        ASSERT_FALSE(tracks[r].empty()) << "missing role " << r;
    engine.drainPendingRoutingRebuild();

    const int kickT = tracks["kick"][0], bassT = tracks["bass"][0];
    const int hatT = tracks["hat"][0], leadT = tracks["lead"][0];
    const int stabT = (tracks["lead"].size() > 1) ? tracks["lead"][1] : leadT;
    const int padT = tracks["pad"][0];
    const int revT = (tracks["hat"].size() > 1) ? tracks["hat"][1] : hatT;

    auto buildPattern = [&](int track, const std::vector<std::pair<int, double>>& notes,
                            int velocity, double durBeats) {
        const int clipId = cmds.addMidiClip(track, 0.0, totalBeats, "p");
        ASSERT_GE(clipId, 0);
        addNotes(cmds, clipId, notes, velocity, durBeats);
    };
    auto addFx = [&](int track, const juce::String& type, int pos) {
        cmds.addFxSlot(track, type.toStdString(), pos, "");
    };
    auto lfo = [&](int track, int idx, const juce::String& pn, double v) {
        cmds.setLfoParam(track, idx, pn.toStdString(), v);
    };
    auto pumpLfo = [&](int track, int idx, double depth) {
        cmds.addLfo(track);
        lfo(track, idx, "waveform", 1);
        lfo(track, idx, "rateSync", 1);
        lfo(track, idx, "rate", 1.0);
        lfo(track, idx, "depth", depth);
        lfo(track, idx, "bipolar", 1);
        lfo(track, idx, "phaseOffset", 180.0);
        lfo(track, idx, "targetParamID", 1);
    };

    // ---- KICK: 4-on-floor, drops for mini-break + breakdown ----
    std::vector<std::pair<int, double>> kk;
    for (int b = introE; b < totalBeats; ++b)
    {
        if ((b >= mainAEnd && b < miniEnd) || (b >= mainBEnd && b < brkEnd)) continue;
        kk.push_back({ 36, b });
    }
    buildPattern(kickT, kk, 122, 1.9);
    addFx(kickT, "compressor", 1);
    cmds.setFxSlotParam(kickT, 1, 0, -18.0f);
    cmds.setFxSlotParam(kickT, 1, 1, 4.0f);
    addFx(kickT, "eq", 2);
    cmds.setFxSlotParam(kickT, 2, 0, 3600.0f);

    // ---- BASS: rolling offbeat; octave lifts in B + finale; autofilter LFO ----
    std::vector<std::pair<int, double>> be;
    const int rootSeq[8] = { 36, 36, 43, 41, 38, 38, 45, 43 };
    for (int bar = 16; bar < 136; ++bar)
    {
        if ((bar >= 20 && bar < 22) || (bar >= 48 && bar < 56)) continue;
        if ((bar >= 88 && bar < 96)) continue; // true breakdown
        const int oct = (bar >= 56 && bar < 88) ? 12 : (bar >= 96 ? 12 : 0);
        for (int b = bar * 4; b < bar * 4 + 4; ++b)
            be.push_back({ rootSeq[bar % 8] + oct + (b % 2), b + 0.5 });
    }
    buildPattern(bassT, be, 112, 0.4);
    addFx(bassT, "eq", 1);
    addFx(bassT, "compressor", 2);
    cmds.setFxSlotParam(bassT, 2, 0, -20.0f);
    cmds.setFxSlotParam(bassT, 2, 1, 3.0f);
    cmds.addLfo(bassT);
    lfo(bassT, 0, "waveform", 0);
    lfo(bassT, 0, "rateSync", 1);
    lfo(bassT, 0, "rate", 2.0);
    lfo(bassT, 0, "depth", 0.30);
    lfo(bassT, 0, "bipolar", 1);
    lfo(bassT, 0, "targetParamID", 200);
    pumpLfo(bassT, 1, 0.55);

    // ---- HATS: quarters in build, offbeat 8ths + rolls, reverb+delay ----
    std::vector<std::pair<int, double>> hh;
    for (int bar = 8; bar < 136; ++bar)
    {
        if ((bar >= 48 && bar < 56) || (bar >= 88 && bar < 96)) continue;
        for (int b = bar * 4; b < bar * 4 + 4; ++b)
        {
            if (bar < 16) { hh.push_back({ 44, b }); continue; }
            hh.push_back({ 44, b + 0.5 });
            const bool rollPoint = ((b / 4) % 8 == 4) || bar >= 96;
            if (rollPoint)
            {
                hh.push_back({ 46, b + 0.75 }); hh.push_back({ 46, b + 1.0 });
                hh.push_back({ 46, b + 1.25 });
            }
            if (bar >= 96 && (b / 4) % 2 == 0)
                hh.push_back({ 46, b + 0.25 });
        }
    }
    buildPattern(hatT, hh, 92, 0.2);
    addFx(hatT, "reverb", 1);
    cmds.setFxSlotParam(hatT, 1, 0, 0.75f);
    cmds.setFxSlotParam(hatT, 1, 2, 0.30f);
    addFx(hatT, "delay", 2);
    cmds.setFxSlotParam(hatT, 2, 0, 0.156f);
    cmds.setFxSlotParam(hatT, 2, 1, 0.30f);
    cmds.setFxSlotParam(hatT, 2, 2, 0.18f);

    // ---- LEAD ARP: delay+reverb+compressor (B section shifts phrase) ----
    std::vector<std::pair<int, double>> la;
    const int arpA[8] = { 62, 65, 69, 74, 65, 69, 77, 74 };
    const int arpB[8] = { 66, 69, 73, 78, 69, 73, 81, 78 };
    for (int bar = 16; bar < 136; ++bar)
    {
        if ((bar >= 48 && bar < 56) || (bar >= 88 && bar < 96)) continue;
        const int* arp = (bar >= 56) ? arpB : arpA;
        for (int b = bar * 4; b < bar * 4 + 4; ++b)
            la.push_back({ arp[(b / 4) % 8], b + (b % 4) * 0.25 });
    }
    buildPattern(leadT, la, 88, 0.2);
    addFx(leadT, "delay", 1);
    cmds.setFxSlotParam(leadT, 1, 0, 0.19f);
    cmds.setFxSlotParam(leadT, 1, 1, 0.4f);
    cmds.setFxSlotParam(leadT, 1, 2, 0.32f);
    addFx(leadT, "reverb", 2);
    cmds.setFxSlotParam(leadT, 2, 0, 0.9f);
    cmds.setFxSlotParam(leadT, 2, 2, 0.25f);
    addFx(leadT, "compressor", 3);
    cmds.setFxSlotParam(leadT, 3, 0, -16.0f);
    pumpLfo(leadT, 0, 0.45);

    // Breakdown melody (long notes over the wash).
    std::vector<std::pair<int, double>> bm;
    const int phraseB[4] = { 69, 74, 71, 76 };
    for (int k = 0; k < 4; ++k)
    {
        bm.push_back({ phraseB[k], 352.0 + k * 8.0 });
        bm.push_back({ phraseB[k] + 5, 356.0 + k * 8.0 });
    }
    {
        const int clipId = cmds.addMidiClip(leadT, 0.0, totalBeats, "bm");
        ASSERT_GE(clipId, 0);
        addNotes(cmds, clipId, bm, 95, 3.0);
    }

    // ---- STAB: offbeat chords, flanger + reverb ----
    std::vector<std::pair<int, double>> st;
    const int ch[8][3] = { {48,52,55}, {50,54,57}, {52,56,59}, {45,49,52},
                           {50,53,57}, {53,57,60}, {55,59,62}, {47,51,54} };
    for (int bar = 16; bar < 136; ++bar)
    {
        if ((bar >= 48 && bar < 56) || (bar >= 88 && bar < 96)) continue;
        const auto& c = ch[bar % 8];
        st.push_back({ c[0], bar * 4.0 + 1.0 });
        st.push_back({ c[1], bar * 4.0 + 1.0 });
        st.push_back({ c[2], bar * 4.0 + 1.0 });
    }
    buildPattern(stabT, st, 96, 1.3);
    addFx(stabT, "flanger", 1);
    cmds.setFxSlotParam(stabT, 1, 0, 0.5f);
    cmds.setFxSlotParam(stabT, 1, 1, 0.55f);
    addFx(stabT, "reverb", 2);
    cmds.setFxSlotParam(stabT, 2, 0, 0.85f);
    cmds.setFxSlotParam(stabT, 2, 2, 0.40f);

    // ---- PADS: whole track, chorus+reverb, swell + pump ----
    std::vector<std::pair<int, double>> pp;
    for (int bar = 0; bar < 136; ++bar)
    {
        const double b = bar * 4.0;
        pp.push_back({ 52, b });
        pp.push_back({ 55, b + 0.5 });
    }
    buildPattern(padT, pp, 62, 4.0);
    addFx(padT, "chorus", 1);
    cmds.setFxSlotParam(padT, 1, 0, 1.3f);
    cmds.setFxSlotParam(padT, 1, 1, 0.6f);
    cmds.setFxSlotParam(padT, 1, 4, 0.55f);
    addFx(padT, "reverb", 2);
    cmds.setFxSlotParam(padT, 2, 0, 0.95f);
    cmds.setFxSlotParam(padT, 2, 2, 0.35f);
    cmds.addLfo(padT);
    lfo(padT, 0, "waveform", 1);
    lfo(padT, 0, "rateSync", 1);
    lfo(padT, 0, "rate", 0.5);
    lfo(padT, 0, "depth", 0.22);
    lfo(padT, 0, "bipolar", 1);
    lfo(padT, 0, "targetParamID", 1);
    pumpLfo(padT, 1, 0.38);

    // ---- REVERSE-HAT downlifters at the two drops (mainB + finale) ----
    if (revT != hatT)
    {
        // sampler param 8 = Reverse (raw -> setFxSlotParam writes param_N)
        cmds.setFxSlotParam(revT, 0, 8, 1.0f);
    }
    std::vector<std::pair<int, double>> rv = { { 44, 55.5 }, { 44, 95.5 } };
    {
        const int clipId = cmds.addMidiClip(revT, 0.0, totalBeats, "rv");
        ASSERT_GE(clipId, 0);
        addNotes(cmds, clipId, rv, 100, 0.8);
    }

    // ---- Macro automation: dual bass freq sweep + pad riser into drops ----
    auto setLane = [&](int track, const juce::String& name, int paramID,
                       const std::vector<std::pair<double, float>>& pts) {
        ASSERT_TRUE(cmds.addAutomationLane(track, name.toStdString(), paramID));
        for (const auto& [t, v] : pts)
            cmds.addAutomationPoint(track, name.toStdString(), t, v);
        auto trackList = engine.getProjectModel().getTrackListTree();
        auto al = trackList.getChild(track).getChildWithName(IDs::AUTOMATION_LIST);
        for (auto lane : al)
            if (lane.getProperty(IDs::name, "").toString() == name)
                lane.setProperty(IDs::automationEnabled, true,
                                 &engine.getProjectModel().getUndoManager());
    };
    setLane(bassT, "BassSweep", 200,
            { { 64.0, 0.12f }, { 128.0, 0.30f }, { 192.0, 0.55f },
              { 224.0, 0.18f }, { 288.0, 0.45f }, { 352.0, 0.60f },
              { 384.0, 0.25f }, { 480.0, 0.55f }, { 544.0, 0.68f } });
    setLane(padT, "Riser", 200,
            { { 0.0, 0.10f }, { 188.0, 0.12f }, { 191.0, 0.65f }, { 224.0, 0.30f },
              { 348.0, 0.14f }, { 352.0, 0.75f }, { 384.0, 0.35f }, { 544.0, 0.45f } });

    engine.drainPendingRoutingRebuild();
    auto* mp = engine.getMainProcessor();
    auto& em = mp->getExportManager();
    juce::AudioFormatManager exportFm;
    exportFm.registerBasicFormats();
    const juce::File outDir("D:/pdf/roo projects/hdaw3/.tmp_dnb_theme");
    const double dur = HDAW::ExportManager::calculateProjectDuration(engine.getProjectModel());
    const juce::File out = outDir.getChildFile("psytrance_production_v4.wav");
    cmds.setMasterGain(1.0f);

    auto computePeak = [&](const juce::File& f) {
        std::unique_ptr<juce::AudioFormatReader> rdr(exportFm.createReaderFor(f));
        if (rdr == nullptr) return 1.0f;
        juce::AudioBuffer<float> buf(2, static_cast<int>(rdr->lengthInSamples));
        rdr->read(&buf, 0, static_cast<int>(rdr->lengthInSamples), 0, true, true);
        return (std::max)(buf.getMagnitude(0, 0, static_cast<int>(rdr->lengthInSamples)),
                          buf.getMagnitude(1, 0, static_cast<int>(rdr->lengthInSamples)));
    };
    auto render = [&](float masterGain, bool& ok) {
        cmds.setMasterGain(masterGain);
        out.deleteFile();
        ok = em.startExport(engine.getProjectModel().getTree(), exportFm,
                            &engine.getPluginManager(), out, 48000.0, 0.0, dur,
                            HDAW::ExportManager::WAV, 24);
        if (!ok) return 1.0f;
        ok = waitForExport(em, 1200000);
        if (!ok) return 1.0f;
        EXPECT_GT(out.getSize(), 1000);
        return computePeak(out);
    };
    bool ok = false;
    const float canaryPeak = render(0.25f, ok);
    ASSERT_TRUE(ok);
    ASSERT_LE(canaryPeak, 0.9f) << "canary clipped";
    const float truePeak = canaryPeak / 0.25f;
    const float finalGain = (std::min)(0.90f / truePeak, 1.0f);
    const float finalPeak = render(finalGain, ok);
    ASSERT_TRUE(ok);
    EXPECT_LE(finalPeak, 0.95f) << "final render clipped";
    EXPECT_GE(finalPeak, 0.35f) << "final render too quiet";
    juce::Logger::writeToLog("V4: truePeak=" + juce::String(truePeak, 3)
        + " finalGain=" + juce::String(finalGain, 3) + " finalPeak=" + juce::String(finalPeak, 3)
        + " dur=" + juce::String(dur, 1));
}

// F natural minor shared by EVERY voice in v5 (dark forest): pitch-class set
// {F,G,Ab,Bb,C,Db,Eb}. All bass roots, arps, chords, pads and the breakdown
// melody are derived from these 7 degrees - nothing chromatic enters.
static int fMinorDeg(int degree, int octave) // degree 0 = F, octave 3 => F3(53)
{
    int d = degree % 7; if (d < 0) d += 7;
    int oct = degree / 7; if (degree < 0 && (degree % 7) != 0) oct -= 1;
    static const int pc[7] = { 0, 2, 3, 5, 7, 8, 10 }; // F,G,Ab,Bb,C,Db,Eb
    return 12 * (octave + oct) + 17 + pc[d];           // 17 => F offset
}

// v5: DARK FOREST - 150 BPM, F natural minor, deep/detuned-lead palette
// (dark packs prioritized), reverse downlifters, i-VII-VI-VII progressions,
// pads in Fm7 voicings, long-form at ~3:30.
TEST(PsytranceComposition, DarkForestV5)
{
    // Role -> track list, sorted by pack darkness so the anchor tracks use
    // the darkest registered libraries first.
    struct Loaded { juce::String role; juce::String path; juce::String lib; };
    std::vector<Loaded> sel;
    for (const auto& line : juce::StringArray::fromLines(
             juce::File("D:/pdf/roo projects/hdaw3/timbre-lib/psy_sample_selection.tsv").loadFileAsString()))
    {
        const int tab = line.indexOfChar('\t');
        if (tab <= 0) continue;
        const juce::String role = line.substring(0, tab).trim().toLowerCase();
        const juce::String path = line.substring(tab + 1).upToFirstOccurrenceOf("\t", false, false).trim();
        const juce::String lib = line.fromLastOccurrenceOf("\t", false, false).trim();
        if (!role.isEmpty() && !path.isEmpty()) sel.push_back({ role, path, lib });
    }
    if (sel.empty())
        GTEST_SKIP() << "library selection TSV missing";

    const juce::StringArray darkOrder{
        "TerraTech-Psytrance", "SantoGrau-DarkPsy", "Hypnoticum-PsyTrance",
        "Hipotermic-PsyTrance", "FLOW36-Psytrance", "Batuhan-Psy-Fundamentals",
        "Avalon-Psytrance", "Ascend-Psytrance" };
    for (const auto& s : sel)
    {
        int r = darkOrder.indexOf(s.lib);
        if (r < 0) r = 100;
        (void)r;
    }
    // Reorder per role: darkest lib first.
    std::map<juce::String, std::vector<Loaded>> byRole;
    for (const auto& s : sel) byRole[s.role].push_back(s);
    for (auto& [role, list] : byRole)
        std::stable_sort(list.begin(), list.end(), [&](const Loaded& a, const Loaded& b) {
            int ra = darkOrder.indexOf(a.lib); if (ra < 0) ra = 100;
            int rb = darkOrder.indexOf(b.lib); if (rb < 0) rb = 100;
            return ra < rb;
        });

    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.setTempo(150.0);
    engine.drainPendingRoutingRebuild();

    const int totalBeats = 512; // 128 bars @ 4/4 ~ 3:25 at 150 BPM
    const int introE = 32, buildE = 64, mainAEnd = 192, miniEnd = 224;
    const int mainBEnd = 352, brkEnd = 384;

    std::map<juce::String, std::vector<int>> tracks;
    for (const auto& [role, list] : byRole)
    {
        for (const auto& s : list)
        {
            const int t = cmds.addTrack((juce::String("V5") + role + juce::String(tracks[role].size())).toStdString(), -1, -1, 0);
            ASSERT_GE(t, 0);
            cmds.addFxSlot(t, "sampler", 0, "");
            const int root = (role == "kick") ? 41 : (role == "bass") ? 41 : (role == "hat") ? 46
                           : (role == "lead") ? 65 : (role == "pad") ? 53 : 60;
            cmds.setSamplerSample(t, 0, s.path.toStdString(), root);
            cmds.setTrackVolume(t, 0.95);
            tracks[role].push_back(t);
        }
    }
    for (const auto& r : { "kick", "bass", "hat", "lead", "pad" })
        ASSERT_FALSE(tracks[r].empty()) << "missing role " << r;
    engine.drainPendingRoutingRebuild();

    const int kickT = tracks["kick"][0], bassT = tracks["bass"][0];
    const int hatT = tracks["hat"][0], leadT = tracks["lead"][0];
    const int stabT = (tracks["lead"].size() > 1) ? tracks["lead"][1] : leadT;
    const int padT = tracks["pad"][0];
    const int revT = (tracks["hat"].size() > 1) ? tracks["hat"][1] : hatT;

    auto buildPattern = [&](int track, const std::vector<std::pair<int, double>>& notes,
                            int velocity, double durBeats) {
        const int clipId = cmds.addMidiClip(track, 0.0, totalBeats, "p");
        ASSERT_GE(clipId, 0);
        addNotes(cmds, clipId, notes, velocity, durBeats);
    };
    auto addFx = [&](int track, const juce::String& type, int pos) {
        cmds.addFxSlot(track, type.toStdString(), pos, "");
    };
    auto lfo = [&](int track, int idx, const juce::String& pn, double v) {
        cmds.setLfoParam(track, idx, pn.toStdString(), v);
    };
    auto pumpLfo = [&](int track, int idx, double depth) {
        cmds.addLfo(track);
        lfo(track, idx, "waveform", 1);
        lfo(track, idx, "rateSync", 1);
        lfo(track, idx, "rate", 1.0);
        lfo(track, idx, "depth", depth);
        lfo(track, idx, "bipolar", 1);
        lfo(track, idx, "phaseOffset", 180.0);
        lfo(track, idx, "targetParamID", 1);
    };

    // Progression per 4-bar phrase (degrees of F minor), dark i-VII-VI-VII /
    // VI-VII-i-i shapes: A section F Eb Db Eb | main B Db Eb F F
    const int progA[8] = { 0, 5, 4, 5, 0, 5, 4, 5 };
    const int progB[8] = { 4, 5, 0, 0, 4, 5, 2, 2 };

    // ---- KICK: 4-on-floor, drops in mini + true breakdown ----
    std::vector<std::pair<int, double>> kk;
    for (int b = introE; b < totalBeats; ++b)
    {
        if ((b >= mainAEnd && b < miniEnd) || (b >= mainBEnd && b < brkEnd)) continue;
        kk.push_back({ fMinorDeg(0, 2), b }); // F2
    }
    buildPattern(kickT, kk, 122, 1.9);
    addFx(kickT, "compressor", 1);
    cmds.setFxSlotParam(kickT, 1, 0, -18.0f);
    cmds.setFxSlotParam(kickT, 1, 1, 4.0f);
    addFx(kickT, "eq", 2);
    cmds.setFxSlotParam(kickT, 2, 0, 3600.0f);

    // ---- BASS: offbeat 8ths on the progression roots; F2-based, dark ----
    std::vector<std::pair<int, double>> be;
    for (int bar = 16; bar < 128; ++bar)
    {
        if ((bar >= 48 && bar < 56) || (bar >= 88 && bar < 96)) continue;
        const int* prog = (bar >= 56) ? progB : progA;
        const int deg = prog[bar % 8];
        const int oct = (bar >= 96) ? 1 : 0; // finale down an octave for weight
        for (int b = bar * 4; b < bar * 4 + 4; ++b)
            be.push_back({ fMinorDeg(deg, 2 + oct), b + 0.5 });
    }
    buildPattern(bassT, be, 112, 0.4);
    addFx(bassT, "eq", 1);
    addFx(bassT, "compressor", 2);
    cmds.setFxSlotParam(bassT, 2, 0, -20.0f);
    cmds.setFxSlotParam(bassT, 2, 1, 3.0f);
    cmds.addLfo(bassT);
    lfo(bassT, 0, "waveform", 0);
    lfo(bassT, 0, "rateSync", 1);
    lfo(bassT, 0, "rate", 2.0);
    lfo(bassT, 0, "depth", 0.30);
    lfo(bassT, 0, "bipolar", 1);
    lfo(bassT, 0, "targetParamID", 200);
    pumpLfo(bassT, 1, 0.55);

    // ---- HATS: quarters in build, offbeat 8ths + rolls, reverb ----
    std::vector<std::pair<int, double>> hh;
    for (int bar = 8; bar < 128; ++bar)
    {
        if ((bar >= 48 && bar < 56) || (bar >= 88 && bar < 96)) continue;
        for (int b = bar * 4; b < bar * 4 + 4; ++b)
        {
            if (bar < 16) { hh.push_back({ 46, b }); continue; }
            hh.push_back({ 46, b + 0.5 });
            if ((b / 4) % 8 == 4 || bar >= 96)
            {
                hh.push_back({ 48, b + 0.75 }); hh.push_back({ 48, b + 1.0 });
                hh.push_back({ 48, b + 1.25 });
            }
        }
    }
    buildPattern(hatT, hh, 92, 0.2);
    addFx(hatT, "reverb", 1);
    cmds.setFxSlotParam(hatT, 1, 0, 0.75f);
    cmds.setFxSlotParam(hatT, 1, 2, 0.30f);

    // ---- LEAD ARP: 16th chord-tone arps (Fm7 / DbM7 / Eb7 voicings) ----
    // chord tones per progression degree: 0->{0,2,4,5}, 4->{4,6,1,3+7?} use
    // in-scale 7ths: Fm7 = 0,2,4,5 | DbM7 = 4,6,1(+7),3(+7)?? keep triads+ext:
    //   0: F,A,C,Eb(deg 0,2,4,5)  4(Db): Db,F,Ab,C(4,6,0+7,2+7)
    //   5(Eb): Eb,G,Bb,Db(5,1+7,3+7,4+7)  2(Ab): Ab,C,Eb,G(2,4,6,1+7)
    const int chordTones[7][4] = {
        { 0, 2, 4, 5 }, { 1, 3, 5, 6 }, { 2, 4, 6, 1 }, { 3, 5, 0, 2 },
        { 4, 6, 1, 3 }, { 5, 0, 2, 4 }, { 6, 1, 3, 5 } };
    std::vector<std::pair<int, double>> la;
    for (int bar = 16; bar < 128; ++bar)
    {
        if ((bar >= 48 && bar < 56) || (bar >= 88 && bar < 96)) continue;
        const int* prog = (bar >= 56) ? progB : progA;
        const int deg = prog[bar % 8];
        const int* ct = chordTones[deg];
        for (int b = bar * 4; b < bar * 4 + 4; ++b)
        {
            const int c = ct[(b % 4) % 4];
            la.push_back({ fMinorDeg(c, 3) + ((b % 4) == 3 ? 12 : 0), b + (b % 4) * 0.25 });
        }
    }
    buildPattern(leadT, la, 85, 0.2);
    addFx(leadT, "delay", 1);
    cmds.setFxSlotParam(leadT, 1, 0, 0.17f);
    cmds.setFxSlotParam(leadT, 1, 1, 0.4f);
    cmds.setFxSlotParam(leadT, 1, 2, 0.32f);
    addFx(leadT, "reverb", 2);
    cmds.setFxSlotParam(leadT, 2, 0, 0.9f);
    cmds.setFxSlotParam(leadT, 2, 2, 0.25f);
    addFx(leadT, "compressor", 3);
    pumpLfo(leadT, 0, 0.45);

    // Breakdown melody: slow F-minor phrase (degrees), reverbed.
    std::vector<std::pair<int, double>> bm;
    const int phrase[4] = { 0, 2, 4, 5 };
    for (int k = 0; k < 4; ++k)
    {
        bm.push_back({ fMinorDeg(phrase[k], 3), 352.0 + k * 8.0 });
        bm.push_back({ fMinorDeg(phrase[k] + 3, 3), 356.0 + k * 8.0 });
    }
    {
        const int clipId = cmds.addMidiClip(leadT, 0.0, totalBeats, "bm");
        ASSERT_GE(clipId, 0);
        addNotes(cmds, clipId, bm, 95, 3.0);
    }

    // ---- STABS: offbeat triads of the bar's degree ----
    std::vector<std::pair<int, double>> st;
    for (int bar = 16; bar < 128; ++bar)
    {
        if ((bar >= 48 && bar < 56) || (bar >= 88 && bar < 96)) continue;
        const int* prog = (bar >= 56) ? progB : progA;
        const int deg = prog[bar % 8];
        for (int n = 0; n < 3; ++n)
            st.push_back({ fMinorDeg(deg + 2 * n, 3), bar * 4.0 + 1.0 });
    }
    buildPattern(stabT, st, 96, 1.3);
    addFx(stabT, "flanger", 1);
    cmds.setFxSlotParam(stabT, 1, 0, 0.5f);
    cmds.setFxSlotParam(stabT, 1, 1, 0.55f);
    addFx(stabT, "reverb", 2);
    cmds.setFxSlotParam(stabT, 2, 0, 0.9f);
    cmds.setFxSlotParam(stabT, 2, 2, 0.42f);

    // ---- PADS: held Fm7 voicings throughout (intro + breakdown carrier) ----
    std::vector<std::pair<int, double>> pp;
    for (int bar = 0; bar < 128; ++bar)
    {
        const int* prog = (bar >= 56) ? progB : progA;
        const int deg = prog[bar % 8];
        const double b = bar * 4.0;
        pp.push_back({ fMinorDeg(deg, 3), b });
        pp.push_back({ fMinorDeg(deg + 2, 3), b + 0.5 });
        pp.push_back({ fMinorDeg(deg + 4, 3), b + 1.0 });
    }
    buildPattern(padT, pp, 55, 4.0);
    addFx(padT, "chorus", 1);
    cmds.setFxSlotParam(padT, 1, 0, 1.2f);
    cmds.setFxSlotParam(padT, 1, 1, 0.65f);
    cmds.setFxSlotParam(padT, 1, 4, 0.55f);
    addFx(padT, "reverb", 2);
    cmds.setFxSlotParam(padT, 2, 0, 0.95f);
    cmds.setFxSlotParam(padT, 2, 2, 0.38f);
    cmds.addLfo(padT);
    lfo(padT, 0, "waveform", 1);
    lfo(padT, 0, "rateSync", 1);
    lfo(padT, 0, "rate", 0.5);
    lfo(padT, 0, "depth", 0.22);
    lfo(padT, 0, "bipolar", 1);
    lfo(padT, 0, "targetParamID", 1);
    pumpLfo(padT, 1, 0.38);

    // ---- REVERSE downlifters into main A + finale ----
    if (revT != hatT)
        cmds.setFxSlotParam(revT, 0, 8, 1.0f);
    std::vector<std::pair<int, double>> rv = { { 46, 63.5 }, { 46, 127.5 } };
    {
        const int clipId = cmds.addMidiClip(revT, 0.0, totalBeats, "rv");
        ASSERT_GE(clipId, 0);
        addNotes(cmds, clipId, rv, 100, 0.8);
    }

    // ---- Automation: bass sweep, pad riser into the two drops ----
    auto setLane = [&](int track, const juce::String& name, int paramID,
                       const std::vector<std::pair<double, float>>& pts) {
        ASSERT_TRUE(cmds.addAutomationLane(track, name.toStdString(), paramID));
        for (const auto& [t, v] : pts)
            cmds.addAutomationPoint(track, name.toStdString(), t, v);
        auto trackList = engine.getProjectModel().getTrackListTree();
        auto al = trackList.getChild(track).getChildWithName(IDs::AUTOMATION_LIST);
        for (auto lane : al)
            if (lane.getProperty(IDs::name, "").toString() == name)
                lane.setProperty(IDs::automationEnabled, true,
                                 &engine.getProjectModel().getUndoManager());
    };
    setLane(bassT, "BassSweep", 200,
            { { 64.0, 0.12f }, { 128.0, 0.35f }, { 192.0, 0.55f },
              { 224.0, 0.20f }, { 288.0, 0.45f }, { 352.0, 0.60f },
              { 384.0, 0.30f }, { 448.0, 0.55f }, { 512.0, 0.66f } });
    setLane(padT, "Riser", 200,
            { { 0.0, 0.10f }, { 60.0, 0.12f }, { 64.0, 0.70f }, { 128.0, 0.30f },
              { 188.0, 0.14f }, { 192.0, 0.75f }, { 224.0, 0.35f },
              { 348.0, 0.16f }, { 352.0, 0.80f }, { 384.0, 0.35f }, { 512.0, 0.5f } });

    engine.drainPendingRoutingRebuild();
    auto* mp = engine.getMainProcessor();
    auto& em = mp->getExportManager();
    juce::AudioFormatManager exportFm;
    exportFm.registerBasicFormats();
    const juce::File outDir("D:/pdf/roo projects/hdaw3/.tmp_dnb_theme");
    const double dur = HDAW::ExportManager::calculateProjectDuration(engine.getProjectModel());
    const juce::File out = outDir.getChildFile("psytrance_darkforest_v5.wav");
    cmds.setMasterGain(1.0f);

    auto computePeak = [&](const juce::File& f) {
        std::unique_ptr<juce::AudioFormatReader> rdr(exportFm.createReaderFor(f));
        if (rdr == nullptr) return 1.0f;
        juce::AudioBuffer<float> buf(2, static_cast<int>(rdr->lengthInSamples));
        rdr->read(&buf, 0, static_cast<int>(rdr->lengthInSamples), 0, true, true);
        return (std::max)(buf.getMagnitude(0, 0, static_cast<int>(rdr->lengthInSamples)),
                          buf.getMagnitude(1, 0, static_cast<int>(rdr->lengthInSamples)));
    };
    auto render = [&](float masterGain, bool& ok) {
        cmds.setMasterGain(masterGain);
        out.deleteFile();
        ok = em.startExport(engine.getProjectModel().getTree(), exportFm,
                            &engine.getPluginManager(), out, 48000.0, 0.0, dur,
                            HDAW::ExportManager::WAV, 24);
        if (!ok) return 1.0f;
        ok = waitForExport(em, 1200000);
        if (!ok) return 1.0f;
        EXPECT_GT(out.getSize(), 1000);
        return computePeak(out);
    };
    bool ok = false;
    const float canaryPeak = render(0.25f, ok);
    ASSERT_TRUE(ok);
    ASSERT_LE(canaryPeak, 0.9f) << "canary clipped";
    const float truePeak = canaryPeak / 0.25f;
    const float finalGain = (std::min)(0.90f / truePeak, 1.0f);
    const float finalPeak = render(finalGain, ok);
    ASSERT_TRUE(ok);
    EXPECT_LE(finalPeak, 0.95f) << "final render clipped";
    EXPECT_GE(finalPeak, 0.35f) << "final render too quiet";
    juce::Logger::writeToLog("V5: truePeak=" + juce::String(truePeak, 3)
        + " finalPeak=" + juce::String(finalPeak, 3) + " dur=" + juce::String(dur, 1));
}


// ── Saturator internal FX (plan 2026-09-02 Task 2) ──────────────────────────

namespace {

// Correlated (Goertzel) energy in dB of the given frequencies over an exact-
// cycle window of channel 0. The saturator tests render whole seconds of
// 440 Hz so the fundamental and every measured harmonic complete an integer
// number of cycles inside the window (no leakage), and skip the first second
// so the oversampler/DC-blocker startup transient is excluded.
double harmonicEnergyDb (const juce::AudioBuffer<float>& buf, double sr,
                         int startSample, int numSamples,
                         const double* freqs, int numFreqs)
{
    double energy = 0.0;
    const double twoPi = juce::MathConstants<double>::twoPi;
    for (int h = 0; h < numFreqs; ++h)
    {
        double re = 0.0, im = 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            const double s = buf.getSample (0, startSample + i);
            const double ang = -twoPi * freqs[h] * static_cast<double> (startSample + i) / sr;
            re += s * std::cos (ang);
            im += s * std::sin (ang);
        }
        energy += re * re + im * im;
    }
    return 10.0 * std::log10 (energy + 1e-12);
}

} // namespace

// Gate 1: Mix = 0 must be a BIT-IDENTICAL bypass through a real routing
// rebuild — the slot (Mix 0 exact, Drive maxed) leaves the block untouched,
// so a bypassed slot and the active slot render identical buffers. If the
// implementation ever routes Mix=0 through the oversampled path, the halfband
// filters delay the signal by the oversampler latency and this fails.
TEST (InternalFx, SaturatorDryMixBitIdentical)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addFxSlot (0, "saturator");
    cmds.setFxSlotParam (0, 0, 0, 40.0f); // Drive dB max — maximum contrast
    cmds.setFxSlotParam (0, 0, 3, 0.0f);  // Mix = 0 -> exact bypass

    // Gate 1/10 seam: the saturator must survive a REAL rebuild (tree ->
    // TrackFXSlot("saturator") -> prepare -> loadParamsFromTree) with its
    // params restored onto the live slot.
    engine.drainPendingRoutingRebuild();
    engine.getMainProcessor()->rebuildRoutingGraph();
    engine.drainPendingRoutingRebuild();

    auto* track = engine.getMainProcessor()->getTrack (0);
    ASSERT_NE (track, nullptr);
    ASSERT_GE (track->getNumFXSlots(), 1);
    auto* slot = track->getFXChain().at (0).get();
    ASSERT_EQ (slot->getType().toStdString(), "saturator");
    const auto restored = slot->getInternalParamValues();
    ASSERT_GE (restored.size(), 4u);
    EXPECT_FLOAT_EQ (restored[0], 40.0f);
    EXPECT_FLOAT_EQ (restored[3], 0.0f);

    constexpr double sr = 48000.0;
    constexpr int n = 96000;
    juce::AudioBuffer<float> input (2, n);
    for (int i = 0; i < n; ++i)
    {
        const float t = static_cast<float> (i) / static_cast<float> (sr);
        const float v = 0.5f * std::sin (juce::MathConstants<float>::twoPi * 440.0f * t);
        input.setSample (0, i, v);
        input.setSample (1, i, v);
    }

    // Render through the track in DEVICE-SIZED chunks: the slot's oversampler
    // is sized to the prepared maximumBlockSize, so (unlike the blockwise
    // stateless filters) a saturator slot must never see a larger block.
    const int kChunk = (std::max)(1, track->getBlockSize());
    auto renderThroughTrack = [&](juce::AudioBuffer<float>& buf) {
        juce::MidiBuffer m;
        for (int start = 0; start < buf.getNumSamples(); start += kChunk)
        {
            const int count = std::min(kChunk, buf.getNumSamples() - start);
            juce::AudioBuffer<float> block (2, count);
            for (int ch = 0; ch < 2; ++ch)
                block.copyFrom (ch, 0, buf, ch, start, count);
            track->processBlock (block, m);
            for (int ch = 0; ch < 2; ++ch)
                buf.copyFrom (ch, start, block, ch, 0, count);
        }
    };

    juce::AudioBuffer<float> active (input);
    renderThroughTrack (active);
    slot->setBypassed (true);
    juce::AudioBuffer<float> bypassed (input);
    renderThroughTrack (bypassed);

    ASSERT_EQ (active.getNumSamples(), bypassed.getNumSamples());
    long long mismatches = 0;
    for (int ch = 0; ch < active.getNumChannels(); ++ch)
    {
        const float* a = active.getReadPointer (ch);
        const float* b = bypassed.getReadPointer (ch);
        for (int i = 0; i < active.getNumSamples(); ++i)
            if (a[i] != b[i])
                ++mismatches;
    }
    EXPECT_EQ (mismatches, 0) << "Mix=0 must be a bit-identical bypass";
}

// Gate 3: saturation must actually saturate. 440 Hz sine through the live
// (rebuilt) slot at Mix=1: Drive 24 dB must add strictly more 2nd-5th
// harmonic energy than Drive 0 (which is ~transparent). Measured with
// integer-cycle Goertzel correlation, DC-blocker/oversampler warm-up skipped.
TEST (InternalFx, SaturatorDriveAddsHarmonics)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.addFxSlot (0, "saturator"); // defaults: Type SoftTanh, Mix 1, Out 0 dB
    engine.drainPendingRoutingRebuild();
    engine.getMainProcessor()->rebuildRoutingGraph();
    engine.drainPendingRoutingRebuild();

    auto* track = engine.getMainProcessor()->getTrack (0);
    ASSERT_NE (track, nullptr);
    ASSERT_GE (track->getNumFXSlots(), 1);
    auto* slot = track->getFXChain().at (0).get();
    ASSERT_EQ (slot->getType().toStdString(), "saturator");

    constexpr double sr = 48000.0;
    constexpr int n = 96000;      // 2 s
    constexpr int skip = 48000;   // drop the first second (startup transient)
    juce::AudioBuffer<float> input (2, n);
    for (int i = 0; i < n; ++i)
    {
        const float t = static_cast<float> (i) / static_cast<float> (sr);
        const float v = 0.5f * std::sin (juce::MathConstants<float>::twoPi * 440.0f * t);
        input.setSample (0, i, v);
        input.setSample (1, i, v);
    }
    const double harmonics[4] = { 880.0, 1320.0, 1760.0, 2200.0 };

    // DEVICE-SIZED chunks (see SaturatorDryMixBitIdentical): the oversampler
    // is sized to the prepared maximumBlockSize.
    const int kChunk = (std::max)(1, track->getBlockSize());
    auto renderThroughSlot = [&](juce::AudioBuffer<float>& buf) {
        juce::MidiBuffer m;
        for (int start = 0; start < buf.getNumSamples(); start += kChunk)
        {
            const int count = std::min(kChunk, buf.getNumSamples() - start);
            juce::AudioBuffer<float> block (2, count);
            for (int ch = 0; ch < 2; ++ch)
                block.copyFrom (ch, 0, buf, ch, start, count);
            slot->process (block, m);
            for (int ch = 0; ch < 2; ++ch)
                buf.copyFrom (ch, start, block, ch, 0, count);
        }
    };

    cmds.setFxSlotParam (0, 0, 0, 0.0f); // Drive 0 dB
    juce::AudioBuffer<float> clean (input);
    slot->reset();
    renderThroughSlot (clean);
    const double cleanDb = harmonicEnergyDb (clean, sr, skip, n - skip,
                                             harmonics, 4);

    cmds.setFxSlotParam (0, 0, 0, 24.0f); // Drive 24 dB
    juce::AudioBuffer<float> driven (input);
    slot->reset();
    renderThroughSlot (driven);
    const double drivenDb = harmonicEnergyDb (driven, sr, skip, n - skip,
                                              harmonics, 4);

    std::cout << "SaturatorDrive: h2-h5 clean=" << cleanDb
              << " driven=" << drivenDb
              << " dB (delta=" << (drivenDb - cleanDb) << ")" << std::endl;
    EXPECT_GT (drivenDb, cleanDb) << "drive must add 2nd-5th harmonic energy";
}

// Gate 4 / lesson 7: the saturator's oversampler latency must reach the
// track's reported latency exactly once. Track latency before vs after
// adding the slot differs by exactly the slot's reported latency (integer,
// because the oversampler is built with useIntegerLatency=true).
TEST (InternalFx, SaturatorLatencyMatchesOversampler)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    engine.drainPendingRoutingRebuild();
    engine.getMainProcessor()->rebuildRoutingGraph();
    engine.drainPendingRoutingRebuild();

    auto* track = engine.getMainProcessor()->getTrack (0);
    ASSERT_NE (track, nullptr);
    ASSERT_EQ (track->getNumFXSlots(), 0);
    const int before = track->getLatencySamples();

    cmds.addFxSlot (0, "saturator");
    engine.drainPendingRoutingRebuild();
    engine.getMainProcessor()->rebuildRoutingGraph();
    engine.drainPendingRoutingRebuild();

    ASSERT_GE (track->getNumFXSlots(), 1);
    auto* slot = track->getFXChain().at (0).get();
    ASSERT_EQ (slot->getType().toStdString(), "saturator");
    const int slotLat = slot->getLatencySamples();
    EXPECT_GT (slotLat, 0) << "2x oversampling must report latency";
    std::cout << "SaturatorLatency: before=" << before
              << " after=" << track->getLatencySamples()
              << " slot=" << slotLat << std::endl;
    EXPECT_EQ (track->getLatencySamples() - before, slotLat)
        << "Track::updateLatency must sum the internal slot latency once";
}
