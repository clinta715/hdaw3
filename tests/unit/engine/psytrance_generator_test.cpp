#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "engine/PsytranceGenerator.h"
#include "engine/PhraseGenerator.h"
#include "engine/AudioEngineCommands_Helpers.h"
#include "model/ProjectModel.h"
#include "frontend/router/Router_Composition.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <cmath>
#include <map>

// PsytranceGenerator plan gates G1.1/G1.2 (pure generator) + G1.3 (command)
// + G1.5 (RPC router twin). Canonical layout mirrors the verified DarkForestV5
// F-minor section table (guide §4): 512 beats / 128 bars @ 4/4.

namespace {

HDAW::PsytranceParams canonParams()
{
    HDAW::PsytranceParams p;
    p.keyRoot = 5;   // F
    p.scaleMode = 1; // Minor (Aeolian) {0,2,3,5,7,8,10}
    p.density = 0.7;
    p.seed = 42;
    p.sections = {
        { "intro",     0.0,  32.0 },
        { "build",    32.0,  64.0 },
        { "mainA",    64.0, 192.0 },
        { "mini",    192.0, 224.0 },
        { "mainB",   224.0, 352.0 },
        { "breakdown",352.0, 384.0 },
        { "finale",  384.0, 512.0 },
    };
    return p;
}

void mapAllRoles(HDAW::PsytranceParams& p, int base)
{
    p.kick  = base + 0; p.bass = base + 1; p.hat = base + 2; p.arp = base + 3;
    p.stab  = base + 4; p.pad  = base + 5; p.riser = base + 6; p.down = base + 7;
    p.clap  = base + 8;
}

int countRole(const HDAW::PsytranceScore& s, const char* role)
{
    for (const auto& c : s.clips)
        if (c.role == role) return static_cast<int>(c.notes.size());
    return -1;
}

const std::vector<HDAW::PsytranceNote>* notesOf(const HDAW::PsytranceScore& s, const char* role)
{
    for (const auto& c : s.clips)
        if (c.role == role) return &c.notes;
    return nullptr;
}

bool inFMinorPc(int pitch)
{
    static const int kPcs[7] = { 5, 7, 8, 10, 0, 1, 3 }; // F,G,Ab,Bb,C,Db,Eb
    const int pc = ((pitch % 12) + 12) % 12;
    for (int k : kPcs) if (pc == k) return true;
    return false;
}

} // namespace

// ── G1.1: key discipline + section schedule ────────────────────────────────
TEST(PsytranceGenerator, KeyDisciplineAndSectionSchedule)
{
    auto p = canonParams();
    mapAllRoles(p, 0);
    const auto s = HDAW::PsytranceGenerator::generate(p);
    ASSERT_TRUE(s.error.empty()) << s.error;
    EXPECT_EQ(s.totalBeats, 512.0);

    // Every pitched role stays strictly inside the F-minor pitch class set
    // (key discipline, guide §9.7). Unpitched roles (hat/clap) are exempt —
    // hats happen to land on Ab/Bb/C and clap is a noise hit.
    for (const char* role : { "kick", "bass", "arp", "stab", "pad", "riser", "down" })
    {
        const auto* notes = notesOf(s, role);
        ASSERT_NE(notes, nullptr) << "role " << role << " produced no clip";
        ASSERT_FALSE(notes->empty()) << "role " << role << " produced no notes";
        for (const auto& n : *notes)
            EXPECT_TRUE(inFMinorPc(n.pitch)) << role << " note pitch " << n.pitch
                                             << " outside F minor";
    }

    // KICK: F2 (41) 4-on-floor from build (32) onward, silent in mini +
    // breakdown: 416 = 480 beats − 32 (mini) − 32 (breakdown).
    {
        const auto* notes = notesOf(s, "kick");
        ASSERT_EQ(static_cast<int>(notes->size()), 416);
        int silent = 0;
        for (const auto& n : *notes)
        {
            EXPECT_EQ(n.pitch, 41);
            EXPECT_DOUBLE_EQ(n.durationBeats, 1.9);
            EXPECT_DOUBLE_EQ(n.startBeat, std::floor(n.startBeat)); // whole beats
            if (n.startBeat >= 192.0 && n.startBeat < 224.0) ++silent; // mini
            if (n.startBeat >= 352.0 && n.startBeat < 384.0) ++silent; // breakdown
        }
        EXPECT_EQ(silent, 0) << "kick must be silent in mini + breakdown";
        EXPECT_GE(notes->front().startBeat, 32.0);
        EXPECT_LT(notes->back().startBeat, 512.0);
    }

    // BASS: offbeat 8ths (x.5) only in the full stack; octave-2 in mainA,
    // octave-3 in mainB + finale (register spot-check via pitch < 53 / >= 53).
    {
        const auto* notes = notesOf(s, "bass");
        ASSERT_EQ(static_cast<int>(notes->size()), 384); // 128 beats × 3 sections
        int nonOffbeat = 0, sectionLeak = 0, lowOct = 0, highOct = 0, badReg = 0;
        for (const auto& n : *notes)
        {
            if (std::abs(n.startBeat - std::floor(n.startBeat) - 0.5) > 1e-6)
                ++nonOffbeat;
            if (n.startBeat < 64.0 || (n.startBeat >= 192.0 && n.startBeat < 224.0)
                || (n.startBeat >= 352.0 && n.startBeat < 384.0))
                ++sectionLeak;
            const bool high = (n.startBeat >= 224.0 && n.startBeat < 352.0)
                           || n.startBeat >= 384.0;
            if (high) { ++highOct; if (n.pitch < 53) ++badReg; }
            else      { ++lowOct;  if (n.pitch >= 53) ++badReg; }
        }
        EXPECT_EQ(nonOffbeat, 0);
        EXPECT_EQ(sectionLeak, 0);
        EXPECT_EQ(lowOct, 128);
        EXPECT_EQ(highOct, 256);
        EXPECT_EQ(badReg, 0) << "bass register must lift only in mainB/finale";
    }

    // ARP: 16th grid, +12 glint exclusively on the last 16th of each beat.
    {
        const auto* notes = notesOf(s, "arp");
        ASSERT_EQ(static_cast<int>(notes->size()), 1536 + 4); // 16ths × 384 beats + breakdown melody
        int badGrid = 0, glintOffsets = 0, glintNonLast = 0;
        for (const auto& n : *notes)
        {
            if (n.durationBeats >= 3.0) continue; // breakdown melody, not arp 16ths
            const double frac = n.startBeat - std::floor(n.startBeat);
            if (std::abs(frac) > 1e-6 && std::abs(frac - 0.25) > 1e-6
                && std::abs(frac - 0.5) > 1e-6 && std::abs(frac - 0.75) > 1e-6)
                ++badGrid;
            if (std::abs(frac - 0.75) < 1e-6)
            {
                ++glintOffsets; // last 16th of the beat — must carry the +12
                if (n.pitch < 65) ++glintNonLast; // arp base 53 + glint 12 = 65
            }
        }
        EXPECT_EQ(badGrid, 0);
        // One note per beat sits at offset .75 (the glinted last 16th):
        // 3 full-stack sections × 128 beats = 384, every one ≥ 65.
        EXPECT_EQ(glintOffsets, 384);
        EXPECT_EQ(glintNonLast, 0);
    }

    // BREAKDOWN MELODY inside the arp clip: 4 slow notes in [352,384).
    {
        const auto* notes = notesOf(s, "arp");
        int melody = 0;
        double firstMelody = -1.0, prev = -10.0;
        for (const auto& n : *notes)
        {
            if (n.startBeat >= 352.0 && n.startBeat < 384.0)
            {
                EXPECT_GE(n.durationBeats, 3.0);
                EXPECT_GE(n.startBeat - prev, 7.9);
                prev = n.startBeat;
                if (firstMelody < 0) firstMelody = n.startBeat;
                ++melody;
            }
        }
        EXPECT_EQ(melody, 4);
        EXPECT_EQ(firstMelody, 352.0);
    }

    // STABS: triad (3 simultaneous notes) on beat 2 (b%4==1) of full-stack
    // bars; extra triad on beat 4 in mainB/finale at density 0.7.
    {
        const auto* notes = notesOf(s, "stab");
        ASSERT_EQ(static_cast<int>(notes->size()), 480); // 32 bars × (3 + 3) × 2 + 32×3
        for (const auto& n : *notes)
            EXPECT_TRUE(inFMinorPc(n.pitch));
    }

    // PADS: whole arrangement, two tones per bar → 2 × 128 bars.
    {
        const auto* notes = notesOf(s, "pad");
        ASSERT_EQ(static_cast<int>(notes->size()), 256);
        EXPECT_EQ(notes->front().startBeat, 0.0);
        EXPECT_GE(notes->back().startBeat, 380.0);
    }

    // RISERS: rising-velocity 8ths only in the 8 beats before each drop
    // (mainA 64, mainB 224, finale 384) → 3 banks × 8.
    {
        const auto* notes = notesOf(s, "riser");
        ASSERT_EQ(static_cast<int>(notes->size()), 24);
        for (const auto& n : *notes)
        {
            const double st = n.startBeat;
            const bool inBank = (st >= 56.0 && st < 64.0)
                             || (st >= 216.0 && st < 224.0)
                             || (st >= 376.0 && st < 384.0);
            EXPECT_TRUE(inBank) << "riser note outside the pre-drop windows: " << st;
        }
    }

    // DOWNS: one long tonal-reverse per drop, spanning the 8 beats into it.
    {
        const auto* notes = notesOf(s, "down");
        ASSERT_EQ(static_cast<int>(notes->size()), 3);
        EXPECT_DOUBLE_EQ(notes->at(0).startBeat, 56.0);
        EXPECT_DOUBLE_EQ(notes->at(1).startBeat, 216.0);
        EXPECT_DOUBLE_EQ(notes->at(2).startBeat, 376.0);
        for (const auto& n : *notes)
        {
            EXPECT_DOUBLE_EQ(n.durationBeats, 8.0);
            EXPECT_EQ(n.pitch, 41);
        }
    }

    // CLAPS on 2/4 from the build on: beats with b%4 ∈ {1,3}.
    {
        const auto* notes = notesOf(s, "clap");
        ASSERT_FALSE(notes->empty());
        for (const auto& n : *notes)
        {
            const int m = static_cast<int>(std::floor(n.startBeat)) % 4;
            EXPECT_TRUE(m == 1 || m == 3) << "clap outside 2/4: " << n.startBeat;
            EXPECT_GE(n.startBeat, 32.0);
        }
    }

    // Skipped: nothing mapped out → everything present.
    EXPECT_TRUE(s.skipped.empty());
    EXPECT_EQ(s.notesTotal, 416 + 384 + countRole(s, "hat") + 1540 + 480 + 256
                              + 24 + 3 + countRole(s, "clap"));
}

// ── G1.2: determinism + seed ───────────────────────────────────────────────
TEST(PsytranceGenerator, DeterminismAndSeed)
{
    auto p1 = canonParams();
    mapAllRoles(p1, 0);
    auto s1 = HDAW::PsytranceGenerator::generate(p1);
    auto s2 = HDAW::PsytranceGenerator::generate(p1);
    ASSERT_EQ(s1.clips.size(), s2.clips.size());

    auto serialize = [](const HDAW::PsytranceScore& sc) {
        std::string out;
        for (const auto& c : sc.clips)
        {
            out += c.role + ":";
            for (const auto& n : c.notes)
                out += std::to_string(n.pitch) + "," + std::to_string(n.startBeat) + ";";
        }
        return out;
    };
    EXPECT_EQ(serialize(s1), serialize(s2)); // same seed → identical

    // Different seed: ONLY the density-gated hat rolls change; everything
    // else stays pitch-identical (kick/bass/arp/pad/stab/riser/down are
    // structural, seed-free).
    auto p2 = canonParams();
    mapAllRoles(p2, 0);
    p2.seed = 43;
    const auto s3 = HDAW::PsytranceGenerator::generate(p2);

    const auto* h1 = notesOf(s1, "hat");
    const auto* h3 = notesOf(s3, "hat");
    ASSERT_NE(h1, nullptr);
    ASSERT_NE(h3, nullptr);
    bool hatsDiffer = false;
    if (h1->size() != h3->size()) hatsDiffer = true;
    for (size_t i = 0; i < std::min(h1->size(), h3->size()) && !hatsDiffer; ++i)
        if (h1->at(i).startBeat != h3->at(i).startBeat || h1->at(i).pitch != h3->at(i).pitch)
            hatsDiffer = true;
    EXPECT_TRUE(hatsDiffer) << "density-gated rolls must respond to the seed";

    for (const char* role : { "kick", "bass", "arp", "stab", "pad", "riser", "down" })
    {
        const auto* a = notesOf(s1, role);
        const auto* b = notesOf(s3, role);
        ASSERT_NE(a, nullptr);
        ASSERT_NE(b, nullptr);
        ASSERT_EQ(a->size(), b->size()) << role;
        for (size_t i = 0; i < a->size(); ++i)
        {
            EXPECT_EQ(a->at(i).pitch, b->at(i).pitch) << role;
            EXPECT_EQ(a->at(i).startBeat, b->at(i).startBeat) << role;
            EXPECT_EQ(a->at(i).velocity, b->at(i).velocity) << role;
            EXPECT_EQ(a->at(i).durationBeats, b->at(i).durationBeats) << role;
        }
    }
}

// ── G1.3: AudioEngineCommands round-trip + one undo unit ───────────────────
TEST(PsytranceGeneratorCommand, RoundTripClipsOnRightTracksOneUndo)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.setTempo(140.0);
    engine.drainPendingRoutingRebuild();

    auto p = canonParams();
    mapAllRoles(p, 0);
    // Build the 9-track palette (samples NOT required — notes/clips only).
    for (int i = 0; i < 9; ++i)
        ASSERT_GE(cmds.addTrack("Psy" + std::to_string(i), -1, -1, 0), 0);
    engine.drainPendingRoutingRebuild();

    auto r = cmds.generatePsytrance(p);
    ASSERT_TRUE(r.error.empty()) << r.error;
    ASSERT_EQ(r.clips.size(), 9u);
    EXPECT_EQ(r.totalBeats, 512.0);
    EXPECT_GT(r.notesTotal, 0);
    EXPECT_EQ(r.notesSkipped, 0);
    EXPECT_TRUE(r.skippedRoles.empty());

    // Compare with the pure generator (same source of truth).
    const auto score = HDAW::PsytranceGenerator::generate(p);
    ASSERT_EQ(score.clips.size(), r.clips.size());

    // Every clip sits on its role's track with the expected note count and
    // spans the full arrangement (beats → seconds: 512 beats @ 140 BPM).
    auto& m = engine.getProjectModel();
    for (const auto& rc : r.clips)
    {
        ASSERT_GE(rc.trackIndex, 0);
        auto track = m.getTrackListTree().getChild(rc.trackIndex);
        auto clipList = track.getChildWithName(IDs::CLIP_LIST);
        ASSERT_NE(clipList.getNumChildren(), 0) << rc.role << " clip missing";
        auto clip = clipList.getChild(0);
        auto notes = clip.getChildWithName(IDs::MIDI_NOTE_LIST);
        EXPECT_EQ(notes.getNumChildren(), rc.noteCount) << rc.role;
        // Clip spans the arrangement: duration seconds = beatsToSeconds(512).
        const double durSec = HDAW::beatsToSeconds(512.0, 140.0);
        EXPECT_NEAR(static_cast<double>(clip.getProperty(IDs::duration)),
                    durSec, 1e-6) << rc.role;
    }

    // ONE undo removes every generated clip/note (single transaction).
    cmds.undo();
    engine.drainPendingRoutingRebuild();
    for (int t = 0; t < 9; ++t)
    {
        auto track = m.getTrackListTree().getChild(t);
        auto clipList = track.getChildWithName(IDs::CLIP_LIST);
        EXPECT_EQ(clipList.getNumChildren(), 0) << "track " << t
            << " still has clips after one undo";
    }

    // Redo restores them (undoable as a unit).
    cmds.redo();
    engine.drainPendingRoutingRebuild();
    int total = 0;
    for (int t = 0; t < 9; ++t)
    {
        auto track = m.getTrackListTree().getChild(t);
        auto clipList = track.getChildWithName(IDs::CLIP_LIST);
        total += clipList.getNumChildren();
    }
    EXPECT_EQ(total, 9);
}

// ── G1.4-adjacent validation: bad role track id is a tool-named error and
//    nothing is written (partial-write guard). ───────────────────────────────
TEST(PsytranceGeneratorCommand, BadTrackRejectedNoPartialWrites)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    engine.drainPendingRoutingRebuild();

    auto p = canonParams();
    mapAllRoles(p, 0);
    p.bass = 999; // out of range
    for (int i = 0; i < 9; ++i)
        cmds.addTrack("T" + std::to_string(i), -1, -1, 0);
    engine.drainPendingRoutingRebuild();

    auto r = cmds.generatePsytrance(p);
    EXPECT_FALSE(r.error.empty());
    EXPECT_TRUE(r.error.find("out of range") != std::string::npos) << r.error;
    EXPECT_TRUE(r.clips.empty());

    // Nothing written anywhere (no partial state).
    auto& m = engine.getProjectModel();
    for (int t = 0; t < 9; ++t)
    {
        auto track = m.getTrackListTree().getChild(t);
        auto clipList = track.getChildWithName(IDs::CLIP_LIST);
        EXPECT_EQ(clipList.getNumChildren(), 0);
    }
}

// ── G1.5: RPC router twin round-trips the same JSON the frontend sends ─────
TEST(PsytranceGeneratorRouter, DispatchCompositionRoundTrip)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.setTempo(140.0);
    engine.drainPendingRoutingRebuild();
    for (int i = 0; i < 9; ++i)
        cmds.addTrack("R" + std::to_string(i), -1, -1, 0);
    engine.drainPendingRoutingRebuild();

    QJsonObject paramsobj;
    paramsobj["paletteTrackIds"] = QJsonObject{
        { "kick", 0 }, { "bass", 1 }, { "hat", 2 }, { "arp", 3 },
        { "stab", 4 }, { "pad", 5 }, { "riser", 6 }, { "down", 7 }, { "clap", 8 } };
    QJsonArray sections;
    sections.append(QJsonObject{ { "name", "intro" },     { "start", 0.0 },   { "end", 32.0 } });
    sections.append(QJsonObject{ { "name", "build" },     { "start", 32.0 },  { "end", 64.0 } });
    sections.append(QJsonObject{ { "name", "mainA" },     { "start", 64.0 },  { "end", 192.0 } });
    sections.append(QJsonObject{ { "name", "mini" },      { "start", 192.0 }, { "end", 224.0 } });
    sections.append(QJsonObject{ { "name", "mainB" },     { "start", 224.0 }, { "end", 352.0 } });
    sections.append(QJsonObject{ { "name", "breakdown" }, { "start", 352.0 }, { "end", 384.0 } });
    sections.append(QJsonObject{ { "name", "finale" },    { "start", 384.0 }, { "end", 512.0 } });
    paramsobj["sections"] = sections;
    paramsobj["keyRoot"] = 5;
    paramsobj["scaleMode"] = 1;
    paramsobj["density"] = 0.7;
    paramsobj["seed"] = 42;

    auto res = frontend::dispatchComposition(engine, "generatePsytrance", QJsonValue(paramsobj));
    ASSERT_FALSE(res.isError);
    ASSERT_TRUE(res.payload.isObject());
    auto payload = res.payload.toObject();
    EXPECT_EQ(payload.value("totalBeats").toDouble(), 512.0);
    EXPECT_GT(payload.value("notesTotal").toInt(), 0);
    auto clips = payload.value("clips").toArray();
    ASSERT_EQ(clips.size(), 9);
    // Roles land on the tracks the frontend mapped.
    std::map<int, int> byTrack;
    for (const auto& cv : clips)
    {
        auto c = cv.toObject();
        byTrack[c.value("trackId").toInt()] = c.value("clipId").toInt(-1);
        EXPECT_GE(c.value("noteCount").toInt(), 0);
    }
    for (int t = 0; t < 9; ++t)
        EXPECT_NE(byTrack[t], -1) << "no clip on palette track " << t;
    EXPECT_EQ(payload.value("notesSkipped").toInt(), 0);
    EXPECT_TRUE(payload.value("skipped").toArray().isEmpty());

    // Unknown composition method still errors (dispatch boundary intact).
    auto bad = frontend::dispatchComposition(engine, "generatePsytranceNoSuch", QJsonValue(paramsobj));
    EXPECT_TRUE(bad.isError);
}