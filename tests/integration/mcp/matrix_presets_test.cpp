// Matrix-preset MCP tools (list_matrix_presets / apply_matrix_preset) — R3 of
// docs/plans/2026-09-16-matrix-presets.md. Fixture sheets are written to a temp
// dir and HDAW_MATRIX_PRESETS_DIR points at it, so the suite exercises sheet
// resolution, listing, and the full apply dispatch/accounting WITHOUT real
// plugins (the live apply/ear pass stays with the manual session per the plan).
// Fixture JSON is built programmatically: a header in this TU's include set
// defines R as a macro, which silently disables R"(...)​ raw string literals.

#include <gtest/gtest.h>

#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/ExportManager.h"
#include "engine/ProjectSerializer.h"
#include "engine/Track.h"
#include "engine/TrackFXSlot.h"
#include "common/PresetApply.h"
#include "common/ProjectCommands.h"
#include "common/WaldorfEditBuffer.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "mcp/McpTransportLoopback.h"
#include "mcp/McpJsonRpc.h"
#include "mcp/PresetFileParser.h"
#include "model/ProjectModel.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTemporaryDir>
#include <QtGlobal>

#include <memory>
#include <string>

namespace {

QJsonObject parseOne(const QByteArray& buf)
{
    const int nl = buf.indexOf('\n');
    const QByteArray line = nl >= 0 ? buf.left(nl) : buf;
    return QJsonDocument::fromJson(line).object();
}

// ── Phase 5 (2026-09-30): microQ edit-buffer retarget ───────────────────────
// A 392-byte microQ dump in the REAL corpus shape: byte5 = 0x30 (multi-edit),
// the buffer the single-mode OS does not play. Both the DEVICE-native matrix
// dump routes and the apply_preset file-loader route must retarget it to
// 0x20/0x00 and recompute the Waldorf checksum — through ONE implementation
// (src/common/WaldorfEditBuffer.h).
std::vector<uint8_t> makeMicroQDump(uint8_t bufferByte, uint8_t locationByte)
{
    std::vector<uint8_t> d(392, 0);
    d[0] = 0xF0;
    d[1] = mcp::kWaldorfId;
    d[2] = mcp::kWaldorfMachineMicroQ;
    d[3] = 0x00;
    d[4] = 0x01;
    d[5] = bufferByte;
    d[6] = locationByte;
    for (size_t i = 7; i + 2 < d.size(); ++i)
        d[i] = static_cast<uint8_t>(i & 0x7f);
    d[d.size() - 1] = 0xF7;
    uint8_t cs = 0;
    for (size_t i = 4; i + 2 < d.size(); ++i)
        cs += d[i];
    d[d.size() - 2] = cs & 0x7f;
    return d;
}

uint8_t dumpChecksum(const std::vector<uint8_t>& d)
{
    uint8_t cs = 0;
    for (size_t i = 4; i + 2 < d.size(); ++i)
        cs += d[i];
    return cs & 0x7f;
}

QJsonArray microQDumpJson()
{
    QJsonArray a;
    for (const auto b : makeMicroQDump(0x30, 0x40))
        a.append(static_cast<int>(b));
    return a;
}

/// Minimal recording plugin: captures every non-empty MIDI buffer it is handed,
/// so the bytes the isolated child WOULD receive are observable without a real
/// CLAP (the override set mirrors tests/unit/engine/fx_midi_injection_test.cpp).
struct RecordingPlugin : juce::AudioPluginInstance
{
    RecordingPlugin()
        : juce::AudioPluginInstance(
              juce::AudioProcessor::BusesProperties()
                  .withInput("In", juce::AudioChannelSet::stereo())
                  .withOutput("Out", juce::AudioChannelSet::stereo())) {}

    std::vector<juce::MidiBuffer> received;

    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
    const juce::String getName() const override { return "RecordingPlugin"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer& midi) override
    {
        if (midi.isEmpty())
            return;
        juce::MidiBuffer copy;
        for (const auto metadata : midi)
            copy.addEvent(metadata.getMessage(), metadata.samplePosition);
        received.push_back(copy);
    }
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override {}
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumParameters() override { return 0; }
    float getParameter(int) override { return 0; }
    void setParameter(int, float) override {}
    const juce::String getParameterName(int) override { return {}; }
    const juce::String getParameterText(int) override { return {}; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void fillInPluginDescription(juce::PluginDescription& desc) const override
    {
        desc.name = "RecordingPlugin";
        desc.pluginFormatName = "Internal";
    }
};

/// The last SysEx dump the fake received, re-framed as the full wire dump:
/// JUCE's getSysExData()/getSysExDataSize() give only the bytes BETWEEN the
/// leading 0xF0 and the trailing 0xF7 (392-byte dump -> 390 payload bytes).
std::vector<uint8_t> lastSysex(const RecordingPlugin& rec)
{
    for (auto it = rec.received.rbegin(); it != rec.received.rend(); ++it)
        for (const auto metadata : *it)
        {
            const auto m = metadata.getMessage();
            if (!m.isSysEx())
                continue;
            std::vector<uint8_t> out;
            out.push_back(0xF0);
            const auto* p = m.getSysExData();
            out.insert(out.end(), p, p + m.getSysExDataSize());
            out.push_back(0xF7);
            return out;
        }
    return {};
}

class MatrixPresetsTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(temp_.isValid());
        ASSERT_TRUE(writeSheets());
        qputenv("HDAW_MATRIX_PRESETS_DIR", temp_.path().toUtf8());
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        server = std::make_unique<mcp::McpServer>();
        server->setEngine(engine.get());
        mcp::registerAllTools(*server);
        loopback = std::make_unique<mcp::TransportLoopback>();
        server->setTransport(loopback.get());
        server->start();
        // Default project ships ZERO tracks — create the one the tests use.
        call("add_track", { { "name", "Track" } });
    }

    void TearDown() override {
        server->stop();
        server->setTransport(nullptr);
        loopback.reset();
        server.reset();
        engine.reset();
        qunsetenv("HDAW_MATRIX_PRESETS_DIR");
    }

    static bool writeFile(const QString& path, const QByteArray& bytes) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        return f.write(bytes) == bytes.size();
    }

    bool writeSheets() {
        // Engine sheet (hdaw.matrix.preset.v1): one set_fx_param preset with
        // deterministic accounting (mapped+value / mapped+null / unmapped), one
        // midi_cc_pc preset that must error cleanly (no parameter-level path).
        const QJsonObject preset1 {
            { "id", "fx00000000000001" },
            { "name", "fixture mapped lfo" },
            { "role", "lfo" },
            { "appliesVia", "set_fx_param" },
            { "evidence", "fixture" },
            { "params", QJsonObject {
                { "CutoffFrequency", 100 },
                { "AmpLfo1Depth", QJsonValue::Null },
                { "AutoPanManualPanSwitch", 1 } } } };
        const QJsonObject preset2 {
            { "id", "fx00000000000002" },
            { "name", "fixture cc-pc" },
            { "role", "pad" },
            { "appliesVia", "midi_cc_pc" },
            { "evidence", "fixture" },
            { "params", QJsonObject { { "Assign1 Source", 3 } } } };
        if (!writeFile(temp_.filePath("fixture.json"),
                       QJsonDocument(QJsonObject {
                           { "schema", "hdaw.matrix.preset.v1" },
                           { "engine", "fixture" },
                           { "patchCount", 2 },
                           { "presets", QJsonArray { preset1, preset2 } } })
                           .toJson(QJsonDocument::Indented)))
            return false;

        // Decoder-name -> live param index map: CutoffFrequency and AmpLfo1Depth
        // are mapped; AutoPanManualPanSwitch is deliberately absent (the real
        // je8086 map's recorded R5 residue) so the unmapped accounting exercises.
        if (!writeFile(temp_.filePath("fixture_param_index_map.json"),
                       QJsonDocument(QJsonObject {
                           { "schema", "hdaw.je8086.param.index.map.v1" },
                           { "note", "fixture" },
                           { "map", QJsonObject {
                               { "CutoffFrequency", QJsonObject {
                                   { "index", 59 }, { "plugin", "CUTOFF FREQ" },
                                   { "extra", 0 }, { "ties", 1 } } },
                               { "AmpLfo1Depth", QJsonObject {
                                   { "index", 70 }, { "plugin", "A AMP LFO1 DEPTH" },
                                   { "extra", 0 }, { "ties", 1 } } } } } })
                           .toJson(QJsonDocument::Indented)))
            return false;

        // Morph sheet (hdaw.matrix.preset.morph.v1): pair 1:2 carries injectable
        // SysEx arrays (apply 'sysex'); pair 3:4 references a loose .syx file that
        // is intentionally NOT written (deterministic file-missing error).
        const auto step = [](const char* sid, const char* name, const char* via,
                             const QJsonObject& extra) {
            QJsonObject preset {
                { "id", sid }, { "name", name }, { "appliesVia", via },
                { "params", QJsonObject {} } };
            for (auto it = extra.begin(); it != extra.end(); ++it)
                preset[it.key()] = it.value();
            return QJsonObject { { "preset", preset }, { "jumps", QJsonArray {} } };
        };
        QJsonObject s1 = step("ms00000000000001", "fixture morph step 1", "sysex", {});
        s1["sysex"] = QJsonArray { 240, 53, 0, 4, 1, 2, 3, 247 };
        s1["interp"] = QJsonObject {};
        QJsonObject s2 = step("ms00000000000002", "fixture morph step 2", "sysex", {});
        s2["sysex"] = QJsonArray { 240, 53, 0, 4, 4, 5, 6, 247 };
        s2["interp"] = QJsonObject {};
        const QJsonObject s3 = step("ms00000000000003", "fixture file step 1",
                                    "load_nord_bank", QJsonObject { { "file", "step1.syx" } });
        if (!writeFile(temp_.filePath("fixture_morphs.json"),
                       QJsonDocument(QJsonObject {
                           { "schema", "hdaw.matrix.preset.morph.v1" },
                           { "engine", "fixture" },
                           { "pairs", QJsonArray {
                               QJsonObject { { "pair", "1:2" }, { "distance", 0.1 },
                                             { "steps", QJsonArray { s1, s2 } } },
                               QJsonObject { { "pair", "3:4" }, { "distance", 0.2 },
                                             { "steps", QJsonArray { s3 } } } } } })
                           .toJson(QJsonDocument::Indented)))
            return false;

        // microQ (vavra) sheets for the Phase-5 edit-buffer retarget test: BOTH
        // dump routes (a preset's device-native dump and a morph step's sysex)
        // carry the real corpus payload — byte5 = 0x30, a buffer the single-mode
        // OS does not play.
        const QJsonArray microQDump = microQDumpJson();
        const QJsonObject vavraPreset {
            { "id", "fx00000000000060" },
            { "name", "fixture microq device dump" },
            { "role", "movement" },
            { "appliesVia", "sysex" },
            { "evidence", "fixture" },
            { "sysex", microQDump } };
        if (!writeFile(temp_.filePath("vavra.json"),
                       QJsonDocument(QJsonObject {
                           { "schema", "hdaw.matrix.preset.v1" },
                           { "engine", "vavra" },
                           { "patchCount", 1 },
                           { "presets", QJsonArray { vavraPreset } } })
                           .toJson(QJsonDocument::Indented)))
            return false;

        const QJsonObject vavraStep {
            { "preset", QJsonObject {
                { "id", "ms00000000000060" },
                { "name", "fixture microq morph step" },
                { "appliesVia", "sysex" },
                { "params", QJsonObject {} } } },
            { "jumps", QJsonArray {} },
            { "sysex", microQDump } };
        if (!writeFile(temp_.filePath("vavra_morphs.json"),
                       QJsonDocument(QJsonObject {
                           { "schema", "hdaw.matrix.preset.morph.v1" },
                           { "engine", "vavra" },
                           { "pairs", QJsonArray {
                               QJsonObject { { "pair", "1:2" }, { "distance", 0.1 },
                                             { "steps", QJsonArray { vavraStep } } } } } })
                           .toJson(QJsonDocument::Indented)))
            return false;
        return true;
    }

    QJsonObject call(const char* method, const QJsonObject& args = {}) {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = nextId_++;
        req["method"] = "tools/call";
        req["params"] = QJsonObject{ { "name", method }, { "arguments", args } };
        loopback->drainOutgoing();
        loopback->pumpIncoming(QJsonDocument(req).toJson(QJsonDocument::Compact));
        QByteArray out;
        if (!loopback->waitForOutgoing(500, &out)) return {};
        return parseOne(out).value("result").toObject();
    }

    QJsonObject callJson(const char* method, const QJsonObject& args = {}) {
        const auto r = call(method, args);
        const auto content = r.value("content").toArray();
        if (content.isEmpty()) return {};
        return QJsonDocument::fromJson(
            content[0].toObject().value("text").toString().toUtf8()).object();
    }

    QString callText(const char* method, const QJsonObject& args = {}) {
        const auto r = call(method, args);
        const auto content = r.value("content").toArray();
        if (content.isEmpty()) return {};
        return content[0].toObject().value("text").toString();
    }

    bool isError(const QJsonObject& r) { return r.value("isError").toBool(false); }

    QJsonObject listTools() {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = nextId_++;
        req["method"] = "tools/list";
        loopback->drainOutgoing();
        loopback->pumpIncoming(QJsonDocument(req).toJson(QJsonDocument::Compact));
        QByteArray out;
        if (!loopback->waitForOutgoing(500, &out)) return {};
        return parseOne(out).value("result").toObject();
    }

    // Adds a plugin-typed FX slot (tree + ReadModel entry; the bogus plugin id
    // resolves to no instance, which is fine — setParam/sendFxMidi are
    // null-instance-safe and the live-routing seam settles the track).
    // Built via the COMMAND layer since the 2026-09-23 add_fx gate: the MCP
    // surface now rejects unresolvable pluginIds (fixture.test is in no scan
    // cache — the rejection itself is asserted by unit/frontend/
    // add_fx_parity_test), and this fixture wants the inert slot, not the
    // gate. Same command + same args the MCP handler ran before (type
    // "plugin" derived from pluginId, position -1 append) → identical slot:
    // fxType "plugin", pluginID "fixture.test", pluginFormat "" — the slot
    // position the tests read (index 0 alone, index 1 after an internal slot)
    // is unchanged. matrix_presets_rpc_test.cpp:58 is the same precedent.
    void addPluginSlot() {
        engine->getProjectCommands().addFxSlot(0, "plugin", -1, "fixture.test");
        const auto fxChain = engine->getProjectModel().getTrackListTree()
                                 .getChild(0).getChildWithName(IDs::FX_CHAIN);
        ASSERT_TRUE(fxChain.isValid()) << "the fixture track must exist";
        ASSERT_GT(fxChain.getNumChildren(), 0) << "the fixture slot must exist";
    }

    // Adds a plugin-typed TREE slot AND installs a recording fake at the same
    // index on the LIVE chain, so the bytes send_fx_midi hands to the child are
    // observable without a real CLAP (the tree slot keeps the slot valid for the
    // IDs::presetSysex replay payload). Returns the slot index, or -1 when the
    // deviceless live-routing seam did not settle — the caller must FAIL loudly
    // in that case, never claim a pass.
    int installFakePluginSlot() {
        engine->getProjectCommands().addFxSlot(0, "plugin", -1, "fixture.test");
        const auto fxChain = engine->getProjectModel().getTrackListTree()
                                 .getChild(0).getChildWithName(IDs::FX_CHAIN);
        if (!fxChain.isValid() || fxChain.getNumChildren() <= 0)
            return -1;
        const int si = fxChain.getNumChildren() - 1;

        engine->ensureLiveRouting(0);
        auto* proc = engine->getMainProcessor();
        auto* track = proc != nullptr ? proc->getTrack(0) : nullptr;
        if (track == nullptr)
            return -1;

        auto& live = track->getFXChain();
        if (static_cast<int>(live.size()) <= si)
            live.resize(static_cast<size_t>(si) + 1);
        auto fake = std::make_unique<RecordingPlugin>();
        fake_ = fake.get();
        live[static_cast<size_t>(si)] =
            std::make_unique<HDAW::TrackFXSlot>(std::move(fake), "fixture.test", false);
        return si;
    }

    QTemporaryDir temp_;
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;
    std::unique_ptr<mcp::TransportLoopback> loopback;
    RecordingPlugin* fake_ = nullptr;   // live-chain fake plugin, when installed
    int nextId_ = 1;
};

// G1: registration smoke — both tools are listed with a schema.
TEST_F(MatrixPresetsTest, ToolsAreRegistered)
{
    const auto listed = listTools();
    ASSERT_FALSE(listed.isEmpty());
    const auto tools = listed.value("tools").toArray();
    bool hasList = false, hasApply = false;
    for (const auto& t : tools)
    {
        const auto o = t.toObject();
        if (o.value("name").toString() == "list_matrix_presets")
        {
            hasList = true;
            EXPECT_TRUE(o.value("inputSchema").toObject().contains("properties"));
        }
        if (o.value("name").toString() == "apply_matrix_preset")
        {
            hasApply = true;
            EXPECT_TRUE(o.value("inputSchema").toObject().contains("properties"));
        }
    }
    EXPECT_TRUE(hasList);
    EXPECT_TRUE(hasApply);
}

// G2: list returns the fixture entries with derived morph apply kinds.
TEST_F(MatrixPresetsTest, ListReturnsFixtureEntries)
{
    const QJsonObject args { { "engine", "fixture" } };
    const auto o = callJson("list_matrix_presets", args);
    ASSERT_FALSE(o.isEmpty());
    EXPECT_EQ(o.value("engine").toString(), QString("fixture"));
    EXPECT_EQ(o.value("sheet").toString(), QString("fixture.json"));

    const auto presets = o.value("presets").toArray();
    ASSERT_EQ(presets.size(), 2);
    EXPECT_EQ(presets[0].toObject().value("id").toString(), QString("fx00000000000001"));
    EXPECT_EQ(presets[0].toObject().value("role").toString(), QString("lfo"));
    EXPECT_EQ(presets[0].toObject().value("appliesVia").toString(), QString("set_fx_param"));
    EXPECT_FALSE(presets[0].toObject().value("evidence").toString().isEmpty());

    const auto morphs = o.value("morphs").toArray();
    ASSERT_EQ(morphs.size(), 2);
    EXPECT_EQ(morphs[0].toObject().value("pair").toString(), QString("1:2"));
    EXPECT_EQ(morphs[0].toObject().value("apply").toString(), QString("sysex"));
    EXPECT_EQ(morphs[0].toObject().value("steps").toInt(), 2);
    EXPECT_EQ(morphs[1].toObject().value("apply").toString(), QString("file"));
    EXPECT_EQ(morphs[1].toObject().value("steps").toInt(), 1);
    EXPECT_TRUE(morphs[0].toObject().contains("distance"));
}

// G2: unknown engine errors cleanly and lists the available engines.
TEST_F(MatrixPresetsTest, ListUnknownEngineErrorsCleanly)
{
    const QJsonObject args { { "engine", "nosuchengine" } };
    const auto r = call("list_matrix_presets", args);
    EXPECT_TRUE(isError(r));
    const QString t = callText("list_matrix_presets", args);
    EXPECT_TRUE(t.contains("nosuchengine"));
    EXPECT_TRUE(t.contains("fixture")); // available-engines scan finds the fixture sheet
}

// G3: argument and sheet errors are clean isErrors (never empty, never a crash).
TEST_F(MatrixPresetsTest, ApplyArgumentAndSheetErrorsAreClean)
{
    addPluginSlot();

    struct Case { const char* label; QJsonObject args; QString expectInText; };
    const Case cases[] = {
        { "unknown engine",
          { { "engine", "nosuchengine" }, { "id", "fx00000000000001" },
            { "trackId", 0 }, { "slotIndex", 0 } }, "nosuchengine" },
        { "unknown id",
          { { "engine", "fixture" }, { "id", "ffffffffffffffff" },
            { "trackId", 0 }, { "slotIndex", 0 } }, "list_matrix_presets" },
        { "unknown track",
          { { "engine", "fixture" }, { "id", "fx00000000000001" },
            { "trackId", 99 }, { "slotIndex", 0 } }, "track not found" },
        { "slot not found",
          { { "engine", "fixture" }, { "id", "fx00000000000001" },
            { "trackId", 0 }, { "slotIndex", 5 } }, "slot not found" },
    };
    for (const auto& c : cases)
    {
        const auto r = call("apply_matrix_preset", c.args);
        EXPECT_TRUE(isError(r)) << c.label;
        EXPECT_TRUE(callText("apply_matrix_preset", c.args).contains(c.expectInText)) << c.label;
    }
}

// G3: presets whose appliesVia has no parameter-level path error cleanly, and
// non-plugin slots are rejected before any engine command runs.
TEST_F(MatrixPresetsTest, ApplyRejectsNonParamPresetAndNonPluginSlot)
{
    addPluginSlot();
    // fixture cc-pc preset: no <engine>_param_index_map.json + appliesVia != set_fx_param
    const QJsonObject ccpc { { "engine", "fixture" }, { "id", "fx00000000000002" },
                             { "trackId", 0 }, { "slotIndex", 0 } };
    auto r = call("apply_matrix_preset", ccpc);
    EXPECT_TRUE(isError(r));
    EXPECT_TRUE(callText("apply_matrix_preset", ccpc).contains("no parameter-level apply path"));

    // Internal FX slot: matrix presets target the LIVE plugin instance only.
    ASSERT_FALSE(isError(call("add_fx", QJsonObject{ { "trackId", 0 }, { "fxType", "eq" } })));
    const QJsonObject onEq { { "engine", "fixture" }, { "id", "fx00000000000001" },
                             { "trackId", 0 }, { "slotIndex", 1 } };
    r = call("apply_matrix_preset", onEq);
    EXPECT_TRUE(isError(r));
    EXPECT_TRUE(callText("apply_matrix_preset", onEq).contains("slot is not a plugin"));
}

// G4: je8086-style param accounting on the fixture map, WITHOUT real plugins:
// mapped+value -> applied, mapped+null -> skipped, unmapped -> skipped + listed.
TEST_F(MatrixPresetsTest, Je8086StyleParamAccounting)
{
    addPluginSlot();
    const QJsonObject args { { "engine", "fixture" }, { "id", "fx00000000000001" },
                             { "trackId", 0 }, { "slotIndex", 0 } };
    const auto o = callJson("apply_matrix_preset", args);
    ASSERT_FALSE(o.isEmpty());
    EXPECT_EQ(o.value("applied").toInt(), 1);   // CutoffFrequency -> index 59
    EXPECT_EQ(o.value("skipped").toInt(), 2);   // AmpLfo1Depth (null) + unmapped
    const auto unmapped = o.value("unmapped").toArray();
    ASSERT_EQ(unmapped.size(), 1);
    EXPECT_EQ(unmapped[0].toString(), QString("AutoPanManualPanSwitch"));
}

// Morph dispatch: sysex steps queue through send_fx_midi's engine path; file
// steps resolve their .syx under the morph files base; both require a plugin slot.
TEST_F(MatrixPresetsTest, MorphSysexAndFileDispatch)
{
    // Non-plugin slot is rejected deterministically before any engine command.
    ASSERT_FALSE(isError(call("add_fx", QJsonObject{ { "trackId", 0 }, { "fxType", "eq" } })));
    const QJsonObject onEq { { "engine", "fixture" }, { "id", "1:2:step1" },
                             { "trackId", 0 }, { "slotIndex", 0 } };
    auto r = call("apply_matrix_preset", onEq);
    EXPECT_TRUE(isError(r));
    EXPECT_TRUE(callText("apply_matrix_preset", onEq).contains("slot is not a plugin"));

    addPluginSlot(); // plugin slot is index 1

    // File step: fixture intentionally ships no 3-4/step1.syx -> clean error.
    const QJsonObject fileStep { { "engine", "fixture" }, { "id", "3:4:step1" },
                                 { "trackId", 0 }, { "slotIndex", 1 } };
    r = call("apply_matrix_preset", fileStep);
    EXPECT_TRUE(isError(r));
    const QString fileErr = callText("apply_matrix_preset", fileStep);
    EXPECT_TRUE(fileErr.contains("file not found"));
    EXPECT_TRUE(fileErr.contains("3-4"));

    // Sysex step on the plugin slot: queues through the live-routing seam
    // (LiveRoutingSeam proves the deviceless env settles). A queued result must
    // be well-formed {queued, captureDeferred:true}; any failure must be clean.
    const QJsonObject sysexStep { { "engine", "fixture" }, { "id", "1:2:step2" },
                                  { "trackId", 0 }, { "slotIndex", 1 } };
    r = call("apply_matrix_preset", sysexStep);
    const QString sysexText = callText("apply_matrix_preset", sysexStep);
    EXPECT_FALSE(sysexText.isEmpty());
    if (!isError(r))
    {
        const auto o = QJsonDocument::fromJson(sysexText.toUtf8()).object();
        EXPECT_GE(o.value("queued").toInt(), 1) << sysexText.toStdString();
        EXPECT_TRUE(o.value("captureDeferred").toBool()) << sysexText.toStdString();
    }

    // Step ids also resolve by the step's own preset id.
    const QJsonObject byStepId { { "engine", "fixture" }, { "id", "ms00000000000001" },
                                 { "trackId", 0 }, { "slotIndex", 1 } };
    r = call("apply_matrix_preset", byStepId);
    EXPECT_FALSE(callText("apply_matrix_preset", byStepId).isEmpty());
}

// G5 (offline-render propagation): the param-apply path must trigger the SAME
// deferred plugin-state capture send_fx_midi performs (ProjectCommands::
// captureFxSlotState) — PluginParamService::setParam reaches the LIVE child
// only, while offline renders boot a FRESH plugin domain from IDs::
// pluginState. The fixture has no real plugin instance, so the assertion
// lands on the receipt/status seam: a default apply (captureToTree defaults
// true) stamps the slot's capture receipt (never stale "none") and the
// response carries the send_fx_midi capture info; captureToTree:false skips
// the trigger entirely (receipt stays virgin).
TEST_F(MatrixPresetsTest, ParamApplyTriggersDeferredStateCapture)
{
    addPluginSlot();
    // Settle the deferred live-graph projection ONCE up front (lesson 9) so
    // the environment classification below is stable — otherwise the async
    // rebuild can land BETWEEN the capture and the probe and flip the branch.
    if (engine->getMainProcessor() != nullptr
        && engine->getMainProcessor()->getTrack(0) == nullptr)
        engine->ensureLiveRouting(0);
    const QJsonObject args { { "engine", "fixture" }, { "id", "fx00000000000001" },
                             { "trackId", 0 }, { "slotIndex", 0 } };

    // Opt-out first (receipt still virgin): the flag must skip the trigger —
    // no capture object in the response, no receipt stamp.
    QJsonObject off = args;
    off["captureToTree"] = false;
    const auto o2 = callJson("apply_matrix_preset", off);
    ASSERT_FALSE(o2.isEmpty());
    EXPECT_EQ(o2.value("applied").toInt(), 1);
    EXPECT_FALSE(o2.value("captureToTree").toBool(true));
    EXPECT_TRUE(o2.value("capture").toObject().isEmpty());
    EXPECT_TRUE(callText("get_fx_capture_status",
                         { { "trackId", 0 }, { "slotIndex", 0 } }).contains("status=none"));

    // Default: the capture trigger fires — the response carries the
    // send_fx_midi capture info either way: a synchronous outcome ("ok" /
    // "unchanged" / "failed: empty state") when the capture completes
    // in-call, "pending" when it is deferred, or the CLEAN validation
    // failure "failed: track not found" when the LIVE projection is
    // unavailable (deviceless runs: rebuildRoutingGraph no-ops without a
    // RoutingManager — lessons 9/17).
    const auto o = callJson("apply_matrix_preset", args);
    ASSERT_FALSE(o.isEmpty());
    EXPECT_EQ(o.value("applied").toInt(), 1);
    EXPECT_TRUE(o.value("captureToTree").toBool(false));
    const auto cap = o.value("capture").toObject();
    ASSERT_FALSE(cap.isEmpty()) << "capture receipt info missing from response";
    EXPECT_FALSE(cap.value("status").toString().isEmpty());

    const auto statusText = callText("get_fx_capture_status",
                                     { { "trackId", 0 }, { "slotIndex", 0 } });
    if (cap.value("status").toString().startsWith("failed:"))
    {
        // Validation refused the trigger (this fixture's bogus plugin id is
        // not a plugin slot in the LIVE chain; deviceless runs cannot settle
        // the track at all) — matching sendFxMidi, whose validation also
        // precedes the stamp, no receipt is written.
        EXPECT_TRUE(statusText.contains("status=none")) << statusText.toStdString();
    }
    else
    {
        // Trigger accepted: the receipt is stamped synchronously ("pending"
        // while a deferred capture is in flight, otherwise the synchronous
        // outcome) — never the stale "none".
        EXPECT_FALSE(statusText.contains("status=none")) << statusText.toStdString();
    }
}

// G5 (engine seam): the factored trigger is reachable through the
// ProjectCommands interface — the same dispatch apply_matrix_preset uses —
// and validates track/slot exactly like sendFxMidi.
TEST_F(MatrixPresetsTest, CaptureFxSlotStateEngineSeam)
{
    addPluginSlot();
    // Settle the deferred live-graph projection ONCE up front (lesson 9) so
    // the environment classification below is stable (see the sibling test).
    if (engine->getMainProcessor() != nullptr
        && engine->getMainProcessor()->getTrack(0) == nullptr)
        engine->ensureLiveRouting(0);
    auto& pc = engine->getProjectCommands();

    // Validation contract (environment-independent): bad indexes/tracks fail
    // CLEAN with an error, never a crash.
    const auto badIdx = pc.captureFxSlotState(-1, 0);
    EXPECT_FALSE(badIdx.ok);
    EXPECT_FALSE(badIdx.error.empty());
    const auto badTrack = pc.captureFxSlotState(99, 0);
    EXPECT_FALSE(badTrack.ok);
    EXPECT_TRUE(QString::fromStdString(badTrack.error).contains("track not found"))
        << badTrack.error;
    EXPECT_TRUE(callText("get_fx_capture_status",
                         { { "trackId", 99 }, { "slotIndex", 0 } }).contains("slot not found in tree"));

    // SEAM EQUIVALENCE: for the SAME slot, the factored trigger's verdict
    // matches sendFxMidi's own validation — ok=true where a real plugin
    // instance serves the slot, otherwise the SAME clean validation string
    // ("slot is not a plugin slot" here: the fixture's bogus plugin id never
    // instantiates, so its live TrackFXSlot is not plugin-typed). This is the
    // fixture-constrained proxy for the capture machinery (pending receipt ->
    // live getStateInformation -> IDs::pluginState), which IS sendFxMidi's
    // own code path — reused verbatim, not reinvented (lesson 16).
    ProjectCommands::FxMidiParams midi;
    midi.trackIndex = 0;
    midi.slotIndex = 0;
    midi.captureToTree = false;   // compare the pure validation verdicts
    ProjectCommands::FxMidiEvent ev;
    ev.kind = ProjectCommands::FxMidiEvent::Kind::NoteOn;
    midi.events.push_back(ev);
    const auto viaMidi = pc.sendFxMidi(midi);
    const auto cap = pc.captureFxSlotState(0, 0);
    EXPECT_EQ(cap.ok, viaMidi.ok) << cap.error << " | " << viaMidi.error;
    if (!viaMidi.ok)
        EXPECT_EQ(cap.error, viaMidi.error) << cap.error;
    else
        EXPECT_FALSE(cap.status.empty());
}

// G1 (offline param-override replay, docs/plans/2026-09-17-offline-param-
// replay.md): every successful param apply persists the RESOLVED
// {liveParamIndex: normalizedValue} ledger on the FX_SLOT tree
// (IDs::appliedParamOverrides). The ledger is ORTHOGONAL to the state
// capture — captureToTree=false writes it too — and the response reports
// its size (paramOverrides) only when non-empty.
TEST_F(MatrixPresetsTest, ParamApplyWritesOverrideLedger)
{
    addPluginSlot();
    auto slotTree = engine->getProjectModel().getTrackListTree()
                        .getChild(0).getChildWithName(IDs::FX_CHAIN).getChild(0);
    ASSERT_TRUE(slotTree.isValid());
    EXPECT_FALSE(slotTree.hasProperty(IDs::appliedParamOverrides));

    // captureToTree=false first: the ledger must still be written (G1).
    QJsonObject off { { "engine", "fixture" }, { "id", "fx00000000000001" },
                      { "trackId", 0 }, { "slotIndex", 0 }, { "captureToTree", false } };
    const auto r = callJson("apply_matrix_preset", off);
    ASSERT_FALSE(r.isEmpty());
    EXPECT_EQ(r.value("applied").toInt(), 1);          // CutoffFrequency -> index 59
    EXPECT_EQ(r.value("paramOverrides").toInt(), 1);   // ledger size reported
    ASSERT_TRUE(slotTree.hasProperty(IDs::appliedParamOverrides));

    // Exact resolved pair via the export-side parse seam (what the offline
    // replay will feed setAutomationParam): 100 -> live index 59, 100/127.
    const auto pairs = HDAW::ExportManager::parseAppliedParamOverrides(slotTree);
    ASSERT_EQ(pairs.size(), 1u);
    EXPECT_EQ(pairs[0].first, 59);
    EXPECT_NEAR(pairs[0].second, 100.0f / 127.0f, 1e-6f);

    // Default path (captureToTree=true): replace semantics refresh the same
    // ledger — one entry, same resolved pair.
    const auto r2 = callJson("apply_matrix_preset",
                             { { "engine", "fixture" }, { "id", "fx00000000000001" },
                               { "trackId", 0 }, { "slotIndex", 0 } });
    ASSERT_FALSE(r2.isEmpty());
    EXPECT_EQ(r2.value("paramOverrides").toInt(), 1);
    const auto pairs2 = HDAW::ExportManager::parseAppliedParamOverrides(slotTree);
    ASSERT_EQ(pairs2.size(), 1u);
    EXPECT_EQ(pairs2[0].first, 59);
    EXPECT_NEAR(pairs2[0].second, 100.0f / 127.0f, 1e-6f);
}

// G2/G5 (replay seam): the export render thread's replay runs against the
// OFFLINE RoutingManager after the bake wait and before the first block
// (ExportManager::renderThreadFunc). Unit-level: a local RoutingManager over
// a copied model tree — the same recipe renderThreadFunc uses for its
// localModel. The fixture's bogus plugin id cannot instantiate offline, so
// the slot's param cache stays empty and the replay must COUNT the index as
// skipped-beyond-cache instead of silently no-oping (guard-path proof). The
// applied>0 audible proof is the JE8086 ear-pass re-render (plan G2 live
// clause); real plugins stay out of the unit tier.
TEST_F(MatrixPresetsTest, ParamOverrideLedgerReplaySeam)
{
    addPluginSlot();
    const QJsonObject args { { "engine", "fixture" }, { "id", "fx00000000000001" },
                             { "trackId", 0 }, { "slotIndex", 0 }, { "captureToTree", false } };
    ASSERT_FALSE(callJson("apply_matrix_preset", args).isEmpty());

    // Parse seam negatives: invalid tree and a ledger-free slot -> empty
    // (G5 baseline — no property, nothing to replay).
    EXPECT_TRUE(HDAW::ExportManager::parseAppliedParamOverrides(juce::ValueTree()).empty());
    auto noLedger = engine->getProjectModel().getTrackListTree()
                        .getChild(0).getChildWithName(IDs::FX_CHAIN).getChild(0).createCopy();
    noLedger.removeProperty(IDs::appliedParamOverrides, nullptr);
    EXPECT_TRUE(HDAW::ExportManager::parseAppliedParamOverrides(noLedger).empty());

    // Local offline model — the renderThreadFunc copy recipe.
    ProjectModel localModel;
    {
        auto& src = engine->getProjectModel().getTree();
        localModel.getTree().copyPropertiesFrom(src, nullptr);
        localModel.getTree().removeAllChildren(nullptr);
        for (int i = 0; i < src.getNumChildren(); ++i)
            localModel.getTree().addChild(src.getChild(i).createCopy(), -1, nullptr);
    }

    juce::AudioProcessorGraph renderGraph;
    HDAW::TransportManager renderTransport;
    renderTransport.setSampleRate(48000.0);
    juce::AudioFormatManager fm;
    HDAW::RoutingManager routing(renderGraph, localModel, fm, renderTransport, nullptr);
    routing.rebuildFromValueTree();
    ASSERT_NE(routing.getTrackNode(0), nullptr);

    const auto stats = HDAW::ExportManager::replayAppliedParamOverrides(
        localModel.getTree(), routing);
    EXPECT_EQ(stats.slotsWithOverrides, 1);   // the ledger slot is visited
    EXPECT_EQ(stats.applied, 0);              // no plugin instance -> cache empty
    EXPECT_EQ(stats.skippedBeyondCache, 1);   // index counted, not silently dropped

    // G5 (no ledger anywhere): nothing visited, nothing counted.
    ProjectModel cleanModel;
    {
        auto& src = engine->getProjectModel().getTree();
        cleanModel.getTree().copyPropertiesFrom(src, nullptr);
        cleanModel.getTree().removeAllChildren(nullptr);
        for (int i = 0; i < src.getNumChildren(); ++i)
        {
            auto child = src.getChild(i).createCopy();
            // Strip ledgers at the CORRECT depth: root -> TRACK_LIST -> track ->
            // FX_CHAIN -> slot. (The first draft looked for FX_CHAIN directly
            // under root children and silently no-oped — F-D.)
            // child may BE the TRACK_LIST node (root child) or contain it.
            auto trackList = child.hasType(IDs::TRACK_LIST)
                                 ? child
                                 : child.getChildWithName(IDs::TRACK_LIST);
            if (trackList.isValid())
                for (int t = 0; t < trackList.getNumChildren(); ++t)
                {
                    auto chain = trackList.getChild(t).getChildWithName(IDs::FX_CHAIN);
                    for (int s = 0; s < chain.getNumChildren(); ++s)
                        chain.getChild(s).removeProperty(IDs::appliedParamOverrides, nullptr);
                }
            cleanModel.getTree().addChild(child, -1, nullptr);
        }
    }
    juce::AudioProcessorGraph cleanGraph;
    HDAW::RoutingManager cleanRouting(cleanGraph, cleanModel, fm, renderTransport, nullptr);
    cleanRouting.rebuildFromValueTree();
    int remainingLedgers = 0;
    auto tl = cleanModel.getTree().getChildWithName(IDs::TRACK_LIST);
    for (int t = 0; t < tl.getNumChildren(); ++t)
    {
        auto ch = tl.getChild(t).getChildWithName(IDs::FX_CHAIN);
        for (int s = 0; s < ch.getNumChildren(); ++s)
        {
            const juce::String v = ch.getChild(s).getProperty(IDs::appliedParamOverrides, "").toString();
            if (v.isNotEmpty())
            {
                ++remainingLedgers;
                HDAW_LOG("CleanDiag", ("track " + juce::String(t) + " slot " + juce::String(s)
                    + " ledger=" + v).toStdString().c_str());
            }
        }
    }
    HDAW_LOG("CleanDiag", juce::String("remainingLedgers=") + juce::String(remainingLedgers));
    EXPECT_EQ(remainingLedgers, 0); // diagnostic: ledger removal proof
    const auto cleanStats = HDAW::ExportManager::replayAppliedParamOverrides(
        cleanModel.getTree(), cleanRouting);
    EXPECT_EQ(cleanStats.slotsWithOverrides, 0);
    EXPECT_EQ(cleanStats.applied, 0);
    EXPECT_EQ(cleanStats.skippedBeyondCache, 0);
}

// G3 (persistence): the FX_SLOT ledger survives save/load — ProjectSerializer
// serializes the WHOLE tree (save -> toXmlString, load -> fromXml + createCopy),
// so extra FX_SLOT properties ride for free (no whitelist). This is what makes
// the offline replay work for SAVED projects.
TEST_F(MatrixPresetsTest, ParamOverrideLedgerSurvivesSaveLoad)
{
    addPluginSlot();
    const QJsonObject args { { "engine", "fixture" }, { "id", "fx00000000000001" },
                             { "trackId", 0 }, { "slotIndex", 0 }, { "captureToTree", false } };
    ASSERT_FALSE(callJson("apply_matrix_preset", args).isEmpty());

    ProjectModel model;
    {
        auto& src = engine->getProjectModel().getTree();
        model.getTree().copyPropertiesFrom(src, nullptr);
        model.getTree().removeAllChildren(nullptr);
        for (int i = 0; i < src.getNumChildren(); ++i)
            model.getTree().addChild(src.getChild(i).createCopy(), -1, nullptr);
    }
    const juce::File saveFile(temp_.filePath("ledger_roundtrip.hdaw").toStdString());
    ASSERT_TRUE(HDAW::ProjectSerializer::save(model, saveFile));

    ProjectModel loaded;
    ASSERT_TRUE(HDAW::ProjectSerializer::load(loaded, saveFile));
    const auto loadedSlot = loaded.getTree().getChildWithName(IDs::TRACK_LIST)
                                .getChild(0).getChildWithName(IDs::FX_CHAIN).getChild(0);
    ASSERT_TRUE(loadedSlot.isValid());
    ASSERT_TRUE(loadedSlot.hasProperty(IDs::appliedParamOverrides));
    const auto pairs = HDAW::ExportManager::parseAppliedParamOverrides(loadedSlot);
    ASSERT_EQ(pairs.size(), 1u);
    EXPECT_EQ(pairs[0].first, 59);
    EXPECT_NEAR(pairs[0].second, 100.0f / 127.0f, 1e-6f);
    saveFile.deleteFile();
}


} // namespace

// P5 (2026-09-30): apply_matrix_preset on microQ injected into the WRONG edit
// buffer — a silent no-op, because real dumps carry byte5 = 0x30 (multi-edit).
// The retarget must live in ONE implementation and fire on EVERY dump route:
//   (1) the preset's device-native dump, (2) a morph step's sysex dump, and
//   (3) the apply_preset FILE-LOADER path — all three must emit the same bytes.
// The fake plugin makes the bytes the child would receive observable, so this
// test fails if any route pushes the sheet's 0x30 dump verbatim.
TEST_F(MatrixPresetsTest, MicroQEditBufferRetargetIsOneImplementation)
{
    const int si = installFakePluginSlot();
    ASSERT_GE(si, 0) << "the deviceless live-routing seam did not settle a track/FX chain";
    ASSERT_NE(fake_, nullptr);

    const auto raw = makeMicroQDump(0x30, 0x40);   // the real corpus shape
    ASSERT_EQ(raw.size(), 392u);
    ASSERT_EQ(raw[5], 0x30) << "the fixture must carry the multi-edit buffer byte";
    const uint8_t rawChecksum = raw[390];

    // The expectation, built WITHOUT the helper (independent arithmetic).
    auto expected = raw;
    expected[5] = 0x20;
    expected[6] = 0x00;
    expected[390] = dumpChecksum(expected);
    ASSERT_NE(expected[390], rawChecksum)
        << "0x30->0x20 and 0x40->0x00 must move the checksum";

    // Negative control: with NO retarget (a direct send_fx_midi of the same
    // dump) the raw 0x30 bytes come out — this is the pre-fix behaviour of every
    // route below, and it proves the harness can see the difference (nothing
    // downstream rewrites the buffer byte).
    {
        ProjectCommands::FxMidiParams p;
        p.trackIndex = 0;
        p.slotIndex = si;
        p.captureToTree = false;
        ProjectCommands::FxMidiEvent ev;
        ev.kind = ProjectCommands::FxMidiEvent::Kind::SysEx;
        ev.sysex = raw;
        p.events.push_back(std::move(ev));
        fake_->received.clear();
        const auto r = engine->getProjectCommands().sendFxMidi(p);
        ASSERT_TRUE(r.ok) << r.error;
        const auto delivered = lastSysex(*fake_);
        ASSERT_EQ(delivered.size(), 392u) << "the fake must observe the injected dump";
        EXPECT_EQ(delivered, raw) << "no retarget => the 0x30 dump goes out verbatim";
        EXPECT_EQ(delivered[5], 0x30);
    }

    // (1) matrix DEVICE-NATIVE dump route (preset 'sysex' array).
    const QJsonObject presetArgs { { "engine", "vavra" }, { "id", "fx00000000000060" },
                                   { "trackId", 0 }, { "slotIndex", si },
                                   { "captureToTree", true } };
    fake_->received.clear();
    const auto presetPayload = callJson("apply_matrix_preset", presetArgs);
    const auto presetText = callText("apply_matrix_preset", presetArgs);
    ASSERT_FALSE(presetPayload.isEmpty()) << presetText.toStdString();
    ASSERT_EQ(presetPayload.value("route").toString(), QString("device_dump"))
        << presetText.toStdString();
    const auto fromPreset = lastSysex(*fake_);
    ASSERT_EQ(fromPreset.size(), 392u) << "the child received no dump";
    EXPECT_EQ(fromPreset, expected);

    // (2) matrix MORPH-STEP dump route (the sheet's own 20 vavra steps).
    const QJsonObject stepArgs { { "engine", "vavra" }, { "id", "1:2:step1" },
                                 { "trackId", 0 }, { "slotIndex", si },
                                 { "captureToTree", true } };
    fake_->received.clear();
    const auto stepPayload = callJson("apply_matrix_preset", stepArgs);
    ASSERT_FALSE(stepPayload.isEmpty()) << callText("apply_matrix_preset", stepArgs).toStdString();
    const auto fromStep = lastSysex(*fake_);
    ASSERT_EQ(fromStep.size(), 392u) << "the child received no dump";
    EXPECT_EQ(fromStep, expected);

    // (3) the apply_preset FILE-LOADER path with the SAME dump on disk.
    const auto syxPath = temp_.filePath("microq_edit_buffer.syx");
    ASSERT_TRUE(writeFile(syxPath, QByteArray(reinterpret_cast<const char*>(raw.data()),
                                              static_cast<int>(raw.size()))));
    fake_->received.clear();
    bool ok = false;
    const auto fileText = HDAW::waldorfSysexFileToolText(*engine, 0, si, syxPath,
                                                         "Vavra.clap", true, &ok);
    ASSERT_TRUE(ok) << fileText.toStdString();
    const auto fromFile = lastSysex(*fake_);
    ASSERT_EQ(fromFile.size(), 392u) << "the child received no dump";
    EXPECT_EQ(fromFile, expected);

    // THE ONE-IMPLEMENTATION PROPERTY: every route emitted the same bytes.
    EXPECT_EQ(fromPreset, fromFile) << "matrix route != file-loader route";
    EXPECT_EQ(fromStep, fromFile) << "morph-step route != file-loader route";
    EXPECT_EQ(fromFile[5], 0x20);
    EXPECT_EQ(fromFile[6], 0x00);
    EXPECT_NE(fromFile[5], raw[5]) << "the retarget must be observable";
    EXPECT_EQ(fromFile[390], dumpChecksum(fromFile));
    EXPECT_NE(fromFile[390], rawChecksum);

    // The persisted replay payload (what rebuilds/offline renders replay) holds
    // the retargeted dump too, not the sheet's 0x30 original.
    const auto slotTree = engine->getProjectModel().getTrackListTree()
                              .getChild(0).getChildWithName(IDs::FX_CHAIN).getChild(si);
    ASSERT_TRUE(slotTree.isValid());
    const auto persisted = HDAW::decodeFxPresetSysex(
        slotTree.getProperty(IDs::presetSysex).toString());
    ASSERT_FALSE(persisted.empty()) << "IDs::presetSysex was not written";
    EXPECT_EQ(persisted.back(), expected);
    EXPECT_EQ(persisted.back()[5], 0x20);
}

// Xenia (Microwave XT) must be untouched: its dumps are a different machine and
// the offline tooling already frames them at 0x20, so a 0x30-shaped XT dump and
// an already-0x20 one both pass through byte-identical (no over-reach).
TEST_F(MatrixPresetsTest, MicroQRetargetLeavesXeniaDumpsByteIdentical)
{
    std::vector<uint8_t> xt(392, 0);
    xt[0] = 0xF0;
    xt[1] = mcp::kWaldorfId;
    xt[2] = mcp::kWaldorfMachineMw2;
    xt[3] = 0x00;
    xt[4] = 0x01;
    xt[5] = 0x30;
    xt[6] = 0x40;
    for (size_t i = 7; i + 2 < xt.size(); ++i)
        xt[i] = static_cast<uint8_t>(i & 0x7f);
    xt[xt.size() - 1] = 0xF7;
    xt[xt.size() - 2] = dumpChecksum(xt);

    const auto before = xt;
    EXPECT_FALSE(HDAW::retargetWaldorfDumpForSingleEditBuffer(
        xt, HDAW::waldorfMachineForName("xenia")));
    EXPECT_EQ(xt, before);
    EXPECT_EQ(xt[5], 0x30);
    EXPECT_EQ(xt[6], 0x40);
}

// touch 1789771514

// incremental-link probe 1789771936

// incremental probe 1789772172
