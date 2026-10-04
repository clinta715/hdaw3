// Slice A + C of the internal-FX param standardization (2026-10-02) — the MCP
// half of the change; the JSON-RPC twin lives in
// tests/unit/frontend/internal_fx_intent_rpc_test.cpp and asserts the SAME
// payloads/refusals.
//
//   A) Normalized readback: list_fx_params' internal branch and
//      get_internal_fx_param both publish `valueNormalized` +
//      `defaultNormalized` — the SAME real values on the 0..1 axis
//      set_fx_param accepts, computed with the slot's own normalizeParam
//      formula ((v - min) / (max - min), range <= 0 -> 0).
//   C) Intent-based addressing: `intent` is an ALTERNATIVE to
//      paramIndex/paramName on set_internal_fx_param (real units) and
//      set_fx_param (normalized), resolved through the ONE shared
//      HDAW::resolveInternalFxIntent against the EXISTING Device Parameter Map
//      corpus (timbre-lib/device_map/<engine>.params.json). Unique intent ->
//      writes that param; unknown / ambiguous / engine-without-map / plugin
//      slot -> refused with nothing written.
//
// Harness: the TransportLoopback + written-fixture-corpus pattern of
// tests/integration/mcp/device_params_test.cpp (fixture maps in a temp dir,
// HDAW_DEVICE_MAP_DIR points at it), so the suite exercises the resolver's
// resolution and every error path WITHOUT the real generated corpus.
//
// Adversarial: the C cases are RED on the pre-slice tree (`intent` was not a
// declared schema property -> "invalid params: intent: unknown property"; the A
// fields did not exist).

#include <gtest/gtest.h>

#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/ProjectSerializer.h"
#include "engine/TrackFXSlot.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "mcp/McpTransportLoopback.h"
#include "mcp/McpJsonRpc.h"
#include "model/ProjectModel.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
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

class InternalFxIntentTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(temp_.isValid());
        ASSERT_TRUE(writeMaps());
        qputenv("HDAW_DEVICE_MAP_DIR", temp_.path().toUtf8());
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        // One track: slot 0 = eq (Frequency / Q / Gain, the SAME index order as
        // TrackFXSlot::getParamDefsForType("eq") so a resolved intent lands on
        // the param the defs table validates). Fixture intent ids mirror the
        // real corpus grammar (filter-sweep on TWO params — a real eq declares
        // exactly that, which is what makes the ambiguity case honest).
        ASSERT_GE(engine->getProjectCommands().addTrack("T"), 0);
        engine->getProjectCommands().addFxSlot(0, std::string("eq"), -1, "");
        engine->drainPendingRoutingRebuild();
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

    static QJsonObject eqEntry(const char* name, int index, const QJsonArray& intents,
                               double minV, double maxV, double def) {
        return QJsonObject{
            { "name", name }, { "category", "eq" }, { "tier", "movement" },
            { "intents", intents }, { "stages", QJsonArray{ "fx-automation-engineer" } },
            { "index", index }, { "offset", QJsonValue::Null },
            { "trapReason", QJsonValue::Null }, { "note", QJsonValue::Null },
            { "min", minV }, { "max", maxV }, { "default", def } };
    }

    // hdaw.device.param.map.v1 fixture for engine "eq" — the internal fxType
    // string IS the engine id. filter-sweep is declared by TWO params on
    // purpose (the ambiguity refusal); eq-shape by exactly one (the success).
    bool writeMaps() {
        const QJsonArray params{
            eqEntry("Frequency", 0, QJsonArray{ "filter-sweep" }, 20.0, 20000.0, 1000.0),
            eqEntry("Q",         1, QJsonArray{ "eq-shape" },      0.1,    10.0,    0.7),
            eqEntry("Gain",      2, QJsonArray{ "filter-sweep" }, -24.0,   24.0,    0.0) };
        return writeFile(temp_.filePath("eq.params.json"),
                         QJsonDocument(QJsonObject{
                             { "schema", "hdaw.device.param.map.v1" },
                             { "engine", "eq" },
                             { "appliesVia", "set_internal_fx_param" },
                             { "durability", "valuetree" },
                             { "params", params } }).toJson(QJsonDocument::Indented));
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
        return QJsonDocument::fromJson(
            callText(method, args).toUtf8()).object();
    }
    QString callText(const char* method, const QJsonObject& args = {}) {
        const auto content = call(method, args).value("content").toArray();
        return content.isEmpty() ? QString()
                                 : content[0].toObject().value("text").toString();
    }
    bool isError(const QJsonObject& r) { return r.value("isError").toBool(false); }
    bool isError(const char* method, const QJsonObject& args) { return isError(call(method, args)); }

    // The slot's raw ValueTree node: the param_N properties ARE the durable
    // source, so "nothing was written" is provable there.
    juce::ValueTree slotNode(int trackIndex, int slotIndex) {
        return engine->getProjectModel().getTrackListTree()
            .getChild(trackIndex).getChildWithName(IDs::FX_CHAIN).getChild(slotIndex);
    }
    static QJsonObject rowFor(const QJsonArray& rows, int index) {
        for (const auto& r : rows)
            if (r.toObject().value("index").toInt(-1) == index)
                return r.toObject();
        return {};
    }

    QTemporaryDir temp_;
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;
    std::unique_ptr<mcp::TransportLoopback> loopback;
    int nextId_ = 1;
};

// ─── A. normalized readback ────────────────────────────────────────────────

// list_fx_params' internal rows carry valueNormalized/defaultNormalized and
// they are the SAME arithmetic the slot uses for automation/modulation.
TEST_F(InternalFxIntentTest, ListFxParamsPublishesNormalizedValues)
{
    const auto o = callJson("list_fx_params", { { "trackId", 0 }, { "slotIndex", 0 } });
    const QJsonArray params = o.value("params").toArray();
    ASSERT_EQ(params.size(), 3);

    // Every row carries BOTH new keys — never a partial row.
    for (const auto& p : params)
    {
        const auto r = p.toObject();
        EXPECT_TRUE(r.contains("valueNormalized")) << r.value("name").toString().toStdString();
        EXPECT_TRUE(r.contains("defaultNormalized")) << r.value("name").toString().toStdString();
    }

    // Frequency (def 1000 Hz, range 20..20000): (1000-20)/19980.
    const auto freq = rowFor(params, 0);
    ASSERT_FALSE(freq.isEmpty());
    EXPECT_DOUBLE_EQ(freq.value("value").toDouble(), 1000.0);
    EXPECT_NEAR(freq.value("valueNormalized").toDouble(), 980.0 / 19980.0, 1e-6);
    EXPECT_NEAR(freq.value("defaultNormalized").toDouble(), 980.0 / 19980.0, 1e-6);

    // The untouched Q (def 0.7, range 0.1..10): (0.7-0.1)/9.9.
    const auto q = rowFor(params, 1);
    ASSERT_FALSE(q.isEmpty());
    // The engine stores params as float32, so the JSON double is 0.69999998… —
    // compare with float tolerance, not exact double equality.
    EXPECT_NEAR(q.value("value").toDouble(), 0.7, 1e-6);
    EXPECT_NEAR(q.value("valueNormalized").toDouble(), 0.6 / 9.9, 1e-6);
    EXPECT_NEAR(q.value("defaultNormalized").toDouble(), 0.6 / 9.9, 1e-6);

    // Gain's range crosses zero (def 0, -24..24) -> 0.5 — the projection is on
    // the range, not on |value|.
    const auto gain = rowFor(params, 2);
    ASSERT_FALSE(gain.isEmpty());
    EXPECT_DOUBLE_EQ(gain.value("valueNormalized").toDouble(), 0.5);
}

// The real-unit write lands, and the projection follows it to both ends of the
// range (0 at min, 1 at max — no epsilon drift, no off-by-one).
TEST_F(InternalFxIntentTest, NormalizedTracksTheRealWrite)
{
    EXPECT_FALSE(isError("set_internal_fx_param",
        { { "trackId", 0 }, { "slotIndex", 0 }, { "paramIndex", 0 }, { "value", 20.0 } }));
    auto row = rowFor(callJson("list_fx_params",
        { { "trackId", 0 }, { "slotIndex", 0 } }).value("params").toArray(), 0);
    EXPECT_DOUBLE_EQ(row.value("valueNormalized").toDouble(), 0.0);

    EXPECT_FALSE(isError("set_internal_fx_param",
        { { "trackId", 0 }, { "slotIndex", 0 }, { "paramIndex", 0 }, { "value", 20000.0 } }));
    row = rowFor(callJson("list_fx_params",
        { { "trackId", 0 }, { "slotIndex", 0 } }).value("params").toArray(), 0);
    EXPECT_DOUBLE_EQ(row.value("valueNormalized").toDouble(), 1.0);
    EXPECT_NEAR(row.value("defaultNormalized").toDouble(), 980.0 / 19980.0, 1e-6)
        << "the default projection is untouched by a write";
}

// get_internal_fx_param (the second readback surface) publishes the same pair.
TEST_F(InternalFxIntentTest, GetInternalFxParamPublishesNormalizedValues)
{
    ASSERT_FALSE(isError("set_internal_fx_param",
        { { "trackId", 0 }, { "slotIndex", 0 }, { "paramIndex", 1 }, { "value", 5.05 } }));
    const auto o = callJson("get_internal_fx_param", { { "trackId", 0 }, { "slotIndex", 0 } });
    const QJsonArray params = o.value("params").toArray();
    ASSERT_EQ(params.size(), 3);

    const auto q = rowFor(params, 1);
    ASSERT_FALSE(q.isEmpty());
    EXPECT_NEAR(q.value("value").toDouble(), 5.05, 1e-4);
    EXPECT_NEAR(q.value("valueNormalized").toDouble(), (5.05 - 0.1) / 9.9, 1e-5);
    EXPECT_NEAR(q.value("defaultNormalized").toDouble(), 0.6 / 9.9, 1e-5);
    EXPECT_NEAR(params[0].toObject().value("valueNormalized").toDouble(), 980.0 / 19980.0, 1e-6);
}

// ─── C. intent addressing ──────────────────────────────────────────────────

// A unique intent resolves and writes exactly that param (real units).
TEST_F(InternalFxIntentTest, UniqueIntentWritesThatParamInRealUnits)
{
    ASSERT_FALSE(isError("set_internal_fx_param",
        { { "trackId", 0 }, { "slotIndex", 0 }, { "intent", "eq-shape" }, { "value", 5.5 } }));
    const auto slot = slotNode(0, 0);
    EXPECT_DOUBLE_EQ(static_cast<double>(slot.getProperty("param_1", -999.0)), 5.5)
        << "eq-shape is Q (index 1) in the fixture map";
    EXPECT_FALSE(slot.hasProperty("param_0"));
    EXPECT_FALSE(slot.hasProperty("param_2"));
}

// The normalized twin resolves FIRST, then denormalizes: 0.5 on Q's 0.1..10 =
// 5.05 in the ValueTree (the defs-table range, exactly like paramIndex writes).
TEST_F(InternalFxIntentTest, UniqueIntentOnSetFxParamDenormalizes)
{
    ASSERT_FALSE(isError("set_fx_param",
        { { "trackId", 0 }, { "slotIndex", 0 }, { "intent", "eq-shape" }, { "value", 0.5 } }));
    const auto slot = slotNode(0, 0);
    EXPECT_NEAR(static_cast<double>(slot.getProperty("param_1", -999.0)), 5.05, 1e-4);
    EXPECT_FALSE(slot.hasProperty("param_0"));
}

// An intent declared by TWO params is REFUSED with the candidate list — never
// silently picked — and nothing is written.
TEST_F(InternalFxIntentTest, AmbiguousIntentRefusedWithCandidates)
{
    const auto slot = slotNode(0, 0);
    const int before = slot.getNumProperties();
    const QJsonObject args{ { "trackId", 0 }, { "slotIndex", 0 },
                            { "intent", "filter-sweep" }, { "value", 1.0 } };
    EXPECT_TRUE(isError("set_internal_fx_param", args));
    const QString text = callText("set_internal_fx_param", args);
    EXPECT_TRUE(text.contains("ambiguous intent 'filter-sweep' on eq: 2 params declare it"))
        << text.toStdString();
    EXPECT_TRUE(text.contains("0 Frequency")) << text.toStdString();
    EXPECT_TRUE(text.contains("2 Gain")) << text.toStdString();
    EXPECT_TRUE(text.contains("pass paramIndex or paramName")) << text.toStdString();
    EXPECT_EQ(slot.getNumProperties(), before);

    // The normalized twin refuses identically and writes nothing.
    EXPECT_TRUE(isError("set_fx_param", args));
    EXPECT_EQ(callText("set_fx_param", args), text);
    EXPECT_EQ(slot.getNumProperties(), before);
}

// An intent no param declares is refused with the engine's vocabulary, and
// nothing is written.
TEST_F(InternalFxIntentTest, UnknownIntentRefusedWithVocabulary)
{
    const auto slot = slotNode(0, 0);
    const int before = slot.getNumProperties();
    const QJsonObject args{ { "trackId", 0 }, { "slotIndex", 0 },
                            { "intent", "no-such-intent" }, { "value", 1.0 } };
    EXPECT_TRUE(isError("set_internal_fx_param", args));
    const QString text = callText("set_internal_fx_param", args);
    EXPECT_TRUE(text.contains("unknown intent 'no-such-intent' on eq"))
        << text.toStdString();
    EXPECT_TRUE(text.contains("available intents: eq-shape, filter-sweep"))
        << text.toStdString();
    EXPECT_EQ(slot.getNumProperties(), before);
    EXPECT_EQ(callText("set_fx_param", args), text)
        << "the normalized twin reports the identical refusal text";
}

// An internal engine with no map file in the corpus is refused, naming the
// loader's own reason (never a guessed index).
TEST_F(InternalFxIntentTest, EngineWithoutMapRefused)
{
    engine->getProjectCommands().addFxSlot(0, std::string("filter"), -1, "");
    engine->drainPendingRoutingRebuild();
    const QJsonObject args{ { "trackId", 0 }, { "slotIndex", 1 },
                            { "intent", "filter-sweep" }, { "value", 100.0 } };
    EXPECT_TRUE(isError("set_internal_fx_param", args));
    const QString text = callText("set_internal_fx_param", args);
    EXPECT_TRUE(text.contains("no device map for engine 'filter'")) << text.toStdString();
    EXPECT_TRUE(text.contains("file not found")) << text.toStdString();
    EXPECT_FALSE(slotNode(0, 1).hasProperty("param_0"));   // no stray write
}

// An unresolvable map DIR is surfaced as-is — never silently treated as "no
// such intent".
TEST_F(InternalFxIntentTest, UnresolvableMapDirSurfaced)
{
    qputenv("HDAW_DEVICE_MAP_DIR", temp_.filePath("no-such-dir").toUtf8());
    const QJsonObject args{ { "trackId", 0 }, { "slotIndex", 0 },
                            { "intent", "eq-shape" }, { "value", 5.0 } };
    EXPECT_TRUE(isError("set_internal_fx_param", args));
    const QString text = callText("set_internal_fx_param", args);
    EXPECT_TRUE(text.contains("device map directory unavailable")) << text.toStdString();
    EXPECT_TRUE(text.contains("HDAW_DEVICE_MAP_DIR is set but not a usable"))
        << text.toStdString();
    EXPECT_FALSE(slotNode(0, 0).hasProperty("param_1"));
    qputenv("HDAW_DEVICE_MAP_DIR", temp_.path().toUtf8());
}

// A plugin slot has no Device Parameter Map: `intent` is refused with the
// shared plugin text on BOTH setters, and nothing is written.
TEST_F(InternalFxIntentTest, PluginSlotIntentRefused)
{
    const auto added = call("add_fx", { { "trackId", 0 },
        { "pluginId", "C:/definitely/missing/hdaw_intent_probe.clap" } });
    ASSERT_FALSE(isError(added)) << callText("add_fx", { { "trackId", 0 },
        { "pluginId", "C:/definitely/missing/hdaw_intent_probe.clap" } }).toStdString();
    ASSERT_EQ(slotNode(0, 1).getProperty(IDs::fxType).toString().toStdString(), "plugin");

    const QJsonObject args{ { "trackId", 0 }, { "slotIndex", 1 },
                            { "intent", "eq-shape" }, { "value", 0.5 } };
    EXPECT_TRUE(isError("set_internal_fx_param", args));
    EXPECT_EQ(callText("set_internal_fx_param", args),
              QString("slot is not an internal FX"));
    EXPECT_TRUE(isError("set_fx_param", args));
    EXPECT_EQ(callText("set_fx_param", args),
              QString("intent is not supported for a plugin FX slot: pass paramIndex or paramName"));
    EXPECT_FALSE(slotNode(0, 1).hasProperty("param_1"));
}

// Precedence: paramName beats paramIndex (unchanged), and a paramIndex present
// alongside an intent suppresses the intent entirely (so `filter-sweep`'s
// ambiguity never fires).
TEST_F(InternalFxIntentTest, ParamIndexAndNameStillWinOverIntent)
{
    ASSERT_FALSE(isError("set_internal_fx_param",
        { { "trackId", 0 }, { "slotIndex", 0 }, { "paramIndex", 0 },
          { "paramName", "Q" }, { "intent", "filter-sweep" }, { "value", 5.5 } }));
    const auto slot = slotNode(0, 0);
    EXPECT_NEAR(static_cast<double>(slot.getProperty("param_1", -999.0)), 5.5, 1e-4)
        << "paramName wins: Q (1), not Frequency (0) and not the ambiguous intent";
    EXPECT_FALSE(slot.hasProperty("param_0"));

    ASSERT_FALSE(isError("set_internal_fx_param",
        { { "trackId", 0 }, { "slotIndex", 0 }, { "paramIndex", 2 },
          { "intent", "filter-sweep" }, { "value", -3.0 } }));
    EXPECT_DOUBLE_EQ(static_cast<double>(slot.getProperty("param_2", -999.0)), -3.0)
        << "with no paramName the index wins, and no resolution runs";
}

// The three-way "which argument is missing" gate is explicit about `intent`.
TEST_F(InternalFxIntentTest, NeitherIndexNorNameNorIntentRefused)
{
    const QJsonObject args{ { "trackId", 0 }, { "slotIndex", 0 }, { "value", 1.0 } };
    EXPECT_TRUE(isError("set_internal_fx_param", args));
    EXPECT_EQ(callText("set_internal_fx_param", args),
              QString("paramIndex, paramName or intent required"));
    EXPECT_TRUE(isError("set_fx_param", args));
    EXPECT_EQ(callText("set_fx_param", args),
              QString("paramIndex, paramName or intent required"));
}

// The schema declares `intent` on both setters (additionalProperties:false is
// upstream, so an undeclared key would be refused before the handler).
TEST_F(InternalFxIntentTest, IntentIsDeclaredInBothSchemas)
{
    QJsonObject req;
    req["jsonrpc"] = "2.0";
    req["id"] = nextId_++;
    req["method"] = "tools/list";
    loopback->drainOutgoing();
    loopback->pumpIncoming(QJsonDocument(req).toJson(QJsonDocument::Compact));
    QByteArray out;
    ASSERT_TRUE(loopback->waitForOutgoing(500, &out));
    const auto listed = parseOne(out).value("result").toObject();

    int checked = 0;
    for (const auto& t : listed.value("tools").toArray())
    {
        const auto o = t.toObject();
        const QString name = o.value("name").toString();
        if (name != "set_internal_fx_param" && name != "set_fx_param")
            continue;
        ++checked;
        const auto props = o.value("inputSchema").toObject().value("properties").toObject();
        EXPECT_TRUE(props.contains("intent")) << name.toStdString();
        EXPECT_EQ(props.value("intent").toObject().value("type").toString(), QString("string"));
    }
    EXPECT_EQ(checked, 2);
}

// ─── growl_bass `Fundamental Hz` = 0 (follow the MIDI note) ────────────────
//
// The DSP branch `(fundHz > 10) ? fundHz : midiFreq` existed but was
// unreachable: the def table's minimum (20 Hz) sat ABOVE the threshold, so no
// value the param surface accepted could select follow-MIDI. Minimum and
// threshold are now 0. These cases drive the REAL surface end to end
// (set_internal_fx_param -> setFxSlotParam clamp -> FX_SLOT param_N) and prove
// the write is NOT clamped up to the old 20, plus the Gate 1/10 round trip
// (whole-tree save/load -> TrackFXSlot + loadParamsFromTree).

class GrowlFundamentalContractTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        ASSERT_GE(engine->getProjectCommands().addTrack("GrowlFund"), 0);
        engine->getProjectCommands().addFxSlot(0, std::string("growl_bass"), -1, "");
        engine->drainPendingRoutingRebuild();
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
        const auto content = call(method, args).value("content").toArray();
        return content.isEmpty()
                   ? QJsonObject{}
                   : QJsonDocument::fromJson(
                         content[0].toObject().value("text").toString().toUtf8()).object();
    }
    QString callText(const char* method, const QJsonObject& args = {}) {
        const auto content = call(method, args).value("content").toArray();
        return content.isEmpty() ? QString()
                                 : content[0].toObject().value("text").toString();
    }
    bool isError(const char* method, const QJsonObject& args) {
        return call(method, args).value("isError").toBool(false);
    }

    juce::ValueTree growlSlot() {
        return engine->getProjectModel().getTrackListTree()
            .getChild(0).getChildWithName(IDs::FX_CHAIN).getChild(0);
    }
    static QJsonObject rowFor(const QJsonArray& rows, int index) {
        for (const auto& r : rows)
            if (r.toObject().value("index").toInt(-1) == index)
                return r.toObject();
        return {};
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;
    std::unique_ptr<mcp::TransportLoopback> loopback;
    int nextId_ = 1;
};

// set_internal_fx_param {paramName:"Fundamental Hz", value:0} lands UNCLAMPED,
// and both readbacks report 0 on a 0..200 range — not clamped to the old 20.
TEST_F(GrowlFundamentalContractTest, ZeroFundamentalLandsUnclampedThroughRealSurface)
{
    const QJsonObject args{ { "trackId", 0 }, { "slotIndex", 0 },
                            { "paramName", "Fundamental Hz" }, { "value", 0.0 } };
    ASSERT_FALSE(isError("set_internal_fx_param", args))
        << callText("set_internal_fx_param", args).toStdString();

    // The durable source of truth: the FX_SLOT param_0 property. A pre-change
    // clamp would have stored 20.0 here.
    EXPECT_DOUBLE_EQ(static_cast<double>(growlSlot().getProperty(
                         juce::Identifier("param_0"), -999.0)), 0.0)
        << "value 0 must survive the setFxSlotParam clamp (min is now 0)";

    const auto listRow = rowFor(callJson("list_fx_params",
        { { "trackId", 0 }, { "slotIndex", 0 } }).value("params").toArray(), 0);
    ASSERT_FALSE(listRow.isEmpty());
    EXPECT_DOUBLE_EQ(listRow.value("value").toDouble(), 0.0);
    EXPECT_DOUBLE_EQ(listRow.value("minValue").toDouble(), 0.0);
    EXPECT_DOUBLE_EQ(listRow.value("maxValue").toDouble(), 200.0);
    EXPECT_DOUBLE_EQ(listRow.value("defaultValue").toDouble(), 55.0);
    // 0 on 0..200 is the bottom of the normalized axis, and the DEFAULT's
    // projection is 55/200 — both computed from min 0, not min 20.
    EXPECT_DOUBLE_EQ(listRow.value("valueNormalized").toDouble(), 0.0);
    // defaultNormalized is a float32 projection of the default (55/200 =
    // 0.275); compare at float32 precision, not double.
    EXPECT_NEAR(listRow.value("defaultNormalized").toDouble(), 55.0 / 200.0, 1e-6);

    // The second readback surface agrees.
    const auto getRow = rowFor(callJson("get_internal_fx_param",
        { { "trackId", 0 }, { "slotIndex", 0 } }).value("params").toArray(), 0);
    ASSERT_FALSE(getRow.isEmpty());
    EXPECT_DOUBLE_EQ(getRow.value("value").toDouble(), 0.0);
    EXPECT_DOUBLE_EQ(getRow.value("minValue").toDouble(), 0.0);
    EXPECT_DOUBLE_EQ(getRow.value("valueNormalized").toDouble(), 0.0);

    // A positive value still lands verbatim (the additive half).
    ASSERT_FALSE(isError("set_internal_fx_param",
        { { "trackId", 0 }, { "slotIndex", 0 }, { "paramIndex", 0 }, { "value", 88.0 } }));
    EXPECT_DOUBLE_EQ(static_cast<double>(growlSlot().getProperty(
                         juce::Identifier("param_0"), -999.0)), 88.0);
}

// Gate 1/10: 0 must survive the whole-tree save/load AND the rebuild path the
// engine uses (TrackFXSlot("growl_bass") + loadParamsFromTree), and the
// untouched default must still be 55 — bit-identity for existing projects.
TEST_F(GrowlFundamentalContractTest, ZeroFundamentalSurvivesProjectRoundTrip)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const juce::File f(dir.filePath("growl_fund.hdaw").toStdString());

    ASSERT_FALSE(isError("set_internal_fx_param",
        { { "trackId", 0 }, { "slotIndex", 0 }, { "paramName", "Fundamental Hz" },
          { "value", 0.0 } }));

    ASSERT_TRUE(HDAW::ProjectSerializer::save(engine->getProjectModel(), f));
    ASSERT_TRUE(f.existsAsFile());

    ProjectModel loaded;
    ASSERT_TRUE(HDAW::ProjectSerializer::load(loaded, f));
    const auto loadedSlot = loaded.getTrackListTree().getChild(0)
                                .getChildWithName(IDs::FX_CHAIN).getChild(0);
    ASSERT_TRUE(loadedSlot.isValid());
    EXPECT_EQ(loadedSlot.getProperty(IDs::fxType).toString().toStdString(), "growl_bass");
    EXPECT_DOUBLE_EQ(static_cast<double>(
        loadedSlot.getProperty(juce::Identifier("param_0"), -999.0)), 0.0)
        << "the serializer persists param_N — 0 must ride the XML round trip";

    // The rebuild path lands the stored 0 in the engine (NOT 55, NOT 20).
    HDAW::TrackFXSlot rebuilt("growl_bass");
    rebuilt.prepare({ 48000.0, 512, 2 });
    rebuilt.loadParamsFromTree(loadedSlot);
    const auto vals = rebuilt.getInternalParamValues();
    ASSERT_GE(vals.size(), 1u);
    EXPECT_FLOAT_EQ(vals[0], 0.0f);

    // Caller-side clamp agrees with the def table (lesson-23 guard): the new
    // min is 0, so 0 passes through and only a NEGATIVE value clamps.
    EXPECT_FLOAT_EQ(rebuilt.getParamDefsForType("growl_bass")[0].minValue, 0.0f);

    // Untouched default: a slot with NO param_0 property keeps the def default.
    juce::ValueTree bare(IDs::FX_SLOT);
    bare.setProperty(IDs::fxType, juce::String("growl_bass"), nullptr);
    HDAW::TrackFXSlot def("growl_bass");
    def.prepare({ 48000.0, 512, 2 });
    def.loadParamsFromTree(bare);
    EXPECT_FLOAT_EQ(def.getInternalParamValues()[0], 55.0f)
        << "the default must stay 55.0f (bit-identity for existing projects)";
}

} // namespace
