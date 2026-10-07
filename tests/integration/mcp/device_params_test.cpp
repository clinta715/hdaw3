// Device-parameter MCP tool (list_device_params) — slice 1 of
// docs/plans/2026-09-21-device-param-map.md.
//
// Fixture maps are written to a temp dir and HDAW_DEVICE_MAP_DIR points at it,
// so the suite exercises dir resolution, index mode, filtering, truncation and
// the error paths WITHOUT the real generated corpus. The real maps are covered
// by the generator's own validation + --check determinism gate.

#include <gtest/gtest.h>

#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "mcp/McpTransportLoopback.h"
#include "mcp/McpJsonRpc.h"

#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTemporaryDir>
#include <QtGlobal>

#include <memory>

namespace {

QJsonObject parseOne(const QByteArray& buf)
{
    const int nl = buf.indexOf('\n');
    const QByteArray line = nl >= 0 ? buf.left(nl) : buf;
    return QJsonDocument::fromJson(line).object();
}

class DeviceParamsTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(temp_.isValid());
        ASSERT_TRUE(writeMaps());
        qputenv("HDAW_DEVICE_MAP_DIR", temp_.path().toUtf8());
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        server = std::make_unique<mcp::McpServer>();
        server->setEngine(engine.get());
        mcp::registerAllTools(*server);
        loopback = std::make_unique<mcp::TransportLoopback>();
        server->setTransport(loopback.get());
        server->start();
    }

    void TearDown() override {
        server->stop();
        server->setTransport(nullptr);
        loopback.reset();
        server.reset();
        engine.reset();
        qunsetenv("HDAW_DEVICE_MAP_DIR");
    }

    static bool writeFile(const QString& path, const QByteArray& bytes) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        return f.write(bytes) == bytes.size();
    }

    static QJsonObject param(const char* name, const char* category, const char* tier,
                             const QJsonArray& intents, const QJsonArray& stages) {
        QJsonObject o {
            { "name", name },
            { "category", category },
            { "tier", tier },
            { "intents", intents },
            { "stages", stages },
            { "index", QJsonValue::Null },
            { "offset", QJsonValue::Null },
            { "trapReason", QJsonValue::Null },
            { "note", QJsonValue::Null } };
        return o;
    }

    bool writeMaps() {
        // Engine map matching the generator's shape (hdaw.device.param.map.v1):
        // one movement param with both index+offset, one identity param with
        // index only, one trap with offset only, one movement param with index.
        QJsonObject p1 = param("F1Cutoff", "filter", "movement",
                               { "filter-sweep" }, { "fx-automation-engineer" });
        p1["index"] = 12; p1["offset"] = 119;

        QJsonObject p2 = param("UniDetune", "osc", "identity",
                               { "detune-width" }, { "sound-selector" });
        p2["index"] = 20;

        QJsonObject p3 = param("Fx1ChorusSpeed", "fx-chorus", "trap",
                               { "chorus-motion" }, QJsonArray{});
        p3["offset"] = 44;
        p3["trapReason"] = "bit-alias";
        p3["note"] = "derived-parameter collapse onto FX1Type/FX1Mix";

        QJsonObject p4 = param("DelayTime", "fx-delay", "movement",
                               { "delay-throw" }, { "fx-automation-engineer" });
        p4["index"] = 33;

        if (!writeFile(temp_.filePath("fixture.params.json"),
                       QJsonDocument(QJsonObject {
                           { "schema", "hdaw.device.param.map.v1" },
                           { "engine", "fixture" },
                           { "verifiedOn", "2026-09-21" },
                           { "appliesVia", "set_fx_param" },
                           { "durability", "jpar" },
                           { "durabilityNote", "fixture note" },
                           { "counts", QJsonObject {
                               { "total", 4 }, { "movement", 2 }, { "identity", 1 },
                               { "trap", 1 }, { "unclassified", 0 } } },
                           { "params", QJsonArray { p1, p2, p3, p4 } } })
                           .toJson(QJsonDocument::Indented)))
            return false;

        if (!writeFile(temp_.filePath("index.json"),
                       QJsonDocument(QJsonObject {
                           { "schema", "hdaw.device.index.v1" },
                           { "verifiedOn", "2026-09-21" },
                           { "engines", QJsonArray { QJsonObject {
                               { "engine", "fixture" }, { "paramCount", 4 },
                               { "appliesVia", "set_fx_param" }, { "durability", "jpar" } } } },
                           { "intents", QJsonArray {
                               QJsonObject { { "id", "filter-sweep" },
                                             { "label", "Filter cutoff sweep" },
                                             { "kind", "movement" },
                                             { "stage", "fx-automation-engineer" } },
                               QJsonObject { { "id", "detune-width" },
                                             { "label", "Detune / unison width" },
                                             { "kind", "identity" },
                                             { "stage", "sound-selector" } } } },
                           { "stages", QJsonArray { "fx-automation-engineer", "sound-selector" } },
                           { "stageNotes", QJsonObject {
                               { "sound-selector", "Picks identity params." },
                               { "fx-automation-engineer", "Owns movement intents." } } },
                           { "excludedStages", QJsonArray { "arranger" } } })
                           .toJson(QJsonDocument::Indented)))
            return false;

        // A map with the wrong schema must be rejected loudly.
        if (!writeFile(temp_.filePath("badschema.params.json"),
                       QJsonDocument(QJsonObject {
                           { "schema", "hdaw.not.the.map.v1" },
                           { "engine", "badschema" },
                           { "params", QJsonArray{} } })
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

    QTemporaryDir temp_;
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;
    std::unique_ptr<mcp::TransportLoopback> loopback;
    int nextId_ = 1;
};

// G2: registration smoke — the tool is listed with a schema.
TEST_F(DeviceParamsTest, ToolIsRegistered)
{
    const auto listed = listTools();
    ASSERT_FALSE(listed.isEmpty());
    bool found = false;
    for (const auto& t : listed.value("tools").toArray())
    {
        const auto o = t.toObject();
        if (o.value("name").toString() == "list_device_params")
        {
            found = true;
            EXPECT_TRUE(o.value("inputSchema").toObject().contains("properties"));
        }
    }
    EXPECT_TRUE(found);
}

// G3: index mode (no engine) returns engines + intent vocabulary + stages.
TEST_F(DeviceParamsTest, IndexModeReturnsVocabulary)
{
    const auto o = callJson("list_device_params");
    ASSERT_FALSE(o.isEmpty());
    EXPECT_EQ(o.value("schema").toString(), QString("hdaw.device.index.v1"));
    ASSERT_FALSE(o.value("engines").toArray().isEmpty());
    EXPECT_EQ(o.value("engines").toArray()[0].toObject().value("engine").toString(),
              QString("fixture"));
    EXPECT_FALSE(o.value("intents").toArray().isEmpty());
    EXPECT_FALSE(o.value("stages").toArray().isEmpty());
    EXPECT_EQ(o.value("stages").toArray()[0].toString(), QString("fx-automation-engineer"));
}

// G4a: engine mode returns the full map with route/durability metadata.
TEST_F(DeviceParamsTest, EngineModeReturnsMap)
{
    const QJsonObject args { { "engine", "fixture" } };
    const auto o = callJson("list_device_params", args);
    ASSERT_FALSE(o.isEmpty());
    EXPECT_EQ(o.value("engine").toString(), QString("fixture"));
    EXPECT_EQ(o.value("appliesVia").toString(), QString("set_fx_param"));
    EXPECT_EQ(o.value("durability").toString(), QString("jpar"));
    EXPECT_EQ(o.value("matched").toInt(), 4);
    EXPECT_EQ(o.value("params").toArray().size(), 4);
    EXPECT_FALSE(o.value("truncated").toBool());

    // nullable fields are omitted when null, present when set
    const auto first = o.value("params").toArray()[0].toObject();
    EXPECT_EQ(first.value("name").toString(), QString("F1Cutoff"));
    EXPECT_TRUE(first.contains("index"));
    EXPECT_TRUE(first.contains("offset"));
    EXPECT_FALSE(first.contains("trapReason"));
}

// G4b: each filter dimension narrows correctly.
TEST_F(DeviceParamsTest, FiltersNarrow)
{
    const auto byCategory = callJson("list_device_params", { { "engine", "fixture" }, { "category", "filter" } });
    ASSERT_EQ(byCategory.value("matched").toInt(), 1);
    EXPECT_EQ(byCategory.value("params").toArray()[0].toObject().value("name").toString(),
              QString("F1Cutoff"));

    const auto byIntent = callJson("list_device_params", { { "engine", "fixture" }, { "intent", "delay-throw" } });
    ASSERT_EQ(byIntent.value("matched").toInt(), 1);
    EXPECT_EQ(byIntent.value("params").toArray()[0].toObject().value("name").toString(),
              QString("DelayTime"));

    const auto byStage = callJson("list_device_params", { { "engine", "fixture" }, { "stage", "sound-selector" } });
    ASSERT_EQ(byStage.value("matched").toInt(), 1);
    EXPECT_EQ(byStage.value("params").toArray()[0].toObject().value("name").toString(),
              QString("UniDetune"));

    const auto byTier = callJson("list_device_params", { { "engine", "fixture" }, { "tier", "trap" } });
    ASSERT_EQ(byTier.value("matched").toInt(), 1);
    const auto trap = byTier.value("params").toArray()[0].toObject();
    EXPECT_EQ(trap.value("name").toString(), QString("Fx1ChorusSpeed"));
    EXPECT_EQ(trap.value("trapReason").toString(), QString("bit-alias"));
    EXPECT_TRUE(trap.contains("note"));
}

// G4c: limit truncates but `matched` still reports the full hit count.
TEST_F(DeviceParamsTest, LimitTruncates)
{
    const auto o = callJson("list_device_params", { { "engine", "fixture" }, { "limit", 2 } });
    ASSERT_FALSE(o.isEmpty());
    EXPECT_EQ(o.value("matched").toInt(), 4);
    EXPECT_EQ(o.value("returned").toInt(), 2);
    EXPECT_TRUE(o.value("truncated").toBool());
}

// G4d: a filter that matches nothing yields a hint, not a misleading empty map.
TEST_F(DeviceParamsTest, NoMatchHint)
{
    const auto o = callJson("list_device_params",
                            { { "engine", "fixture" }, { "intent", "no-such-intent" } });
    ASSERT_FALSE(o.isEmpty());
    EXPECT_EQ(o.value("matched").toInt(), 0);
    EXPECT_TRUE(o.value("hint").toString().contains("filters"));
}

// G4e: unknown / malformed / wrong-schema engines error loudly.
TEST_F(DeviceParamsTest, ErrorPaths)
{
    const QJsonObject unknown { { "engine", "nope" } };
    const auto ru = call("list_device_params", unknown);
    EXPECT_TRUE(isError(ru));
    EXPECT_TRUE(callText("list_device_params", unknown).contains("fixture"));

    const QJsonObject badId { { "engine", "Bad-Id" } };
    EXPECT_TRUE(isError(call("list_device_params", badId)));
    EXPECT_TRUE(callText("list_device_params", badId).contains("engine must match"));

    const QJsonObject badSchema { { "engine", "badschema" } };
    EXPECT_TRUE(isError(call("list_device_params", badSchema)));
    EXPECT_TRUE(callText("list_device_params", badSchema).contains("unsupported schema"));
}

// ---------------------------------------------------------------------------
// Internal-engine source-table maps (generated from the static C++ def
// tables; docs/plans/2026-09-21-device-param-map.md slice 2, 2026-09-30).
// These run against the REAL committed corpus in timbre-lib/device_map —
// the same artifact the generator's --check gate pins.
// ---------------------------------------------------------------------------

class DeviceParamsInternalTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Walk up from cwd (and the exe dir) until timbre-lib/device_map is
        // found — the tests may run from any build subdirectory.
        QStringList roots { QDir::currentPath() };
        roots << QCoreApplication::applicationDirPath();
        QString found;
        for (const QString& root : roots)
        {
            QDir dir(root);
            for (int i = 0; i < 8 && dir.exists(); ++i)
            {
                const QString candidate = dir.filePath("timbre-lib/device_map");
                if (QFile::exists(candidate + "/intents.json")) { found = candidate; break; }
                if (!dir.cdUp()) break;
            }
            if (!found.isEmpty()) break;
        }
        ASSERT_FALSE(found.isEmpty())
            << "real corpus not found: timbre-lib/device_map (walked up from "
            << QDir::currentPath().toStdString() << ")";
        qputenv("HDAW_DEVICE_MAP_DIR", QDir::cleanPath(found).toUtf8());
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        server = std::make_unique<mcp::McpServer>();
        server->setEngine(engine.get());
        mcp::registerAllTools(*server);
        loopback = std::make_unique<mcp::TransportLoopback>();
        server->setTransport(loopback.get());
        server->start();
    }

    void TearDown() override
    {
        server->stop();
        server->setTransport(nullptr);
        loopback.reset();
        server.reset();
        engine.reset();
        qunsetenv("HDAW_DEVICE_MAP_DIR");
    }

    QJsonObject callJson(const char* method, const QJsonObject& args = {})
    {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = 1;
        req["method"] = "tools/call";
        req["params"] = QJsonObject{ { "name", method }, { "arguments", args } };
        loopback->drainOutgoing();
        loopback->pumpIncoming(QJsonDocument(req).toJson(QJsonDocument::Compact));
        QByteArray out;
        if (!loopback->waitForOutgoing(500, &out)) return {};
        const int nl = out.indexOf('\n');
        const auto r = QJsonDocument::fromJson(out.left(nl < 0 ? out.size() : nl)).object()
                           .value("result").toObject();
        const auto content = r.value("content").toArray();
        if (content.isEmpty()) return {};
        return QJsonDocument::fromJson(
            content[0].toObject().value("text").toString().toUtf8()).object();
    }

    static QJsonObject findParam(const QJsonObject& map, const QString& name)
    {
        for (const auto& pv : map.value("params").toArray())
            if (pv.toObject().value("name").toString() == name)
                return pv.toObject();
        return {};
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;
    std::unique_ptr<mcp::TransportLoopback> loopback;
};

// Index mode lists all 16 internal engines alongside the 5 VA ones.
TEST_F(DeviceParamsInternalTest, IndexModeListsInternalEngines)
{
    const auto o = callJson("list_device_params");
    ASSERT_FALSE(o.isEmpty());
    QStringList engines;
    for (const auto& e : o.value("engines").toArray())
        engines << e.toObject().value("engine").toString();
    for (const char* id : { "eq", "compressor", "reverb", "delay", "chorus",
                            "flanger", "phaser", "filter", "saturator",
                            "sampler", "fm_synth", "growl_bass", "psyarp",
                            "psy_fm", "sub_synth", "drum_synth",
                            "reese_bass" })
        EXPECT_TRUE(engines.contains(QString::fromLatin1(id))) << id;
}

// fm_synth: table-served surface with movement intents on the FM core params.
TEST_F(DeviceParamsInternalTest, FmSynthServesSourceTable)
{
    const auto o = callJson("list_device_params", { { "engine", "fm_synth" } });
    ASSERT_FALSE(o.isEmpty());
    EXPECT_EQ(o.value("engine").toString(), QString("fm_synth"));
    EXPECT_EQ(o.value("appliesVia").toString(), QString("set_internal_fx_param"));
    EXPECT_EQ(o.value("matched").toInt(), 26);

    const auto alg = findParam(o, "Algorithm");
    EXPECT_FALSE(alg.isEmpty());
    EXPECT_EQ(alg.value("tier").toString(), QString("movement"));
    const auto algIntents = alg.value("intents").toArray();
    bool hasFmMetal = false;
    for (const auto& v : algIntents)
        if (v.toString() == "fm-metal") hasFmMetal = true;
    EXPECT_TRUE(hasFmMetal);
    EXPECT_TRUE(alg.value("source").toString().startsWith("src/engine/TrackFXSlot.h:"));
    EXPECT_DOUBLE_EQ(alg.value("max").toDouble(), 31.0);

    const auto fb = findParam(o, "Feedback");
    EXPECT_FALSE(fb.isEmpty());
    EXPECT_EQ(fb.value("tier").toString(), QString("movement"));
    EXPECT_DOUBLE_EQ(fb.value("default").toDouble(), 5.0);
}

// sub_synth: the Cutoff range must come verbatim from the def table.
TEST_F(DeviceParamsInternalTest, SubSynthCutoffRangeFromTable)
{
    const auto o = callJson("list_device_params", { { "engine", "sub_synth" } });
    ASSERT_FALSE(o.isEmpty());
    const auto cutoff = findParam(o, "Cutoff");
    EXPECT_FALSE(cutoff.isEmpty());
    EXPECT_EQ(cutoff.value("category").toString(), QString("sub-synth"));
    EXPECT_EQ(cutoff.value("tier").toString(), QString("movement"));
    EXPECT_DOUBLE_EQ(cutoff.value("default").toDouble(), 1800.0);
    EXPECT_DOUBLE_EQ(cutoff.value("min").toDouble(), 20.0);
    EXPECT_DOUBLE_EQ(cutoff.value("max").toDouble(), 20000.0);
    EXPECT_TRUE(cutoff.value("source").toString().contains("TrackFXSlot.h:"));
}

// reese_bass: the 40-row map comes straight from ReeseBassEngine::paramDefs()
// (its `source` citations point at the header), with default/min/max projected
// verbatim and the units/lengths matching the engine contract.
TEST_F(DeviceParamsInternalTest, ReeseBassMapMatchesEngineTable)
{
    const auto o = callJson("list_device_params", { { "engine", "reese_bass" } });
    ASSERT_FALSE(o.isEmpty());
    EXPECT_EQ(o.value("matched").toInt(), 40);
    EXPECT_EQ(o.value("params").toArray().size(), 40);

    const auto cutoff = findParam(o, "Filter Cutoff");
    ASSERT_FALSE(cutoff.isEmpty());
    EXPECT_EQ(cutoff.value("category").toString(), QString("reese-bass"));
    EXPECT_EQ(cutoff.value("unit").toString(), QString("hz"));
    EXPECT_EQ(cutoff.value("index").toInt(), 15);
    EXPECT_DOUBLE_EQ(cutoff.value("default").toDouble(), 1200.0);
    EXPECT_DOUBLE_EQ(cutoff.value("min").toDouble(), 20.0);
    EXPECT_DOUBLE_EQ(cutoff.value("max").toDouble(), 20000.0);
    EXPECT_TRUE(cutoff.value("source").toString().contains("ReeseBassEngine.h:"));

    // The tempo-synced wobble rate declares `beats` (not Hz) — the reese/psy
    // wobble is a project-tempo division.
    const auto lfoRate = findParam(o, "LFO Rate (beats)");
    ASSERT_FALSE(lfoRate.isEmpty());
    EXPECT_EQ(lfoRate.value("unit").toString(), QString("beats"));
    EXPECT_EQ(lfoRate.value("index").toInt(), 30);

    // Osc Shape carries the enum documentation from the engine header.
    const auto shape = findParam(o, "Osc Shape");
    ASSERT_FALSE(shape.isEmpty());
    EXPECT_EQ(shape.value("enum").toObject().value("0").toString(), QString("Saw"));
    EXPECT_EQ(shape.value("enum").toObject().value("2").toString(), QString("Triangle"));
}

// delay: Division carries the enum documentation from InternalDelay.
TEST_F(DeviceParamsInternalTest, DelayDivisionCarriesEnumDoc)
{
    const auto o = callJson("list_device_params", { { "engine", "delay" } });
    ASSERT_FALSE(o.isEmpty());
    EXPECT_EQ(o.value("matched").toInt(), 6);
    const auto division = findParam(o, "Division");
    EXPECT_FALSE(division.isEmpty());
    const auto en = division.value("enum").toObject();
    EXPECT_EQ(en.value("0").toString(), QString("1/8"));
    EXPECT_EQ(en.value("5").toString(), QString("dotted-1/16"));
    EXPECT_EQ(en.value("6").toString(), QString("1/4"));
    EXPECT_TRUE(division.value("source").toString().contains("InternalDelay.h:"));

    // Feedback: a delay's feedback is delay-throw movement (NOT the generic
    // grammar's `riser` mis-fire), with the runaway-guard max (kMaxFeedback)
    // resolved from the header and the full table row projected.
    const auto fb = findParam(o, "Feedback");
    EXPECT_FALSE(fb.isEmpty());
    const auto fbIntents = fb.value("intents").toArray();
    bool fbDelayThrow = false;
    for (const auto& v : fbIntents)
        if (v.toString() == "delay-throw") fbDelayThrow = true;
    EXPECT_TRUE(fbDelayThrow) << "delay Feedback must carry delay-throw";
    EXPECT_DOUBLE_EQ(fb.value("default").toDouble(), 0.3);
    EXPECT_DOUBLE_EQ(fb.value("min").toDouble(), 0.0);
    EXPECT_DOUBLE_EQ(fb.value("max").toDouble(), 0.99);
    EXPECT_EQ(fb.value("index").toInt(), 1);
}

// Slice B: every internal-engine param carries a declared `unit` drawn from
// the closed unitVocabulary. The SAME conceptual parameter has DIFFERENT units
// across engines (Attack is seconds on sub_synth, Decay is scalar on
// drum_synth, Feedback is scalar on psy_fm) — that contrast is the point.
TEST_F(DeviceParamsInternalTest, InternalParamsDeclareVocabularyUnit)
{
    const QStringList vocab {
        "scalar", "boolean", "enum", "seconds", "ms", "hz",
        "cents", "semitones", "db", "ratio", "beats" };
    for (const char* id : { "eq", "compressor", "reverb", "delay", "chorus",
                            "flanger", "phaser", "filter", "saturator",
                            "sampler", "fm_synth", "growl_bass", "psyarp",
                            "psy_fm", "sub_synth", "drum_synth",
                            "reese_bass" })
    {
        const auto o = callJson("list_device_params",
                                { { "engine", QString::fromLatin1(id) } });
        ASSERT_FALSE(o.isEmpty()) << id;
        const auto params = o.value("params").toArray();
        ASSERT_FALSE(params.isEmpty()) << id;
        for (const auto& pv : params)
        {
            const auto p = pv.toObject();
            const QString unit = p.value("unit").toString();
            EXPECT_FALSE(unit.isEmpty())
                << id << " " << p.value("name").toString().toStdString()
                << " has no unit";
            EXPECT_TRUE(vocab.contains(unit))
                << id << " " << p.value("name").toString().toStdString()
                << " unit '" << unit.toStdString() << "' not in vocabulary";
        }
    }
}

// Unit contrasts across engines: seconds vs scalar vs scalar for the same
// conceptual stage, plus a ratio-not-hz oscillator ratio.
TEST_F(DeviceParamsInternalTest, UnitContrastsAcrossEngines)
{
    const auto sub = callJson("list_device_params", { { "engine", "sub_synth" } });
    ASSERT_FALSE(sub.isEmpty());
    EXPECT_EQ(findParam(sub, "Attack").value("unit").toString(), QString("seconds"));
    EXPECT_EQ(findParam(sub, "Sustain").value("unit").toString(), QString("scalar"));
    EXPECT_EQ(findParam(sub, "Portamento").value("unit").toString(), QString("seconds"));
    EXPECT_EQ(findParam(sub, "Cutoff").value("unit").toString(), QString("hz"));
    EXPECT_EQ(findParam(sub, "Osc2 Detune").value("unit").toString(), QString("cents"));
    EXPECT_EQ(findParam(sub, "LFO Rate").value("unit").toString(), QString("hz"));
    EXPECT_EQ(findParam(sub, "LFO Pitch Amt").value("unit").toString(), QString("scalar"));

    const auto drum = callJson("list_device_params", { { "engine", "drum_synth" } });
    ASSERT_FALSE(drum.isEmpty());
    // 0..1 per-voice decay/tone are NOT seconds — the classic trap.
    EXPECT_EQ(findParam(drum, "Kick Decay").value("unit").toString(), QString("scalar"));
    EXPECT_EQ(findParam(drum, "Kick Tone").value("unit").toString(), QString("scalar"));
    EXPECT_EQ(findParam(drum, "Kit Tune").value("unit").toString(), QString("semitones"));
    EXPECT_EQ(findParam(drum, "Key Track").value("unit").toString(), QString("boolean"));
    EXPECT_EQ(findParam(drum, "Voice").value("unit").toString(), QString("enum"));
    EXPECT_EQ(findParam(drum, "Send Delay Time (beats)").value("unit").toString(),
              QString("beats"));
    EXPECT_EQ(findParam(drum, "Send Reverb Size").value("unit").toString(),
              QString("seconds"));

    const auto pf = callJson("list_device_params", { { "engine", "psy_fm" } });
    ASSERT_FALSE(pf.isEmpty());
    EXPECT_EQ(findParam(pf, "Output Level").value("unit").toString(), QString("scalar"));
    EXPECT_EQ(findParam(pf, "Feedback").value("unit").toString(), QString("scalar"));
    EXPECT_EQ(findParam(pf, "OP1 Ratio").value("unit").toString(), QString("ratio"));
    EXPECT_EQ(findParam(pf, "OP1 Attack").value("unit").toString(), QString("seconds"));
    EXPECT_EQ(findParam(pf, "OP1 Sustain").value("unit").toString(), QString("scalar"));

    // Slice B (2026-10-05): the appended post-carrier filter rows are served
    // with units + intents, and the index/range are the def-table values.
    const auto pfCutoff = findParam(pf, "Filter Cutoff");
    ASSERT_FALSE(pfCutoff.isEmpty()) << "psy_fm must serve Filter Cutoff";
    EXPECT_EQ(pfCutoff.value("unit").toString(), QString("hz"));
    EXPECT_EQ(pfCutoff.value("index").toInt(), 33);
    EXPECT_DOUBLE_EQ(pfCutoff.value("default").toDouble(), 20000.0);
    EXPECT_DOUBLE_EQ(pfCutoff.value("max").toDouble(), 20000.0);
    EXPECT_EQ(findParam(pf, "Filter Resonance").value("unit").toString(), QString("scalar"));
    const auto pfType = findParam(pf, "Filter Type");
    ASSERT_FALSE(pfType.isEmpty());
    EXPECT_EQ(pfType.value("index").toInt(), 35);
    EXPECT_EQ(pfType.value("unit").toString(), QString("enum"));
    EXPECT_EQ(pfType.value("enum").toObject().value("2").toString(), QString("bandpass"));
    EXPECT_EQ(findParam(pf, "Filter Key Track").value("unit").toString(), QString("boolean"));
    EXPECT_EQ(findParam(pf, "Filter Env Amount").value("unit").toString(), QString("scalar"));

    // Same CONCEPT (an amp-envelope stage), DIFFERENT units across engines:
    // sub_synth's Attack is seconds, drum_synth's per-voice Kick Decay is a
    // 0..1 scalar. That difference is exactly what the unit field must expose.
    EXPECT_EQ(findParam(sub, "Attack").value("unit").toString(), QString("seconds"));
    EXPECT_EQ(findParam(drum, "Kick Decay").value("unit").toString(), QString("scalar"));
    EXPECT_NE(findParam(sub, "Attack").value("unit").toString(),
              findParam(drum, "Kick Decay").value("unit").toString());

    // `Feedback` is a 0..1 scalar on delay too — never `db`, despite the
    // substring collision the grammar's `db` rule had to be anchored against.
    const auto dly = callJson("list_device_params", { { "engine", "delay" } });
    ASSERT_FALSE(dly.isEmpty());
    EXPECT_EQ(findParam(dly, "Feedback").value("unit").toString(), QString("scalar"));

    // Compressor timing is MILLISECONDS (Attack 0.1..100, Release 1..2000) —
    // not seconds, and not the same unit as the synth envelope Attacks above.
    const auto comp = callJson("list_device_params", { { "engine", "compressor" } });
    ASSERT_FALSE(comp.isEmpty());
    EXPECT_EQ(findParam(comp, "Attack").value("unit").toString(), QString("ms"));
    EXPECT_EQ(findParam(comp, "Release").value("unit").toString(), QString("ms"));
    EXPECT_EQ(findParam(comp, "Threshold").value("unit").toString(), QString("db"));
    EXPECT_EQ(findParam(comp, "Ratio").value("unit").toString(), QString("ratio"));
}

// VA engines (corpus route) must NOT gain a unit key: their payloads are
// byte-stable and carry no `unit`, even though internal engines now do.
TEST_F(DeviceParamsInternalTest, VaParamsCarryNoUnit)
{
    for (const char* id : { "je8086", "nodalred2x", "xenia", "virus", "vavra" })
    {
        const auto o = callJson("list_device_params",
                                { { "engine", QString::fromLatin1(id) } });
        ASSERT_FALSE(o.isEmpty()) << id;
        const auto params = o.value("params").toArray();
        ASSERT_FALSE(params.isEmpty()) << id;
        for (const auto& pv : params)
        {
            const auto p = pv.toObject();
            EXPECT_FALSE(p.contains("unit"))
                << id << " " << p.value("name").toString().toStdString()
                << " must NOT carry a unit (VA payloads are byte-stable)";
        }
    }
}

// Root metadata on ALL 16 internal maps: they must carry the internal route
// (set_internal_fx_param + valuetree), never the VA plugin routes.
TEST_F(DeviceParamsInternalTest, InternalMapsCarryInternalRoute)
{
    for (const char* id : { "eq", "compressor", "reverb", "delay", "chorus",
                            "flanger", "phaser", "filter", "saturator",
                            "sampler", "fm_synth", "growl_bass", "psyarp",
                            "psy_fm", "sub_synth", "drum_synth",
                            "reese_bass" })
    {
        const auto o = callJson("list_device_params",
                                { { "engine", QString::fromLatin1(id) } });
        ASSERT_FALSE(o.isEmpty()) << id;
        EXPECT_EQ(o.value("appliesVia").toString(),
                  QString("set_internal_fx_param")) << id;
        EXPECT_EQ(o.value("durability").toString(),
                  QString("valuetree")) << id;
    }
}

// The wire projection must NOT be lossy for internal maps: every param keeps
// its table index + default/min/max (feeds a later common-range layer).
TEST_F(DeviceParamsInternalTest, WireProjectionKeepsTableFields)
{
    for (const char* id : { "delay", "sub_synth", "fm_synth", "psyarp" })
    {
        const auto o = callJson("list_device_params",
                                { { "engine", QString::fromLatin1(id) } });
        ASSERT_FALSE(o.isEmpty()) << id;
        for (const auto& pv : o.value("params").toArray())
        {
            const auto p = pv.toObject();
            EXPECT_TRUE(p.contains("index")) << id << " " << p.value("name").toString().toStdString();
            EXPECT_TRUE(p.contains("default")) << id << " " << p.value("name").toString().toStdString();
            EXPECT_TRUE(p.contains("min")) << id << " " << p.value("name").toString().toStdString();
            EXPECT_TRUE(p.contains("max")) << id << " " << p.value("name").toString().toStdString();
            EXPECT_TRUE(p.contains("source")) << id << " " << p.value("name").toString().toStdString();
        }
    }
}

// An unknown engine still refuses with the existing error shape, now listing
// the internal engines too.
TEST_F(DeviceParamsInternalTest, UnknownEngineRefusesListingInternal)
{
    const QJsonObject unknown { { "engine", "nope" } };
    QJsonObject req;
    req["jsonrpc"] = "2.0";
    req["id"] = 1;
    req["method"] = "tools/call";
    req["params"] = QJsonObject{ { "name", "list_device_params" },
                                 { "arguments", unknown } };
    loopback->drainOutgoing();
    loopback->pumpIncoming(QJsonDocument(req).toJson(QJsonDocument::Compact));
    QByteArray out;
    ASSERT_TRUE(loopback->waitForOutgoing(500, &out));
    const int nl = out.indexOf('\n');
    const auto r = QJsonDocument::fromJson(out.left(nl < 0 ? out.size() : nl)).object()
                       .value("result").toObject();
    EXPECT_TRUE(r.value("isError").toBool(false));
    const auto content = r.value("content").toArray();
    ASSERT_FALSE(content.isEmpty());
    const QString text = content[0].toObject().value("text").toString();
    EXPECT_TRUE(text.contains("fm_synth"));
    EXPECT_TRUE(text.contains("sub_synth"));
}

} // namespace
