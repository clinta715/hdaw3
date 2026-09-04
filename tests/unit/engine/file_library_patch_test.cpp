// Patch-library tests (docs/superpowers/plans/2026-09-04-patch-libraries-
// sidecars.md): FileLibraryManager `type="patch"` scans, sidecar ingestion,
// search, serialize round-trip, coexistence with audio libraries, and the
// audition_patch MCP tool loading a Virus patch into a probe sub_synth slot
// (asserted on the LIVE processor — Gate 1/10 discipline).

#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "engine/FileLibraryManager.h"
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/Track.h"
#include "engine/TrackFXSlot.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

class FileLibraryPatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                      .getChildFile("hdaw_file_library_patch_test");
        tempDir.deleteRecursively();
        tempDir.createDirectory();
    }
    void TearDown() override {
        tempDir.deleteRecursively();
    }
    juce::File tempDir;

    // Resolve the 267-byte Virus B/C single fixture (tests/unit/engine/
    // testdata/virus/bcsingle.syx) independently of the runner's cwd.
    static juce::File virusFixture() {
        juce::File self(__FILE__);
        juce::File dir = juce::File::isAbsolutePath(__FILE__)
            ? self.getParentDirectory().getChildFile("testdata/virus")
            : juce::File::getCurrentWorkingDirectory().getChildFile(
                "tests/unit/engine/testdata/virus");
        return dir.getChildFile("bcsingle.syx");
    }

    juce::File makePatchDir() {
        auto dir = tempDir.getChildFile("patchlib");
        dir.createDirectory();
        auto dst = dir.getChildFile("bcsingle.syx");
        virusFixture().copyFileTo(dst);
        return dir;
    }

    static void writeSidecar(const juce::File& patchFile, const juce::String& content) {
        auto sc = juce::File(patchFile.getFullPathName() + ".virus.json");
        sc.replaceWithText(content);
    }

    static void waitForScan(HDAW::FileLibraryManager& mgr) {
        for (int i = 0; i < 50 && mgr.isScanning(); ++i)
            juce::Thread::sleep(100);
        ASSERT_FALSE(mgr.isScanning()) << "scan did not complete in time";
    }

    // Bare-result contract: handleRequestOnTestThread returns isError/content
    // at the top level (see the note in McpServer.cpp).
    static bool mcpIsError(const QJsonValue& r) {
        return r.toObject().value("isError").toBool();
    }
    static QString mcpText(const QJsonValue& r) {
        return r.toObject().value("content").toArray().at(0).toObject()
            .value("text").toString();
    }
};

TEST_F(FileLibraryPatchTest, AddPatchLibraryAndScan) {
    auto dir = makePatchDir();
    HDAW::FileLibraryManager mgr(tempDir);
    auto id = mgr.addLibrary("Patch Lib", dir.getFullPathName(), "patch");
    ASSERT_FALSE(id.isEmpty());

    auto info = mgr.getLibraryInfo(id);
    EXPECT_EQ(info.type, "patch");

    mgr.scanLibrary(id);
    waitForScan(mgr);

    auto results = mgr.search("", "patch");
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].name, "bcsingle.syx");
    EXPECT_EQ(results[0].format, "syx");
    EXPECT_GT(results[0].size, 0);
}

TEST_F(FileLibraryPatchTest, PatchSidecarIngestedIntoSearches) {
    auto dir = makePatchDir();
    auto patchFile = dir.getChildFile("bcsingle.syx");
    writeSidecar(patchFile,
        juce::String(R"({
          "schema": "hdaw.virus.patch.v1",
          "name": "~WELCOME", "engine": "sub_synth",
          "format": "bcsingle", "bank": 1, "program": 0,
          "roleCheck": {"role":"bass","verdict":"pass","passed_count":3,"total_count":3,
                        "summary":"3/3 checks passed"},
          "mappedParams": {"0":{"param":"osc1_wave","value":1.0,"raw":0},
                           "1":{"param":"osc1_level","value":0.5039,"raw":64}},
          "unmapped": ["osc2_fm_amount","ring_mod","lfo1","lfo2"],
          "description": "Dancefloor bass, welcomed."
        })"));

    HDAW::FileLibraryManager mgr(tempDir);
    auto id = mgr.addLibrary("Patch Lib", dir.getFullPathName(), "patch");
    mgr.scanLibrary(id);
    waitForScan(mgr);

    // Search by a sidecar-derived tag and by a description term.
    auto byTag = mgr.search("role:pass", "patch");
    ASSERT_EQ(byTag.size(), 1u);
    EXPECT_EQ(byTag[0].path, patchFile.getFullPathName());

    auto byDesc = mgr.search("dancefloor", "patch");
    ASSERT_EQ(byDesc.size(), 1u);

    const auto& e = byDesc[0];
    EXPECT_EQ(e.patchEngine, "sub_synth");
    EXPECT_EQ(e.roleVerdict, "pass");
    EXPECT_EQ(e.unmapped, "osc2_fm_amount,ring_mod,lfo1,lfo2");
    EXPECT_TRUE(e.patchParams.contains("osc1_wave")) << "patchParams carries the mapped params JSON";
    EXPECT_TRUE(e.patchParams.contains("osc1_level"));
    EXPECT_TRUE(e.tags.contains("role:pass"));
    EXPECT_TRUE(e.tags.contains("osc2_fm_amount"));
    EXPECT_TRUE(e.tags.contains("osc1_wave")) << "a mapped param name becomes a tag";
}

TEST_F(FileLibraryPatchTest, PatchEntrySerializeRoundTrip) {
    auto dir = makePatchDir();
    writeSidecar(dir.getChildFile("bcsingle.syx"),
        juce::String(R"({"schema":"hdaw.virus.patch.v1","name":"~WELCOME","engine":"sub_synth",
                        "roleCheck":{"role":"lead","verdict":"pass"},
                        "mappedParams":{"0":{"param":"osc1_wave","value":1.0,"raw":0}},
                        "unmapped":["ring_mod","lfo1"],
                        "description":"Round-trip survivor"})"));

    {
        HDAW::FileLibraryManager mgr(tempDir);
        auto id = mgr.addLibrary("Patch Lib", dir.getFullPathName(), "patch");
        mgr.scanLibrary(id);
        waitForScan(mgr);
        auto results = mgr.search("round-trip", "patch");
        ASSERT_EQ(results.size(), 1u);
        EXPECT_EQ(results[0].patchEngine, "sub_synth");
    }

    // A fresh manager lazy-loads the persisted cache — patch fields survive.
    HDAW::FileLibraryManager mgr2(tempDir);
    auto results = mgr2.search("round-trip", "patch");
    ASSERT_EQ(results.size(), 1u);
    const auto& e = results[0];
    EXPECT_EQ(e.patchEngine, "sub_synth");
    EXPECT_EQ(e.roleVerdict, "pass");
    EXPECT_EQ(e.unmapped, "ring_mod,lfo1");
    EXPECT_TRUE(e.patchParams.contains("osc1_wave"));
    EXPECT_TRUE(e.tags.contains("ring_mod"));
}

TEST_F(FileLibraryPatchTest, MalformedPatchSidecarTolerated) {
    auto dir = makePatchDir();
    writeSidecar(dir.getChildFile("bcsingle.syx"),
        juce::String("{ this is not valid json !!!"));
    HDAW::FileLibraryManager mgr(tempDir);
    auto id = mgr.addLibrary("Patch Lib", dir.getFullPathName(), "patch");
    mgr.scanLibrary(id);
    waitForScan(mgr);

    auto results = mgr.search("", "patch");
    ASSERT_EQ(results.size(), 1u);
    const auto& e = results[0];
    EXPECT_EQ(e.name, "bcsingle.syx");
    EXPECT_TRUE(e.patchEngine.isEmpty());
    EXPECT_TRUE(e.patchParams.isEmpty());
    EXPECT_TRUE(e.roleVerdict.isEmpty());
    EXPECT_TRUE(e.unmapped.isEmpty());
}

TEST_F(FileLibraryPatchTest, PatchLibraryDoesNotBreakAudioLibrary) {
    // Patch dir with sidecar.
    auto patchDir = makePatchDir();
    writeSidecar(patchDir.getChildFile("bcsingle.syx"),
        juce::String(R"({"schema":"hdaw.virus.patch.v1","engine":"sub_synth",
                        "roleCheck":{"role":"bass","verdict":"fail"},
                        "mappedParams":{"0":{"param":"osc1_wave","value":1.0,"raw":0}},
                        "unmapped":["lfo1"],"description":"Patch sidecar intact"})"));

    // Audio dir with a .timbre.json sidecar.
    auto audioDir = tempDir.getChildFile("audiolib");
    audioDir.createDirectory();
    auto wavFile = audioDir.getChildFile("test.wav");
    {
        auto outStream = wavFile.createOutputStream();
        ASSERT_NE(outStream, nullptr);
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(outStream.get(), 44100.0, 1, 16, {}, 0));
        ASSERT_NE(writer, nullptr);
        outStream.release();
        juce::AudioBuffer<float> buffer(1, 44100);
        buffer.clear();
        writer->writeFromAudioSampleBuffer(buffer, 0, 44100);
        writer.reset();
    }
    auto timbreSidecar = juce::File(wavFile.getFullPathName() + ".timbre.json");
    timbreSidecar.replaceWithText(
        juce::String(R"({"dsp_words":"warm analog pad","prose":"Creamy pad for ambient",
                        "captions":[["pad",0.9]],"tags":[["warm",0.9]]})"));

    HDAW::FileLibraryManager mgr(tempDir);
    auto patchId = mgr.addLibrary("Patch Lib", patchDir.getFullPathName(), "patch");
    auto audioId = mgr.addLibrary("Audio Lib", audioDir.getFullPathName(), "audio");
    mgr.scanLibrary(patchId);
    mgr.scanLibrary(audioId);
    waitForScan(mgr);

    // Audio sidecars still work.
    auto audioHits = mgr.search("cream", "audio");
    ASSERT_EQ(audioHits.size(), 1u);
    EXPECT_EQ(audioHits[0].format, "wav");
    EXPECT_TRUE(audioHits[0].description.contains("Creamy"));

    // Patch entry untouched by the audio scan.
    auto patchHits = mgr.search("intact", "patch");
    ASSERT_EQ(patchHits.size(), 1u);
    EXPECT_EQ(patchHits[0].patchEngine, "sub_synth");
    EXPECT_EQ(patchHits[0].roleVerdict, "fail");

    // Type filters are mutually exclusive.
    auto allAudio = mgr.search("", "audio");
    ASSERT_EQ(allAudio.size(), 1u);
    EXPECT_EQ(allAudio[0].format, "wav");
    auto allPatch = mgr.search("", "patch");
    ASSERT_EQ(allPatch.size(), 1u);
    EXPECT_EQ(allPatch[0].format, "syx");
}

TEST_F(FileLibraryPatchTest, AuditionPatchLoadsIntoProbeTrack) {
    auto patchFile = virusFixture();
    ASSERT_TRUE(patchFile.existsAsFile());

    AudioEngine engine;
    engine.initialize();
    mcp::McpServer s;
    s.setEngine(&engine);
    mcp::registerAllTools(s);

    auto r = s.handleRequestOnTestThread(1, "tools/call",
        QJsonObject{{"name", "audition_patch"},
                    {"arguments", QJsonObject{
                        {"path", QString::fromUtf8(patchFile.getFullPathName().toRawUTF8())},
                        {"engine", "sub_synth"},
                        {"role", "bass"}}}});
    ASSERT_FALSE(mcpIsError(r)) << "tool error: " << mcpText(r).toStdString();

    const auto result = QJsonDocument::fromJson(mcpText(r).toUtf8()).object();
    EXPECT_TRUE(result.value("ok").toBool());
    const int trackId = result.value("trackId").toInt();
    const int slotIndex = result.value("slotIndex").toInt();
    EXPECT_EQ(result.value("engine").toString().toStdString(), "sub_synth");
    EXPECT_EQ(result.value("role").toString().toStdString(), "bass");
    EXPECT_EQ(result.value("name").toString().toStdString(), "~WELCOME");

    // Probe track created (default project has 3 tracks).
    EXPECT_GE(trackId, 3);

    // Gate 6: LIVE processor slot reflects the loaded patch (bcsingle.syx maps
    // osc1_wave raw 0 -> Saw = sub wave 1, osc1_level raw 64 -> 64/127,
    // cutoff raw 27 -> 86.8611 Hz; pinned by VirusSysexImport tests).
    auto* proc = engine.getMainProcessor();
    ASSERT_NE(proc, nullptr);
    auto* track = proc->getTrack(trackId);
    ASSERT_NE(track, nullptr);
    auto& chain = track->getFXChain();
    ASSERT_GT(chain.size(), static_cast<size_t>(slotIndex));
    ASSERT_NE(chain[static_cast<size_t>(slotIndex)], nullptr);
    ASSERT_EQ(chain[static_cast<size_t>(slotIndex)]->getType(), "sub_synth");

    auto vals = chain[static_cast<size_t>(slotIndex)]->getInternalParamValues();
    ASSERT_GT(vals.size(), 8u);
    EXPECT_NEAR(vals[0], 1.0f, 1e-3f);      // Osc1 Wave -> Saw
    EXPECT_NEAR(vals[1], 0.503937f, 1e-3f); // Osc1 Level -> 64/127
    EXPECT_NEAR(vals[7], 86.8611f, 1e-2f);  // Cutoff -> 20*pow(1000, 27/127) Hz

    // The probe clip carrying the role phrase is on the probe track.
    auto tl = engine.getProjectModel().getTrackListTree();
    auto clipList = tl.getChild(trackId).getChildWithName(IDs::CLIP_LIST);
    ASSERT_TRUE(clipList.isValid());
    ASSERT_GE(clipList.getNumChildren(), 1);
    auto nl = clipList.getChild(0).getChildWithName(IDs::MIDI_NOTE_LIST);
    ASSERT_TRUE(nl.isValid());
    EXPECT_GE(nl.getNumChildren(), 1);
}

TEST_F(FileLibraryPatchTest, AuditionPatchSniffsEngineFromFile) {
    auto patchFile = virusFixture();
    AudioEngine engine;
    engine.initialize();
    mcp::McpServer s;
    s.setEngine(&engine);
    mcp::registerAllTools(s);

    // No engine arg: the Access header (F0 00 20 33) implies sub_synth.
    auto r = s.handleRequestOnTestThread(2, "tools/call",
        QJsonObject{{"name", "audition_patch"},
                    {"arguments", QJsonObject{
                        {"path", QString::fromUtf8(patchFile.getFullPathName().toRawUTF8())}}}});
    ASSERT_FALSE(mcpIsError(r)) << "tool error: " << mcpText(r).toStdString();
    const auto result = QJsonDocument::fromJson(mcpText(r).toUtf8()).object();
    EXPECT_TRUE(result.value("ok").toBool());
    EXPECT_EQ(result.value("engine").toString().toStdString(), "sub_synth");
}

// ── timbre clustering for patches (dsp sidecar ingest) ──────────────────────
namespace {

// Patch sidecar (virus schema) whose `dsp` dict carries ALL kDspFeatureCount
// keys in kDspFeatureKeys order. Every dim = base + 0.5*i, so two sidecars with
// near bases are near-identical in dsp space while a large base is far away.
juce::String patchSidecarWithDsp(const char* desc, double base) {
    juce::String json =
        "{\"schema\":\"hdaw.virus.patch.v1\",\"engine\":\"sub_synth\","
        "\"roleCheck\":{\"role\":\"bass\",\"verdict\":\"pass\"},"
        "\"description\":\"" + juce::String(desc) + "\",\"dsp\":{";
    for (int i = 0; i < HDAW::kDspFeatureCount; ++i) {
        if (i > 0) json += ",";
        json += "\"" + juce::String(HDAW::kDspFeatureKeys[i]) + "\":"
              + juce::String(base + 0.5 * (double)i, 6);
    }
    json += "}}";
    return json;
}

// Same shape but NO `dsp` key — the patch still has text/tags signal.
juce::String plainPatchSidecar(const char* desc) {
    return "{\"schema\":\"hdaw.virus.patch.v1\",\"engine\":\"sub_synth\","
           "\"roleCheck\":{\"role\":\"bass\",\"verdict\":\"pass\"},"
           "\"description\":\"" + juce::String(desc) + "\"}";
}

const HDAW::Cluster* clusterWithMember(const HDAW::ClusterOutcome& o,
                                       const juce::String& name) {
    for (const auto& c : o.clusters)
        for (const auto& m : c.members)
            if (m.name == name) return &c;
    return nullptr;
}

} // namespace

// dspFeatures ingested from the patch sidecar `dsp` dict — all 20 keys.
TEST_F(FileLibraryPatchTest, PatchSidecarDspIngested) {
    static_assert(HDAW::kDspFeatureCount == 20, "dsp contract is 20 keys");

    auto dir = makePatchDir();
    auto patchFile = dir.getChildFile("bcsingle.syx");
    writeSidecar(patchFile, patchSidecarWithDsp("Bass with dsp", 1.0));

    HDAW::FileLibraryManager mgr(tempDir);
    auto id = mgr.addLibrary("Patch Lib", dir.getFullPathName(), "patch");
    mgr.scanLibrary(id);
    waitForScan(mgr);

    auto e = mgr.getEntry(id, patchFile.getFullPathName());
    ASSERT_EQ(e.name, "bcsingle.syx");
    ASSERT_EQ(e.dspFeatures.size(), (size_t)HDAW::kDspFeatureCount)
        << "all 20 dsp keys present + finite must populate dspFeatures";
    // Spot-check dims in kDspFeatureKeys order: dim5 = centroid = 1.0 + 2.5.
    EXPECT_NEAR(e.dspFeatures[5], 3.5, 1e-6);
    EXPECT_NEAR(e.dspFeatures[12], 7.0, 1e-6); // mel_low
}

// clusterLibrary with method "dsp" over a patch library: near-identical dsp
// vectors cluster together, a far vector separates, and an entry without a dsp
// sidecar is excluded to unassigned.
TEST_F(FileLibraryPatchTest, PatchLibraryClusterableByDsp) {
    auto dir = tempDir.getChildFile("patch_cluster");
    dir.createDirectory();
    auto src = virusFixture();
    const juce::String names[] = {"patch_a.syx", "patch_b.syx",
                                  "patch_c.syx", "patch_d.syx"};
    for (const auto& n : names)
        src.copyFileTo(dir.getChildFile(n));

    writeSidecar(dir.getChildFile("patch_a.syx"), patchSidecarWithDsp("a", 1.0));
    writeSidecar(dir.getChildFile("patch_b.syx"), patchSidecarWithDsp("b", 1.05));
    writeSidecar(dir.getChildFile("patch_c.syx"), patchSidecarWithDsp("c", 50.0));
    writeSidecar(dir.getChildFile("patch_d.syx"), plainPatchSidecar("d"));

    HDAW::FileLibraryManager mgr(tempDir);
    auto id = mgr.addLibrary("Patch Lib", dir.getFullPathName(), "patch");
    mgr.scanLibrary(id);
    waitForScan(mgr);
    ASSERT_EQ(mgr.search("", "patch").size(), 4u);

    juce::String error;
    auto outcome = mgr.clusterLibrary(juce::StringArray{id}, 2, "dsp", error);
    ASSERT_TRUE(error.isEmpty()) << error.toStdString();
    ASSERT_EQ(outcome.method, "dsp");

    // The two near-identical vectors (a, b) must land in the same cluster.
    auto* clusterA = clusterWithMember(outcome, "patch_a.syx");
    auto* clusterB = clusterWithMember(outcome, "patch_b.syx");
    ASSERT_NE(clusterA, nullptr) << "patch_a must be clustered";
    ASSERT_NE(clusterB, nullptr) << "patch_b must be clustered";
    EXPECT_EQ(clusterA, clusterB) << "near-identical dsp vectors share a cluster";

    // The far vector separates, and the no-dsp entry is unassigned for dsp.
    EXPECT_NE(clusterWithMember(outcome, "patch_c.syx"), clusterA);
    bool inUnassigned = false;
    for (const auto& u : outcome.unassigned)
        if (u.name == "patch_d.syx") { inUnassigned = true; break; }
    EXPECT_TRUE(inUnassigned) << "entry without a dsp sidecar is excluded (unassigned) for method dsp";
    EXPECT_EQ(clusterWithMember(outcome, "patch_d.syx"), nullptr);
}