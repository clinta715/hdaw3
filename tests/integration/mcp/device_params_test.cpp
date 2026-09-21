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

} // namespace
