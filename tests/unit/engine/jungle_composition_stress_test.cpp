#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/ExportManager.h"
#include "engine/AudioEngineCommands.h"
#include "model/ProjectModel.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <vector>

// JungleComposition suite: the verified 2026-08-29 amen-jungle recipe
// (docs/jungle-dnb-composition-guide.md §4-§6) as a permanent engine-side
// regression/deliverable, mirroring PsytranceComposition.*. 175 BPM, F minor,
// amen 1-bar loops per bar, sub offbeat 8ths with octave drops, snare 2/4,
// 8th hats with 16th rolls, drop-point rumble, a KEPT-ALIVE mini-break, a
// true breakdown with a sub-drone, and a densest finale. Exercises the
// verified automation contract: FX-param lanes (100 + slot*100 + idx), the
// per-beat triangle PUMP on the sub track's BUILT-IN Volume lane (target
// "Volume" - NOT addAutomationLane with paramID 1), drop-open rumble ramps,
// and the breakdown close-then-open sub-cutoff envelope. Skips cleanly when
// E:/samples is absent (CI machines).
namespace {

bool waitForExport(HDAW::ExportManager& em, int timeoutMs = 900000)
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

// Verified kit paths (registered dnb packs). The whole suite skips if any
// file is missing.
const char* const kKitFiles[] = {
    "E:/samples/100 Amen Breaks By Veak - Volume 2/100AB2_Amen_175_ 001.wav",
    "E:/samples/100 Amen Breaks By Veak - Volume 2/100AB2_Amen_175_ 029.wav",
    "E:/samples/Basses/Bass Loops/Vixage Bass Loop - DNBTypeBeat F 174.wav",
    "E:/samples/Bass Shots/BW_D_Bass_One_Shot__Orion.wav",
    "E:/samples/lingoturbo Mini Drum Pack/snares/lingoturbo_snare_drum_dnb.wav",
    "E:/samples/lingoturbo Mini Drum Pack/hats/lingoturbo_drum_hats_small.wav",
    "E:/samples/lingoturbo Mini Drum Pack/kicks/lingoturbo_drum_kick_boom.wav",
    "E:/samples/Rumble Loops/Odd Frequency - Decode 2 - Rumble Bass 8 - F - 135BPM.wav",
};
bool kitPresent()
{
    for (const char* p : kKitFiles)
        if (!juce::File(p).existsAsFile()) return false;
    return true;
}

// Per-beat triangle points (verified pump: 4 pts/beat, dip to sv at the
// beat, peak ev at +0.5, res 0.25). cycles = number of triangles across
// [start,end] (per-beat when 0). Written to the built-in "Volume" lane.
std::vector<std::pair<double, float>> triPoints(double start, double end,
                                                double cycles, double sv,
                                                double ev, double res)
{
    std::vector<std::pair<double, float>> pts;
    const double span = end - start;
    const double period = (cycles > 0.0) ? span / cycles : 1.0;
    const int n = static_cast<int>(std::ceil(span / res)) + 1;
    for (int i = 0; i < n; ++i)
    {
        const double t = start + i * res;
        if (t > end + 1e-9) break;
        const double frac = std::fmod(t - start, std::max(period, 1e-9)) / period;
        const float v = (frac < 0.5)
            ? static_cast<float>(sv + (ev - sv) * (frac / 0.5))
            : static_cast<float>(ev - (ev - sv) * ((frac - 0.5) / 0.5));
        pts.push_back({ t, v });
    }
    return pts;
}

struct Kit { int breaksA, breaksB, sub, snare, hats, kick, rumble; };

// Build the verified amen-jungle arrangement: one sampler track per role,
// one clip per role spanning the whole track at beat 0 (clip-local note
// starts == absolute beats), internal FX, section score, automation lanes.
// dMinorSub picks the alternate verified sub (BW_D Orion shot, D-minor
// roots) instead of the F-key Vixage loop.
Kit buildArrangement(AudioEngine& engine, ProjectCommands& cmds,
                     bool dMinorSub, int totalBeats)
{
    const int miniEnd   = 288;   // bars 64-71, KEPT ALIVE
    const int mainBEnd  = 416;   // bars 72-103
    const int brkEnd    = 448;   // bars 104-111, true breakdown
    const bool hasMini  = totalBeats > miniEnd;
    const bool hasMainB = totalBeats > mainBEnd;
    const bool hasBrk   = totalBeats > brkEnd;
    std::array<int, 4> roots = dMinorSub
        ? std::array<int, 4>{ 38, 34, 41, 36 }   // D minor
        : std::array<int, 4>{ 41, 39, 37, 39 };  // F minor

    Kit kit;
    kit.breaksA = cmds.addTrack("Jungle_AmenA", -1, -1, 0);
    kit.breaksB = cmds.addTrack("Jungle_AmenB", -1, -1, 0);
    kit.sub     = cmds.addTrack(dMinorSub ? "Jungle_SubDmin" : "Jungle_SubF", -1, -1, 0);
    kit.snare   = cmds.addTrack("Jungle_Snare", -1, -1, 0);
    kit.hats    = cmds.addTrack("Jungle_Hat", -1, -1, 0);
    kit.kick    = cmds.addTrack("Jungle_Kick", -1, -1, 0);
    kit.rumble  = cmds.addTrack("Jungle_Rumble", -1, -1, 0);
    EXPECT_GE(kit.breaksA, 0); EXPECT_GE(kit.breaksB, 0); EXPECT_GE(kit.sub, 0);
    EXPECT_GE(kit.snare, 0);  EXPECT_GE(kit.hats, 0);    EXPECT_GE(kit.kick, 0);
    EXPECT_GE(kit.rumble, 0);

    // Samplers at natural-pitch rootNotes (§3 map). Faders per §2.
    cmds.addFxSlot(kit.breaksA, "sampler", 0, "");
    cmds.setSamplerSample(kit.breaksA, 0, kKitFiles[0], 60);   // amen 001, root 60
    cmds.setTrackVolume(kit.breaksA, 0.85f);
    cmds.addFxSlot(kit.breaksB, "sampler", 0, "");
    cmds.setSamplerSample(kit.breaksB, 0, kKitFiles[1], 60);   // amen 029 (loop B)
    cmds.setTrackVolume(kit.breaksB, 0.85f);
    cmds.addFxSlot(kit.sub, "sampler", 0, "");
    cmds.setSamplerSample(kit.sub, 0,
        dMinorSub ? kKitFiles[3] : kKitFiles[2],              // BW_D shot 48 / Vixage F 41
        dMinorSub ? 48 : 41);
    cmds.setTrackVolume(kit.sub, 0.75f);
    cmds.addFxSlot(kit.snare, "sampler", 0, "");
    cmds.setSamplerSample(kit.snare, 0, kKitFiles[4], 60);
    cmds.setTrackVolume(kit.snare, 0.85f);
    cmds.addFxSlot(kit.hats, "sampler", 0, "");
    cmds.setSamplerSample(kit.hats, 0, kKitFiles[5], 60);
    cmds.setTrackVolume(kit.hats, 0.78f);
    cmds.addFxSlot(kit.kick, "sampler", 0, "");
    cmds.setSamplerSample(kit.kick, 0, kKitFiles[6], 36);     // kick has no usable pitch
    cmds.setTrackVolume(kit.kick, 0.90f);
    cmds.addFxSlot(kit.rumble, "sampler", 0, "");
    cmds.setSamplerSample(kit.rumble, 0, kKitFiles[7], 41);
    cmds.setTrackVolume(kit.rumble, 0.60f);

    // Internal FX (verified recipe): break reverb wash + comp + HP-sweep eq;
    // sub comp/eq; snare + hats reverb.
    cmds.addFxSlot(kit.breaksA, "reverb", 1, "");
    cmds.setFxSlotParam(kit.breaksA, 1, 0, 0.55f);   // mix
    cmds.setFxSlotParam(kit.breaksA, 1, 2, 0.38f);   // size
    cmds.addFxSlot(kit.breaksA, "compressor", 2, "");
    cmds.setFxSlotParam(kit.breaksA, 2, 0, -16.0f);   // thr dB
    cmds.addFxSlot(kit.breaksA, "eq", 3, "");
    cmds.setFxSlotParam(kit.breaksA, 3, 0, 300.0f);  // freq Hz (BreakHP target)
    cmds.addFxSlot(kit.breaksB, "compressor", 1, "");
    cmds.setFxSlotParam(kit.breaksB, 1, 0, -16.0f);
    cmds.addFxSlot(kit.sub, "eq", 1, "");
    cmds.addFxSlot(kit.sub, "compressor", 2, "");
    cmds.setFxSlotParam(kit.sub, 2, 0, -20.0f);      // thr dB
    cmds.setFxSlotParam(kit.sub, 2, 1, 3.0f);        // ratio
    cmds.addFxSlot(kit.snare, "reverb", 1, "");
    cmds.setFxSlotParam(kit.snare, 1, 0, 0.30f);
    cmds.setFxSlotParam(kit.snare, 1, 2, 0.20f);
    cmds.addFxSlot(kit.hats, "reverb", 1, "");
    cmds.setFxSlotParam(kit.hats, 1, 0, 0.20f);
    cmds.setFxSlotParam(kit.hats, 1, 2, 0.18f);
    cmds.addFxSlot(kit.rumble, "eq", 1, "");         // RumSweep target
    engine.drainPendingRoutingRebuild();

    using Note = std::pair<int, double>;
    // One clip per role spanning the whole track at beat 0 (clip-local note
    // starts == absolute beats); per-section velocity groups addNotes onto it.
    auto openClip = [&](int track) -> int {
        const int clipId = cmds.addMidiClip(track, 0.0, totalBeats, "pattern");
        EXPECT_GE(clipId, 0);   // EXPECT (non-fatal) - this lambda returns int
        return clipId;
    };
    auto score = [&](int clipId, const std::vector<Note>& notes, int vel, double durBeats) {
        addNotes(cmds, clipId, notes, vel, durBeats);
    };

    // ---- BREAKS -------------------------------------------------
    // intro: wisps (1 per 4 bars); build: enters (vel 62); mainA: full
    // (vel 88); mini: hushed but ALIVE (vel 68); finale: densest (vel 104).
    const int cAmA = openClip(kit.breaksA);
    {
        std::vector<Note> intro;
        for (int bar = 0; bar < 16; ++bar)
            if (bar % 4 == 0) intro.push_back({ 60, bar * 4.0 });
        score(cAmA, intro, 48, 4.0);
        std::vector<Note> build;
        for (int bar = 16; bar < 32; ++bar) build.push_back({ 60, bar * 4.0 });
        score(cAmA, build, 62, 4.0);
        std::vector<Note> mainA;
        for (int bar = 32; bar < 64; ++bar) mainA.push_back({ 60, bar * 4.0 });
        score(cAmA, mainA, 88, 4.0);
        if (hasMini)
        {
            std::vector<Note> mini;
            for (int bar = 64; bar < 72; ++bar) mini.push_back({ 60, bar * 4.0 });
            score(cAmA, mini, 68, 4.0);   // kept ALIVE, hushed
        }
        std::vector<Note> finA;
        for (int bar = 112; bar < 160 && bar * 4 < totalBeats; ++bar)
            finA.push_back({ 60, bar * 4.0 });
        score(cAmA, finA, 104, 4.0);
    }
    const int cAmB = openClip(kit.breaksB);
    {
        std::vector<Note> mainB;
        if (hasMainB)
            for (int bar = 72; bar < 104; ++bar) mainB.push_back({ 60, bar * 4.0 });
        score(cAmB, mainB, 92, 4.0);   // loop B for mainB
        std::vector<Note> finB;
        for (int bar = 112; bar < 160 && bar * 4 < totalBeats; ++bar)
            finB.push_back({ 60, bar * 4.0 });
        score(cAmB, finB, 90, 4.0);    // layered over A in the finale
    }

    // ---- SUB ----------------------------------------------------
    // Offbeat 8ths at b+0.5 on the bar's root (F minor 41/39/37/39, or
    // D minor 38/34/41/36 for the BW_D variant), octave drop at 16-beat
    // marks (bar % 4 == 0); held sub-drone (own clip, long) in the break.
    const int cSub = openClip(kit.sub);
    {
        std::vector<Note> sub;
        auto subBar = [&](int bar0, int bar1) {
            for (int bar = bar0; bar < bar1 && bar * 4 < totalBeats; ++bar)
            {
                const int root = roots[bar % 4];
                for (int b = bar * 4; b < bar * 4 + 4; ++b)
                    sub.push_back({ root, b + 0.5 });
                if (bar % 4 == 0)
                    sub.push_back({ root - 12, bar * 4.0 + 0.5 }); // octave drop
            }
        };
        subBar(24, 32);   // build: offbeats from 96
        subBar(32, 64);   // mainA
        if (hasMainB) subBar(72, 104);  // mainB
        subBar(112, 160); // finale (guard inside)
        score(cSub, sub, 100, dMinorSub ? 0.35 : 0.4);
    }
    if (hasBrk)   // true breakdown: sub-DRONE only (kick/bass OFF, break OFF)
    {
        const int cDrone = openClip(kit.sub);
        std::vector<Note> drone = { { roots[0], 416.0 } };
        score(cDrone, drone, 70, 32.0);
    }

    // ---- SNARE on 2/4 (build/mainA/mainB/finale; gone in mini+breakdown)
    const int cSn = openClip(kit.snare);
    {
        std::vector<Note> sn;
        auto snareSect = [&](int bar0, int bar1) {
            for (int bar = bar0; bar < bar1 && bar * 4 < totalBeats; ++bar)
            {
                const double b = bar * 4.0;
                sn.push_back({ 60, b + 1.0 });
                sn.push_back({ 60, b + 3.0 });
            }
        };
        snareSect(16, 32); snareSect(32, 64);
        if (hasMainB) snareSect(72, 104);
        snareSect(112, 160);
        score(cSn, sn, 96, 0.3);
    }

    // ---- HATS: 8ths (+16th rolls at 8-bar marks); the mini is exactly
    // 8 per bar (per-bar generation - the duplicate-stack trap); finale 16ths.
    const int cHh = openClip(kit.hats);
    {
        std::vector<Note> hh;
        auto hatBar = [&](int bar0, int bar1, bool rolls) {
            for (int bar = bar0; bar < bar1 && bar * 4 < totalBeats; ++bar)
            {
                for (int b = bar * 4; b < bar * 4 + 4; ++b)
                {
                    hh.push_back({ 60, b });
                    hh.push_back({ 60, b + 0.5 });
                    if (rolls && bar % 8 == 4)
                    {
                        hh.push_back({ 60, b + 0.75 });
                        hh.push_back({ 60, b + 1.0 });
                        hh.push_back({ 60, b + 1.25 });
                    }
                }
            }
        };
        hatBar(16, 32, false);  // build 8ths
        hatBar(32, 64, true);   // mainA (+ rolls)
        if (hasMini) hatBar(64, 72, false);  // mini: 8/bar kept alive
        if (hasMainB) hatBar(72, 104, true); // mainB (+ rolls)
        score(cHh, hh, 78, 0.18);
        std::vector<Note> fin;
        for (int bar = 112; bar < 160 && bar * 4 < totalBeats; ++bar)  // finale 16ths
            for (int b = bar * 4; b < bar * 4 + 4; ++b)
            {
                fin.push_back({ 60, b });
                fin.push_back({ 60, b + 0.25 });
                fin.push_back({ 60, b + 0.5 });
                fin.push_back({ 60, b + 0.75 });
            }
        score(cHh, fin, 84, 0.18);
    }

    // ---- KICK: sparse (1 of every 2 bars in mains; 1+3 in the finale;
    // 3 ints in the build; none in mini/breakdown) - jungle is break-driven.
    const int cKk = openClip(kit.kick);
    {
        std::vector<Note> kk;
        for (int b : { 68, 84, 116 }) if (b < totalBeats) kk.push_back({ 36, b });
        for (int bar = 32; bar < 64; bar += 2) kk.push_back({ 36, bar * 4.0 });
        if (hasMainB)
            for (int bar = 72; bar < 104; bar += 2) kk.push_back({ 36, bar * 4.0 });
        for (int bar = 112; bar < 160 && bar * 4 < totalBeats; ++bar)
        {
            kk.push_back({ 36, bar * 4.0 });
            kk.push_back({ 36, bar * 4.0 + 2.0 });
        }
        score(cKk, kk, 112, 1.0);
    }

    // ---- RUMBLE at drop points (long swells) -------------------
    const int cRb = openClip(kit.rumble);
    {
        std::vector<Note> rb;
        const int rumbleAt[] = { 16, 48, 128, 288, 416, 448 };
        for (int b : rumbleAt)
            if (b < totalBeats && (b != 288 || hasMainB) && (b != 416 || hasBrk))
                rb.push_back({ 41, b });
        score(cRb, rb, 100, 10.0);
    }

    // ---- AUTOMATION (verified contract) ------------------------
    auto addLane = [&](int track, const juce::String& name, int paramID) {
        ASSERT_TRUE(cmds.addAutomationLane(track, name.toStdString(), paramID));
    };
    auto setLane = [&](int track, const juce::String& name,
                       const std::vector<std::pair<double, float>>& points) {
        for (const auto& [t, v] : points)
            cmds.addAutomationPoint(track, name.toStdString(), t, v);
        cmds.setAutomationEnabled(track, name.toStdString(), true);
        // Contract: automated lanes must be enabled on the tree (default off
        // is a silent no-op at playback/export).
        auto trackList = engine.getProjectModel().getTrackListTree();
        auto al = trackList.getChild(track).getChildWithName(IDs::AUTOMATION_LIST);
        for (auto lane : al)
            if (lane.getProperty(IDs::name, "").toString() == name)
                EXPECT_TRUE(lane.getProperty(IDs::automationEnabled, false).toString() != "0");
    };

    // Break HP lane (eq slot3 param0 -> 100 + 3*100 + 0 = 400): rises
    // build->mainA, ducks for the mini, re-enters for mainB/finale.
    {
        std::vector<std::pair<double, float>> pts = {
            { 64.0, 0.10f }, { 128.0, 0.35f }, { 256.0, 0.10f },
            { 288.0, 0.30f }, { 416.0, 0.45f }, { 448.0, 0.55f },
            { 640.0, 0.55f } };
        pts.erase(std::remove_if(pts.begin(), pts.end(),
                                 [&](const auto& p) { return p.first > totalBeats; }),
                  pts.end());
        addLane(kit.breaksA, "BreakHP", 400);
        setLane(kit.breaksA, "BreakHP", pts);
    }

    // Sub EQ cutoff macro (eq slot1 param0 -> 200): ramp across the track +
    // the close-then-open breakdown envelope (0.58 -> 0.05 through the
    // breakdown, open to 0.80 by 464 into the finale) - verified shape.
    {
        std::vector<std::pair<double, float>> pts = {
            { 64.0, 0.12f },  { 128.0, 0.30f }, { 256.0, 0.42f },
            { 288.0, 0.34f }, { 416.0, 0.58f } };
        if (hasBrk)
        {
            const std::pair<double, float> close[] = {
                { 418.0, 0.56f }, { 424.0, 0.50f }, { 432.0, 0.34f },
                { 440.0, 0.15f }, { 448.0, 0.05f }, { 456.0, 0.40f }, { 464.0, 0.80f } };
            for (const auto& p : close)
                if (p.first < totalBeats) pts.push_back(p);
        }
        pts.push_back({ (double)totalBeats, 0.70f });
        addLane(kit.sub, "SubCutoff", 200);
        setLane(kit.sub, "SubCutoff", pts);
    }

    // Rumble drop-open ramps (eq slot1 param0 -> 200): the sweep opens
    // 0.15 -> 0.95 across each drop (mainA / mainB / finale / end swell).
    {
        std::vector<std::pair<double, float>> pts = { { 0.0, 0.25f }, { 56.0, 0.50f } };
        const std::pair<double, float> ramps[] = {
            { 124.0, 0.15f }, { 132.0, 0.95f },
            { 284.0, 0.15f }, { 292.0, 0.95f },
            { 436.0, 0.15f }, { 452.0, 0.95f } };
        for (const auto& p : ramps)
            if (p.first < totalBeats) pts.push_back(p);   // ramp start time
        pts.push_back({ (double)std::max(64, totalBeats - 12), 0.15f });
        pts.push_back({ (double)std::max(64, totalBeats - 4), 0.95f });
        addLane(kit.rumble, "RumSweep", 200);
        setLane(kit.rumble, "RumSweep", pts);
    }

    // PUMP: per-beat triangles on the sub's BUILT-IN Volume lane (verified
    // 0.72 dip at the beat, 1.0 at +0.5, res 0.25). Do NOT addAutomationLane
    // for it - the built-in lane already exists (adding paramID 1 errors).
    {
        const auto pump = triPoints(64.0, totalBeats, totalBeats - 64, 0.72, 1.0, 0.25);
        for (const auto& [t, v] : pump)
            cmds.addAutomationPoint(kit.sub, "Volume", t, v);
        cmds.setAutomationEnabled(kit.sub, "Volume", true);
    }

    engine.drainPendingRoutingRebuild();
    return kit;
}

// One render at the given master gain into outFile; returns the measured
// peak (1.0 on failure) and sets ok. Uses the ExportManager render thread
// (safe with the message pump started in test_main).
float renderJungle(AudioEngine& engine, ProjectCommands& cmds,
                   juce::AudioFormatManager& exportFm, const juce::File& out,
                   float masterGain, bool& ok)
{
    auto* mp = engine.getMainProcessor();
    auto& em = mp->getExportManager();
    auto computePeak = [&](const juce::File& f) {
        std::unique_ptr<juce::AudioFormatReader> rdr(exportFm.createReaderFor(f));
        if (rdr == nullptr) return 1.0f;
        juce::AudioBuffer<float> buf(2, static_cast<int>(rdr->lengthInSamples));
        rdr->read(&buf, 0, static_cast<int>(rdr->lengthInSamples), 0, true, true);
        return (std::max)(buf.getMagnitude(0, 0, static_cast<int>(rdr->lengthInSamples)),
                          buf.getMagnitude(1, 0, static_cast<int>(rdr->lengthInSamples)));
    };
    cmds.setMasterGain(masterGain);
    out.deleteFile();
    ok = em.startExport(engine.getProjectModel().getTree(), exportFm,
                        &engine.getPluginManager(), out, 48000.0, 0.0,
                        HDAW::ExportManager::calculateProjectDuration(engine.getProjectModel()),
                        HDAW::ExportManager::WAV, 24);
    if (!ok) return 1.0f;
    ok = waitForExport(em, 900000);
    if (!ok) return 1.0f;
    EXPECT_GT(out.getSize(), 1000);
    return computePeak(out);
}

// Canary at -12 dB, infer the true peak (24-bit WAV pins at full scale when
// the sum is hot), scale the master to ~0.90 dBFS, return finalPeak.
float renderCanaryAndFinal(AudioEngine& engine, ProjectCommands& cmds,
                           juce::AudioFormatManager& exportFm,
                           const juce::File& outDir, const juce::String& tag,
                           bool keep, bool saveLoadBetween, bool& allOk)
{
    const juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    const juce::File canaryOut = keep
        ? outDir.getChildFile("jungle_composition_" + tag + "_canary.wav")
        : tempDir.getChildFile("hdaw_jungle_cmp_canary.wav");
    const juce::File finalOut = keep
        ? outDir.getChildFile("jungle_composition_" + tag + "_final.wav")
        : tempDir.getChildFile("hdaw_jungle_cmp_final.wav");

    bool ok = false;
    const float canaryPeak = renderJungle(engine, cmds, exportFm, canaryOut, 0.25f, ok);
    if (!ok) { allOk = false; return -1.0f; }
    if (canaryPeak > 0.9f)  // canary clipped - faders too hot
    {
        EXPECT_LE(canaryPeak, 0.9f) << "canary clipped - faders too hot";
        allOk = false;
        return -1.0f;
    }
    const float truePeak = canaryPeak / 0.25f;
    const float finalGain = (std::min)(0.90f / truePeak, 1.0f);
    if (!keep) canaryOut.deleteFile();

    if (saveLoadBetween)
    {
        // Save + load -> FULL routing rebuild between the two renders (the
        // session-accumulation pattern; state must survive and the 2nd render
        // is the FINAL deliverable).
        engine.drainPendingRoutingRebuild();
        const juce::File proj = tempDir.getChildFile("hdaw_jungle_cmp_iter1.hdaw");
        proj.deleteFile();
        if (!cmds.saveProject(proj.getFullPathName().toStdString()))
        {
            EXPECT_TRUE(false) << "saveProject failed before final render";
            allOk = false;
            return -1.0f;
        }
        if (!cmds.loadProject(proj.getFullPathName().toStdString()))
        {
            EXPECT_TRUE(false) << "loadProject failed before final render";
            allOk = false;
            return -1.0f;
        }
        proj.deleteFile();
        engine.drainPendingRoutingRebuild();
    }

    ok = false;
    const float finalPeak = renderJungle(engine, cmds, exportFm, finalOut, finalGain, ok);
    if (!ok) { allOk = false; return -1.0f; }
    EXPECT_LE(finalPeak, 0.95f) << "final render clipped";
    EXPECT_GE(finalPeak, 0.35f) << "final render too quiet";
    if (!keep) finalOut.deleteFile();
    juce::Logger::writeToLog("JungleComposition[" + tag + "]: canary="
        + juce::String(canaryPeak, 3) + " truePeak=" + juce::String(truePeak, 3)
        + " finalGain=" + juce::String(finalGain, 3)
        + " finalPeak=" + juce::String(finalPeak, 3));
    return finalPeak;
}

} // namespace


// Main regression: the FULL verified arc (640 beats ~ 3:39 at 175) with every
// section, FX chain and automation lane, save/load accumulation between the
// canary and the final render (render 2 = final deliverable).
TEST(JungleComposition, AmenFminFullArcWithAccumulation)
{
    if (!kitPresent())
        GTEST_SKIP() << "jungle kit samples absent (E:/samples)";

    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.setTempo(175.0);
    engine.drainPendingRoutingRebuild();

    buildArrangement(engine, cmds, /*dMinorSub=*/false, /*totalBeats=*/640);

    juce::AudioFormatManager exportFm;
    exportFm.registerBasicFormats();
    const juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    static const bool keep = std::getenv("HDAW_KEEP_JUNGLE_RENDERS") != nullptr;
    const juce::File outDir = keep
        ? juce::File("D:/pdf/roo projects/hdaw3/.tmp_dnb_theme")
        : tempDir;

    bool allOk = true;
    const float finalPeak = renderCanaryAndFinal(engine, cmds, exportFm, outDir,
                                                 "amen_fmin", keep,
                                                 /*saveLoadBetween=*/true, allOk);
    EXPECT_GT(finalPeak, 0.20f);   // gate: peak within [0.2..1.0]
    EXPECT_LE(finalPeak, 1.00f);
    ASSERT_TRUE(allOk);
}


// Render test / alternate verified sub: the BW_D Orion shot (root 48, D-minor
// roots 38/34/41/36) on the compact 256-beat arc (~87 s). Canary + final are
// rendered as deliverables (jungle_composition_dmin_*.wav when kept).
TEST(JungleComposition, DminSubVariantRenders)
{
    if (!kitPresent())
        GTEST_SKIP() << "jungle kit samples absent (E:/samples)";

    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.setTempo(175.0);
    engine.drainPendingRoutingRebuild();

    buildArrangement(engine, cmds, /*dMinorSub=*/true, /*totalBeats=*/256);

    juce::AudioFormatManager exportFm;
    exportFm.registerBasicFormats();
    const juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    static const bool keep = std::getenv("HDAW_KEEP_JUNGLE_RENDERS") != nullptr;
    const juce::File outDir = keep
        ? juce::File("D:/pdf/roo projects/hdaw3/.tmp_dnb_theme")
        : tempDir;

    bool allOk = true;
    const float finalPeak = renderCanaryAndFinal(engine, cmds, exportFm, outDir,
                                                 "dmin", keep,
                                                 /*saveLoadBetween=*/false, allOk);
    EXPECT_GT(finalPeak, 0.20f);
    EXPECT_LE(finalPeak, 1.00f);
    ASSERT_TRUE(allOk);
}
