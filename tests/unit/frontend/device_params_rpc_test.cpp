// RPC-layer tests for device.listParams — the RPC parity of the MCP tool
// list_device_params (docs/plans/2026-09-21-device-param-map.md, and the
// AGENTS.md "maintain RPC parity" rule).
//
// Fixture maps are written to a temp dir and HDAW_DEVICE_MAP_DIR points at it,
// so the suite exercises dir resolution, index mode, filtering, truncation and
// the error paths WITHOUT the real generated corpus.

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTemporaryDir>
#include <QtGlobal>

#include "engine/AudioEngine.h"
#include "frontend/FrontendRouter.h"

#include <memory>

namespace {

class DeviceParamsRpcTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(temp_.isValid());
        ASSERT_TRUE(writeMaps());
        qputenv("HDAW_DEVICE_MAP_DIR", temp_.path().toUtf8());
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
    }

    void TearDown() override {
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
        return QJsonObject {
            { "name", name }, { "category", category }, { "tier", tier },
            { "intents", intents }, { "stages", stages },
            { "index", QJsonValue::Null }, { "offset", QJsonValue::Null },
            { "trapReason", QJsonValue::Null }, { "note", QJsonValue::Null } };
    }

    bool writeMaps() {
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
                                             { "stage", "fx-automation-engineer" } } } },
                           { "stages", QJsonArray { "fx-automation-engineer", "sound-selector" } },
                           { "excludedStages", QJsonArray { "arranger" } } })
                           .toJson(QJsonDocument::Indented)))
            return false;
        return true;
    }

    QJsonObject call(const QString& method, const QJsonObject& args = {}) {
        auto r = frontend::dispatch(*engine, method, args);
        EXPECT_FALSE(r.isError)
            << "dispatch(" << method.toStdString() << ") errored: "
            << (r.payload.isObject()
                    ? r.payload.toObject().value("message").toString().toStdString()
                    : std::string("non-object error"));
        return r.payload.toObject();
    }

    frontend::DispatchResult callRaw(const QString& method, const QJsonObject& args = {}) {
        return frontend::dispatch(*engine, method, args);
    }

    QTemporaryDir temp_;
    std::unique_ptr<AudioEngine> engine;
};

// RPC parity: the same map the MCP tool serves, with route/durability metadata.
TEST_F(DeviceParamsRpcTest, EngineModeReturnsMap)
{
    const auto o = call("device.listParams", QJsonObject{ { "engine", "fixture" } });
    EXPECT_EQ(o.value("engine").toString(), QString("fixture"));
    EXPECT_EQ(o.value("appliesVia").toString(), QString("set_fx_param"));
    EXPECT_EQ(o.value("durability").toString(), QString("jpar"));
    EXPECT_EQ(o.value("matched").toInt(), 4);
    EXPECT_EQ(o.value("params").toArray().size(), 4);
    EXPECT_FALSE(o.value("truncated").toBool());
}

// Index mode (no engine) returns engines + intent vocabulary + stages.
TEST_F(DeviceParamsRpcTest, IndexModeReturnsVocabulary)
{
    const auto o = call("device.listParams");
    EXPECT_EQ(o.value("schema").toString(), QString("hdaw.device.index.v1"));
    ASSERT_FALSE(o.value("engines").toArray().isEmpty());
    EXPECT_EQ(o.value("engines").toArray()[0].toObject().value("engine").toString(),
              QString("fixture"));
    EXPECT_FALSE(o.value("intents").toArray().isEmpty());
    EXPECT_EQ(o.value("stages").toArray()[0].toString(), QString("fx-automation-engineer"));
}

// Each filter dimension narrows; limit truncates but `matched` reports the full count.
TEST_F(DeviceParamsRpcTest, FiltersAndLimit)
{
    const auto byCategory = call("device.listParams",
                                 { { "engine", "fixture" }, { "category", "filter" } });
    ASSERT_EQ(byCategory.value("matched").toInt(), 1);
    EXPECT_EQ(byCategory.value("params").toArray()[0].toObject().value("name").toString(),
              QString("F1Cutoff"));

    const auto byStage = call("device.listParams",
                              { { "engine", "fixture" }, { "stage", "sound-selector" } });
    ASSERT_EQ(byStage.value("matched").toInt(), 1);
    EXPECT_EQ(byStage.value("params").toArray()[0].toObject().value("name").toString(),
              QString("UniDetune"));

    const auto byTier = call("device.listParams",
                             { { "engine", "fixture" }, { "tier", "trap" } });
    ASSERT_EQ(byTier.value("matched").toInt(), 1);
    EXPECT_EQ(byTier.value("params").toArray()[0].toObject().value("trapReason").toString(),
              QString("bit-alias"));

    const auto limited = call("device.listParams",
                              { { "engine", "fixture" }, { "limit", 2 } });
    EXPECT_EQ(limited.value("matched").toInt(), 4);
    EXPECT_EQ(limited.value("returned").toInt(), 2);
    EXPECT_TRUE(limited.value("truncated").toBool());
}

// Unknown namespace/method and bad engine ids error loudly (RPC error shape).
TEST_F(DeviceParamsRpcTest, ErrorPaths)
{
    const auto unknownMethod = callRaw("device.nope", QJsonObject{});
    EXPECT_TRUE(unknownMethod.isError);
    EXPECT_TRUE(unknownMethod.payload.toObject().value("message").toString()
                    .contains("unknown device method"));

    const auto unknownEngine = callRaw("device.listParams", QJsonObject{ { "engine", "nope" } });
    EXPECT_TRUE(unknownEngine.isError);
    EXPECT_TRUE(unknownEngine.payload.toObject().value("message").toString().contains("fixture"));

    const auto badId = callRaw("device.listParams", QJsonObject{ { "engine", "Bad-Id" } });
    EXPECT_TRUE(badId.isError);
    EXPECT_TRUE(badId.payload.toObject().value("message").toString().contains("engine must match"));
}

} // namespace
