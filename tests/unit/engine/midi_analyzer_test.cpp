#include <gtest/gtest.h>
#include "engine/MidiAnalyzer.h"
#include "engine/PhraseGenerator.h"
#include "engine/RhythmPatternGenerator.h"
#include <juce_core/juce_core.h>
#include <fstream>
#include <windows.h>

namespace
{

void writeVarLen(std::vector<uint8_t>& buf, uint32_t value)
{
    if (value < 0x80) {
        buf.push_back(static_cast<uint8_t>(value));
        return;
    }
    std::vector<uint8_t> bytes;
    bytes.push_back(static_cast<uint8_t>(value & 0x7F));
    value >>= 7;
    while (value > 0) {
        bytes.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    for (auto it = bytes.rbegin(); it != bytes.rend(); ++it)
        buf.push_back(*it);
}

std::string writeTestMidiFile(const std::vector<std::tuple<int, int, double, double>>& notes,
                               double bpm = 120.0)
{
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    char path[MAX_PATH];
    GetTempFileNameA(tempPath, "mid", 0, path);

    const int ticksPerQuarter = 480;

    // Build note-on/off events with proper delta times
    struct MidiEvent { uint32_t tick; bool isNoteOn; int pitch; int velocity; };
    std::vector<MidiEvent> events;
    for (const auto& [pitch, vel, start, dur] : notes) {
        uint32_t onTick = static_cast<uint32_t>(start * ticksPerQuarter);
        uint32_t offTick = static_cast<uint32_t>((start + dur) * ticksPerQuarter);
        events.push_back({ onTick, true, pitch, vel });
        events.push_back({ offTick, false, pitch, 0 });
    }
    std::sort(events.begin(), events.end(),
        [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });

    std::vector<uint8_t> data;

    const uint8_t header[] = {
        'M','T','h','d', 0,0,0,6, 0,0, 0,1,
        static_cast<uint8_t>((ticksPerQuarter >> 8) & 0xFF),
        static_cast<uint8_t>(ticksPerQuarter & 0xFF)
    };
    data.insert(data.end(), header, header + sizeof(header));

    std::vector<uint8_t> trackData;

    // Tempo meta event
    uint32_t usPerQuarter = static_cast<uint32_t>(60000000.0 / bpm);
    writeVarLen(trackData, 0);
    trackData.insert(trackData.end(), { 0xFF, 0x51, 0x03 });
    trackData.push_back((usPerQuarter >> 16) & 0xFF);
    trackData.push_back((usPerQuarter >> 8) & 0xFF);
    trackData.push_back(usPerQuarter & 0xFF);

    // Note events with proper delta times
    uint32_t lastTick = 0;
    for (const auto& ev : events) {
        uint32_t delta = ev.tick - lastTick;
        lastTick = ev.tick;
        writeVarLen(trackData, delta);
        if (ev.isNoteOn) {
            trackData.push_back(0x90);
            trackData.push_back(static_cast<uint8_t>(ev.pitch));
            trackData.push_back(static_cast<uint8_t>(ev.velocity));
        } else {
            trackData.push_back(0x80);
            trackData.push_back(static_cast<uint8_t>(ev.pitch));
            trackData.push_back(0);
        }
    }

    // End of track
    writeVarLen(trackData, 0);
    trackData.insert(trackData.end(), { 0xFF, 0x2F, 0 });

    data.insert(data.end(), { 'M','T','r','k' });
    uint32_t trackLen = static_cast<uint32_t>(trackData.size());
    data.push_back((trackLen >> 24) & 0xFF);
    data.push_back((trackLen >> 16) & 0xFF);
    data.push_back((trackLen >> 8) & 0xFF);
    data.push_back(trackLen & 0xFF);
    data.insert(data.end(), trackData.begin(), trackData.end());

    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()), data.size());
    f.close();
    return std::string(path);
}

TEST(MidiAnalyzerTest, AnalyzeNonexistentFileReturnsEmpty)
{
    auto result = HDAW::MidiAnalyzer::analyze(juce::File("C:/nonexistent/path.mid"));
    EXPECT_TRUE(result.fileName.isEmpty());
    EXPECT_EQ(result.trackCount, 0);
}

TEST(MidiAnalyzerTest, AnalyzeSimpleScaleReturnsFingerprint)
{
    std::vector<std::tuple<int, int, double, double>> notes = {
        {60, 100, 0.0, 0.5},
        {62, 100, 0.5, 0.5},
        {64, 100, 1.0, 0.5},
        {65, 100, 1.5, 0.5},
        {67, 100, 2.0, 0.5},
        {69, 100, 2.5, 0.5},
        {71, 100, 3.0, 0.5},
        {72, 100, 3.5, 0.5},
    };

    auto path = writeTestMidiFile(notes);
    auto file = juce::File(path);
    auto result = HDAW::MidiAnalyzer::analyze(file);

    EXPECT_EQ(result.trackCount, 1);
    EXPECT_GT(result.fingerprint.avgNoteDensity, 0.0);
    EXPECT_GT(result.fingerprint.pitchRange, 0);
    EXPECT_GE(result.fingerprint.scaleType, 0);
    EXPECT_NEAR(result.fingerprint.avgVelocity, 100.0 / 127.0, 0.05);

    file.deleteFile();
}

TEST(MidiAnalyzerTest, AnalyzeChordReturnsHighPolyphony)
{
    std::vector<std::tuple<int, int, double, double>> notes = {
        {60, 100, 0.0, 1.0},
        {64, 100, 0.0, 1.0},
        {67, 100, 0.0, 1.0},
        {60, 100, 1.0, 1.0},
        {64, 100, 1.0, 1.0},
        {67, 100, 1.0, 1.0},
    };

    auto path = writeTestMidiFile(notes);
    auto result = HDAW::MidiAnalyzer::analyze(juce::File(path));

    EXPECT_GE(result.fingerprint.avgPolyphony, 2.5);
    EXPECT_EQ(result.fingerprint.voiceCount, 1);

    juce::File(path).deleteFile();
}

TEST(MidiAnalyzerTest, RepeatedBarsAppearAsPatterns)
{
    std::vector<std::tuple<int, int, double, double>> notes;
    auto addBar = [&](int barStart, int pitch) {
        notes.push_back({pitch, 100, barStart + 0.0, 0.25});
        notes.push_back({pitch + 4, 100, barStart + 0.5, 0.25});
        notes.push_back({pitch + 7, 100, barStart + 1.0, 0.25});
        notes.push_back({pitch + 12, 80, barStart + 1.5, 0.5});
    };

    addBar(0.0, 60);
    addBar(4.0, 62);
    addBar(8.0, 60);
    addBar(12.0, 65);

    auto path = writeTestMidiFile(notes);
    auto result = HDAW::MidiAnalyzer::analyze(juce::File(path));

    bool foundRepeated = false;
    for (const auto& p : result.patterns) {
        if (!p.isMotif && p.frequency >= 2.0) {
            foundRepeated = true;
            break;
        }
    }
    EXPECT_TRUE(foundRepeated);

    juce::File(path).deleteFile();
}

TEST(MidiAnalyzerTest, StyleClassificationForHighDensity)
{
    HDAW::MidiFingerprint fp;
    fp.avgNoteDensity = 12.0;
    fp.pitchRange = 5;
    fp.avgNoteDuration = 0.1;
    fp.rhythmComplexity = 0.7;

    int style = HDAW::MidiAnalyzer::classifyStyle(fp);
    EXPECT_EQ(style, PhraseGenerator::TrapHiHat);
}

TEST(MidiAnalyzerTest, StyleClassificationForPad)
{
    HDAW::MidiFingerprint fp;
    fp.avgNoteDensity = 1.5;
    fp.pitchRange = 36;
    fp.avgNoteDuration = 4.0;

    int style = HDAW::MidiAnalyzer::classifyStyle(fp);
    EXPECT_EQ(style, PhraseGenerator::Pad);
}

TEST(MidiAnalyzerTest, StyleClassificationForWalkingBass)
{
    HDAW::MidiFingerprint fp;
    fp.avgNoteDensity = 4.0;
    fp.pitchRange = 18;
    fp.swingAmount = 0.5;
    fp.rhythmComplexity = 0.2;
    fp.avgPolyphony = 1.0;

    int style = HDAW::MidiAnalyzer::classifyStyle(fp);
    EXPECT_EQ(style, PhraseGenerator::WalkingBass);
}

TEST(MidiAnalyzerTest, ToPatternJsonReturnsNonEmpty)
{
    std::vector<std::tuple<int, int, double, double>> notes = {
        {60, 100, 0.0, 0.5},
        {64, 100, 0.5, 0.5},
        {67, 100, 1.0, 0.5},
        {72, 100, 1.5, 0.5},
    };
    auto path = writeTestMidiFile(notes);
    auto result = HDAW::MidiAnalyzer::analyze(juce::File(path));

    auto [params, styleParams] = HDAW::MidiAnalyzer::toPatternJson(result);
    EXPECT_FALSE(params.isEmpty());
    EXPECT_FALSE(styleParams.isEmpty());

    juce::File(path).deleteFile();
}

TEST(MidiAnalyzerTest, AnalyzeRealWorldMidiFiles)
{
    juce::File dir("E:\\midi\\@MIDILATINO_THE_CORILLO_MIDI_PACK");
    ASSERT_TRUE(dir.isDirectory());

    juce::Array<juce::File> midiFiles;
    juce::DirectoryIterator iter(dir, false, "*.mid");
    while (iter.next())
        midiFiles.add(iter.getFile());

    int count = 0;
    for (auto& entry : midiFiles)
    {
        if (count >= 10) break;

        auto result = HDAW::MidiAnalyzer::analyze(entry);
        if (result.trackCount == 0) continue;

        auto& fp = result.fingerprint;
        auto styleName = PhraseGenerator::styleName(
            static_cast<PhraseGenerator::Style>(result.guessedStyle));

        std::cout << "\n=== " << entry.getFileName().toStdString() << " ===\n";
        std::cout << "  BPM: " << result.sourceBpm
                  << "  Time: " << result.timeSignatureNum << "/" << result.timeSignatureDen
                  << "  Tracks: " << result.trackCount << "\n";
        std::cout << "  Notes/bar: " << fp.avgNoteDensity
                  << "  Duration: " << fp.avgNoteDuration << " beats"
                  << "  Pitch range: " << fp.pitchRange << " st\n";
        std::cout << "  Velocity: " << fp.avgVelocity
                  << "  Swing: " << fp.swingAmount
                  << "  Syncopation: " << fp.syncopationScore << "\n";
        std::cout << "  Quantization: " << fp.quantizationStrength
                  << "  Complexity: " << fp.rhythmComplexity
                  << "  Chromaticism: " << fp.chromaticism << "\n";
        std::cout << "  Root: " << fp.rootNote
                  << "  Scale: " << fp.scaleType
                  << "  Polyphony: " << fp.avgPolyphony << "\n";
        std::cout << "  Style: " << styleName
                  << "  Confidence: " << result.styleConfidence << "\n";
        std::cout << "  Patterns: " << result.patterns.size() << "\n";
        std::cout << "  Params: " << result.paramsJson.substring(0, 200).toStdString() << "\n";

        count++;
    }
    EXPECT_GT(count, 0);
}

TEST(MidiAnalyzerTest, CreateRemixFromAnalyzedMidi)
{
    // ── Step 1: Pick a random MIDI file ──
    juce::File dir("E:\\midi\\@MIDILATINO_THE_CORILLO_MIDI_PACK");
    ASSERT_TRUE(dir.isDirectory());

    juce::Array<juce::File> midiFiles;
    juce::DirectoryIterator iter(dir, false, "*.mid");
    while (iter.next())
        midiFiles.add(iter.getFile());
    ASSERT_GT(midiFiles.size(), 0);

    // Use a deterministic index for reproducibility (change to random in production)
    int fileIdx = 4; // Belleza_Gm — good candidate (3 tracks, G minor)
    auto midiFile = midiFiles[fileIdx];

    std::cout << "\n╔══════════════════════════════════════════╗\n";
    std::cout << "║       REMIX PIPELINE — ANALYSIS          ║\n";
    std::cout << "╚══════════════════════════════════════════╝\n";
    std::cout << "Source: " << midiFile.getFileName().toStdString() << "\n\n";

    // ── Step 2: Analyze ──
    auto analysis = HDAW::MidiAnalyzer::analyze(midiFile);
    ASSERT_GT(analysis.trackCount, 0);

    auto& fp = analysis.fingerprint;
    std::cout << "── Analysis Results ──\n";
    std::cout << "  BPM: " << analysis.sourceBpm << "\n";
    std::cout << "  Key: " << PhraseGenerator::noteName(fp.rootNote)
              << " " << PhraseGenerator::modeName(fp.scaleType) << "\n";
    std::cout << "  Style: " << PhraseGenerator::styleName(
        static_cast<PhraseGenerator::Style>(analysis.guessedStyle)) << "\n";
    std::cout << "  Density: " << fp.avgNoteDensity << " notes/bar\n";
    std::cout << "  Velocity: " << fp.avgVelocity << " (avg)\n";
    std::cout << "  Syncopation: " << fp.syncopationScore << "\n";
    std::cout << "  Patterns found: " << analysis.patterns.size() << "\n\n";

    // ── Step 3: Build remix parameters from analysis ──
    int rootNote = fp.rootNote;
    int scaleMode = fp.scaleType >= 0 ? fp.scaleType : 1; // default Minor
    double bpm = analysis.sourceBpm;

    // Choose style based on analysis — pick something complementary
    PhraseGenerator::Style remixStyle;
    if (fp.avgNoteDensity > 4.0)
        remixStyle = PhraseGenerator::Arpeggio;
    else if (fp.avgPolyphony > 2.5)
        remixStyle = PhraseGenerator::ChordStab;
    else
        remixStyle = PhraseGenerator::Lead;

    std::cout << "── Remix Strategy ──\n";
    std::cout << "  Generating: " << PhraseGenerator::styleName(remixStyle) << "\n";
    std::cout << "  Key center: " << PhraseGenerator::noteName(rootNote) << "\n";
    std::cout << "  Scale: " << PhraseGenerator::modeName(scaleMode) << "\n\n";

    // ── Step 4: Generate phrases using PhraseGenerator ──
    PhraseGenerator::PhraseParams phraseParams;
    phraseParams.scaleRoot = rootNote;
    phraseParams.scaleMode = scaleMode;
    phraseParams.style = remixStyle;
    phraseParams.lengthBeats = 8.0; // 2 bars
    phraseParams.density = static_cast<int>(fp.avgNoteDensity * 2.5); // busier for chord stabs
    phraseParams.noteDuration = fp.avgNoteDuration * 0.8; // slightly shorter
    phraseParams.lowNote = rootNote + 12; // octave above root
    phraseParams.highNote = rootNote + 36; // 3 octaves
    phraseParams.minVelocity = static_cast<int>(fp.avgVelocity * 127.0 * 0.6);
    phraseParams.maxVelocity = static_cast<int>(fp.avgVelocity * 127.0 * 1.2);
    phraseParams.seed = 42; // deterministic for reproducibility

    auto phrase = PhraseGenerator::generatePhrase(phraseParams);
    std::cout << "── Generated Phrase ──\n";
    std::cout << "  Notes: " << phrase.size() << "\n";
    for (size_t i = 0; i < phrase.size() && i < 8; ++i) {
        std::cout << "  [" << phrase[i].startBeat << "] "
                  << PhraseGenerator::noteName(phrase[i].noteNumber)
                  << " vel=" << phrase[i].velocity
                  << " dur=" << phrase[i].durationBeats << "\n";
    }
    if (phrase.size() > 8)
        std::cout << "  ... (" << (phrase.size() - 8) << " more notes)\n";
    std::cout << "\n";

    // ── Step 5: Generate bass line ──
    PhraseGenerator::PhraseParams bassParams;
    bassParams.scaleRoot = rootNote;
    bassParams.scaleMode = scaleMode;
    bassParams.style = PhraseGenerator::BassLine;
    bassParams.lengthBeats = 8.0;
    bassParams.density = 4;
    bassParams.noteDuration = 0.5;
    bassParams.lowNote = rootNote; // root octave
    bassParams.highNote = rootNote + 12;
    bassParams.minVelocity = 90;
    bassParams.maxVelocity = 120;
    bassParams.seed = 42;

    auto bass = PhraseGenerator::generatePhrase(bassParams);
    std::cout << "── Generated Bass Line ──\n";
    std::cout << "  Notes: " << bass.size() << "\n";
    for (size_t i = 0; i < bass.size() && i < 8; ++i) {
        std::cout << "  [" << bass[i].startBeat << "] "
                  << PhraseGenerator::noteName(bass[i].noteNumber)
                  << " vel=" << bass[i].velocity << "\n";
    }
    std::cout << "\n";

    // ── Step 6: Generate drum pattern ──
    RhythmPatternGenerator::Params drumParams;
    drumParams.grid = 16;
    drumParams.bars = 2;
    // Use explicit voices for reliability (avoids DSL priority conflicts)
    RhythmPatternGenerator::Voice kick, hat, clap;
    kick.hits = 4;   kick.rotation = 0; kick.pitch = 36; kick.velocity = 112; kick.duration = 0.25;
    hat.hits = 8;    hat.rotation = 0;  hat.pitch = 42;  hat.velocity = 88;   hat.duration = 0.1;
    clap.hits = 2;   clap.rotation = 2; clap.pitch = 39; clap.velocity = 100; clap.duration = 0.15;
    drumParams.voices = { kick, hat, clap };

    auto drums = RhythmPatternGenerator::generate(drumParams);
    std::cout << "── Generated Drums ──\n";
    std::cout << "  Events: " << drums.size() << "\n";
    for (size_t i = 0; i < drums.size() && i < 12; ++i) {
        const char* name = "??";
        if (drums[i].pitch == 36) name = "KICK";
        else if (drums[i].pitch == 42) name = "HAT";
        else if (drums[i].pitch == 39) name = "CLAP";
        std::cout << "  [" << drums[i].startBeat << "] " << name
                  << " vel=" << drums[i].velocity << "\n";
    }
    if (drums.size() > 12)
        std::cout << "  ... (" << (drums.size() - 12) << " more hits)\n";
    std::cout << "\n";

    // ── Step 7: Assemble remix track plan ──
    std::cout << "╔══════════════════════════════════════════╗\n";
    std::cout << "║       REMIX TRACK PLAN                   ║\n";
    std::cout << "╚══════════════════════════════════════════╝\n";
    std::cout << "\n";
    std::cout << "Track 1: MELODY (PhraseGenerator " << PhraseGenerator::styleName(remixStyle) << ")\n";
    std::cout << "  Notes: " << phrase.size() << " over 2 bars\n";
    std::cout << "  Suggested patch: FM bell/pluck from Dexed 80's library\n";
    std::cout << "  Or: Lekebusch \"Synthes\" stab sample\n";
    std::cout << "  Or: VST preset (Serum/Massive pluck)\n";
    std::cout << "\n";
    std::cout << "Track 2: BASS (BassLine style)\n";
    std::cout << "  Notes: " << bass.size() << " over 2 bars\n";
    std::cout << "  Suggested patch: FM bass from Dexed (bassics.syx)\n";
    std::cout << "  Or: Lekebusch \"Bassline\" WAV (TB-303/Juno style)\n";
    std::cout << "  Or: VST bass preset\n";
    std::cout << "\n";
    std::cout << "Track 3: DRUMS (Euclidean + Tresillo)\n";
    std::cout << "  Events: " << drums.size() << " over 2 bars\n";
    std::cout << "  Kick: Lekebusch \"Bassdrum\" WAV (909/808)\n";
    std::cout << "  Hat:  Lekebusch \"Hihats\" WAV\n";
    std::cout << "  Clap: Lekebusch \"Handclap\" WAV (808 clap)\n";
    std::cout << "\n";
    std::cout << "Track 4: CHORDS (from analyzed patterns)\n";
    std::cout << "  Patterns extracted: " << analysis.patterns.size() << "\n";
    std::cout << "  Suggested: use analyzed chord voicings, regenerate with Pad style\n";
    std::cout << "  Patch: FM strings/pad from Dexed (strings.syx)\n";
    std::cout << "\n";

    // ── Step 8: List available sound resources ──
    std::cout << "╔══════════════════════════════════════════╗\n";
    std::cout << "║       SOUND RESOURCES                    ║\n";
    std::cout << "╚══════════════════════════════════════════╝\n";
    std::cout << "\n";
    std::cout << "FM Patches (Dexed 80's Library, 302 .syx files):\n";
    std::cout << "  bassics.syx     — bass\n";
    std::cout << "  brass.syx       — brass stab\n";
    std::cout << "  bells.syx       — bell/pluck\n";
    std::cout << "  strings.syx     — pad/strings\n";
    std::cout << "  piano.syx       — piano\n";
    std::cout << "  rhodes1.syx     — electric piano\n";
    std::cout << "  organ1.syx      — organ\n";
    std::cout << "  effect-1.syx    — FX\n";
    std::cout << "\n";
    std::cout << "Samples (Lekebusch SAE, ~1000 WAV files):\n";
    std::cout << "  E:\\samples\\_lekebusch\\Bassdrum\\   — 72 kicks\n";
    std::cout << "  E:\\samples\\_lekebusch\\Hihats\\     — 120 hats\n";
    std::cout << "  E:\\samples\\_lekebusch\\Snaredrum\\  — 64 snares\n";
    std::cout << "  E:\\samples\\_lekebusch\\Handclap\\   — 64 claps\n";
    std::cout << "  E:\\samples\\_lekebusch\\Bassline\\   — 92 synth bass\n";
    std::cout << "  E:\\samples\\_lekebusch\\Synthes\\    — 81 synth stabs\n";
    std::cout << "  E:\\samples\\_lekebusch\\Texture\\    — 81 textures\n";
    std::cout << "  E:\\samples\\_lekebusch\\Impact\\     — 81 FX/impacts\n";

    // Verify we generated something meaningful
    EXPECT_GT(phrase.size(), 0u);
    EXPECT_GT(bass.size(), 0u);
    EXPECT_GT(drums.size(), 0u);
    EXPECT_GT(analysis.patterns.size(), 0u);
}

} // namespace
