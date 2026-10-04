// Slice A + C (2026-10-02) — the JSON-RPC half of the internal-FX param
// standardization; the MCP twin lives in
// tests/integration/mcp/internal_fx_intent_test.cpp.
//
//   A) read.getInternalFxParams (FrontendRpc.h's toJson) publishes
//      `valueNormalized` + `defaultNormalized` alongside the real-unit fields.
//   C) project.setFxSlotParam accepts `intent` when `paramIndex` is absent,
//      resolved through the SAME shared HDAW::resolveInternalFxIntent the MCP
//      setters call — so the success write, the refusal text and the "nothing
//      was written" guarantee are identical on both surfaces BY CONSTRUCTION.
//
// Harness: the written-fixture-corpus pattern of
// tests/unit/frontend/device_params_rpc_test.cpp (fixture maps in a temp dir,
// HDAW_DEVICE_MAP_DIR points at it) + the frontend::dispatch twin harness of
// missing_route_parity_test.cpp.
//
// Adversarial: the C cases are RED on the pre-slice tree (the route required
// `paramIndex` -> "trackIndex, slotIndex, paramIndex, value required").

#include <gtest/gtest.h>

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

#include "engine/AudioEngine.h"
#include "frontend/FrontendRouter.h"
#include "model/ProjectModel.h"

#include <memory>

namespace {

class InternalFxIntentRpcTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(temp_.isValid());
        ASSERT_TRUE(writeMaps());
        qputenv("HDAW_DEVICE_MAP_DIR", temp_.path().toUtf8());
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        ASSERT_GE(engine->getProjectCommands().addTrack("T"), 0);
        engine->getProjectCommands().addFxSlot(0, std::string("eq"), -1, "");
        engine->drainPendingRoutingRebuild();
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

    static QJsonObject eqEntry(const char* name, int index, const QJsonArray& intents,
                               double minV, double maxV, double def) {
        return QJsonObject{
            { "name", name }, { "category", "eq" }, { "tier", "movement" },
            { "intents", intents }, { "stages", QJsonArray{ "fx-automation-engineer" } },
            { "index", index }, { "offset", QJsonValue::Null },
            { "trapReason", QJsonValue::Null }, { "note", QJsonValue::Null },
            { "min", minV }, { "max", maxV }, { "default", def } };
    }

    // Identical fixture to the MCP twin: filter-sweep on TWO params (ambiguity),
    // eq-shape on exactly one (success).
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

    frontend::DispatchResult rpc(const QString& method, const QJsonObject& args) {
        return frontend::dispatch(*engine, method, args);
    }
    QString failureText(const QString& method, const QJsonObject& args) {
        const auto r = rpc(method, args);
        EXPECT_TRUE(r.isError) << method.toStdString() << " was expected to fail";
        EXPECT_EQ(r.payload.toObject().value("code").toInt(), -32602);
        return r.payload.toObject().value("message").toString();
    }

    juce::ValueTree slotNode(int trackIndex, int slotIndex) {
        return engine->getProjectModel().getTrackListTree()
            .getChild(trackIndex).getChildWithName(IDs::FX_CHAIN).getChild(slotIndex);
    }
    static QJsonObject rowFor(const QJsonArray& rows, int index) {
        for (const auto& r : rows)
            if (r.toObject().value("paramIndex").toInt(-1) == index)
                return r.toObject();
        return {};
    }

    QTemporaryDir temp_;
    std::unique_ptr<AudioEngine> engine;
};

// ─── A. read.getInternalFxParams ───────────────────────────────────────────

// Both new keys are present on every row and carry the 0..1 projection.
TEST_F(InternalFxIntentRpcTest, ReadInternalFxParamsPublishesNormalized)
{
    const auto r = rpc("read.getInternalFxParams",
                       QJsonObject{ { "trackIndex", 0 }, { "slotIndex", 0 } });
    ASSERT_FALSE(r.isError) << r.payload.toObject().value("message").toString().toStdString();
    const QJsonArray params = r.payload.toArray();
    ASSERT_EQ(params.size(), 3);

    for (const auto& p : params)
    {
        const auto row = p.toObject();
        EXPECT_TRUE(row.contains("valueNormalized"));
        EXPECT_TRUE(row.contains("defaultNormalized"));
        // The real-unit contract is untouched.
        EXPECT_TRUE(row.contains("value"));
        EXPECT_TRUE(row.contains("minValue"));
        EXPECT_TRUE(row.contains("maxValue"));
        EXPECT_TRUE(row.contains("defaultValue"));
    }

    const auto freq = rowFor(params, 0);
    EXPECT_DOUBLE_EQ(freq.value("value").toDouble(), 1000.0);
    EXPECT_NEAR(freq.value("valueNormalized").toDouble(), 980.0 / 19980.0, 1e-6);
    EXPECT_NEAR(freq.value("defaultNormalized").toDouble(), 980.0 / 19980.0, 1e-6);

    const auto gain = rowFor(params, 2);
    EXPECT_DOUBLE_EQ(gain.value("valueNormalized").toDouble(), 0.5)
        << "Gain's -24..24 range projects its 0 default to the midpoint";
}

// The projection follows a real-unit write through the route.
TEST_F(InternalFxIntentRpcTest, NormalizedFollowsTheRouteWrite)
{
    ASSERT_FALSE(rpc("project.setFxSlotParam",
                     QJsonObject{ { "trackIndex", 0 }, { "slotIndex", 0 },
                                  { "paramIndex", 0 }, { "value", 20.0 } }).isError);
    auto r = rpc("read.getInternalFxParams",
                 QJsonObject{ { "trackIndex", 0 }, { "slotIndex", 0 } });
    EXPECT_DOUBLE_EQ(rowFor(r.payload.toArray(), 0).value("valueNormalized").toDouble(), 0.0);

    ASSERT_FALSE(rpc("project.setFxSlotParam",
                     QJsonObject{ { "trackIndex", 0 }, { "slotIndex", 0 },
                                  { "paramIndex", 0 }, { "value", 20000.0 } }).isError);
    r = rpc("read.getInternalFxParams",
            QJsonObject{ { "trackIndex", 0 }, { "slotIndex", 0 } });
    const auto row = rowFor(r.payload.toArray(), 0);
    EXPECT_DOUBLE_EQ(row.value("valueNormalized").toDouble(), 1.0);
    EXPECT_NEAR(row.value("defaultNormalized").toDouble(), 980.0 / 19980.0, 1e-6);
}

// ─── C. project.setFxSlotParam {intent} ────────────────────────────────────

// A unique intent resolves and writes exactly that param, in real units.
TEST_F(InternalFxIntentRpcTest, UniqueIntentWritesThatParam)
{
    const auto r = rpc("project.setFxSlotParam",
                       QJsonObject{ { "trackIndex", 0 }, { "slotIndex", 0 },
                                    { "intent", "eq-shape" }, { "value", 5.5 } });
    ASSERT_FALSE(r.isError) << r.payload.toObject().value("message").toString().toStdString();
    const auto slot = slotNode(0, 0);
    EXPECT_DOUBLE_EQ(static_cast<double>(slot.getProperty("param_1", -999.0)), 5.5);
    EXPECT_FALSE(slot.hasProperty("param_0"));
    EXPECT_FALSE(slot.hasProperty("param_2"));
}

// Ambiguous intent: the SAME text the MCP twin asserts, byte for byte, and no
// write on either surface.
TEST_F(InternalFxIntentRpcTest, AmbiguousIntentRefusedWithCandidates)
{
    const auto slot = slotNode(0, 0);
    const int before = slot.getNumProperties();
    const QJsonObject args{ { "trackIndex", 0 }, { "slotIndex", 0 },
                            { "intent", "filter-sweep" }, { "value", 1.0 } };
    const QString text = failureText("project.setFxSlotParam", args);
    EXPECT_EQ(text,
              QString("ambiguous intent 'filter-sweep' on eq: 2 params declare it "
                      "(0 Frequency, 2 Gain); pass paramIndex or paramName"))
        << text.toStdString();
    EXPECT_EQ(slot.getNumProperties(), before);
}

// Unknown intent: the engine's vocabulary travels back.
TEST_F(InternalFxIntentRpcTest, UnknownIntentRefusedWithVocabulary)
{
    const auto slot = slotNode(0, 0);
    const int before = slot.getNumProperties();
    const QJsonObject args{ { "trackIndex", 0 }, { "slotIndex", 0 },
                            { "intent", "no-such-intent" }, { "value", 1.0 } };
    const QString text = failureText("project.setFxSlotParam", args);
    EXPECT_EQ(text,
              QString("unknown intent 'no-such-intent' on eq: no param declares it "
                      "(available intents: eq-shape, filter-sweep)"))
        << text.toStdString();
    EXPECT_EQ(slot.getNumProperties(), before);
}

// An internal engine with no map file: refused, naming the loader's reason.
TEST_F(InternalFxIntentRpcTest, EngineWithoutMapRefused)
{
    engine->getProjectCommands().addFxSlot(0, std::string("filter"), -1, "");
    engine->drainPendingRoutingRebuild();
    const QJsonObject args{ { "trackIndex", 0 }, { "slotIndex", 1 },
                            { "intent", "filter-sweep" }, { "value", 100.0 } };
    const QString text = failureText("project.setFxSlotParam", args);
    EXPECT_TRUE(text.startsWith("no device map for engine 'filter':")) << text.toStdString();
    EXPECT_TRUE(text.contains("file not found")) << text.toStdString();
    EXPECT_FALSE(slotNode(0, 1).hasProperty("param_0"));
}

// Unresolvable map dir is surfaced verbatim (never read as "unknown intent").
TEST_F(InternalFxIntentRpcTest, UnresolvableMapDirSurfaced)
{
    qputenv("HDAW_DEVICE_MAP_DIR", temp_.filePath("no-such-dir").toUtf8());
    const QJsonObject args{ { "trackIndex", 0 }, { "slotIndex", 0 },
                            { "intent", "eq-shape" }, { "value", 5.0 } };
    const QString text = failureText("project.setFxSlotParam", args);
    EXPECT_TRUE(text.startsWith("device map directory unavailable:")) << text.toStdString();
    EXPECT_TRUE(text.contains("HDAW_DEVICE_MAP_DIR is set but not a usable"))
        << text.toStdString();
    EXPECT_FALSE(slotNode(0, 0).hasProperty("param_1"));
    qputenv("HDAW_DEVICE_MAP_DIR", temp_.path().toUtf8());
}

// A plugin slot + intent: refused with the shared text, nothing written.
TEST_F(InternalFxIntentRpcTest, PluginSlotIntentRefused)
{
    const auto added = rpc("project.addFxSlot",
                           QJsonObject{ { "trackIndex", 0 },
                                        { "pluginId", "C:/definitely/missing/hdaw_intent_rpc.clap" } });
    ASSERT_FALSE(added.isError)
        << added.payload.toObject().value("message").toString().toStdString();
    ASSERT_EQ(slotNode(0, 1).getProperty(IDs::fxType).toString().toStdString(), "plugin");

    const QJsonObject args{ { "trackIndex", 0 }, { "slotIndex", 1 },
                            { "intent", "eq-shape" }, { "value", 0.5 } };
    EXPECT_EQ(failureText("project.setFxSlotParam", args),
              QString("slot is not an internal FX"));
    EXPECT_FALSE(slotNode(0, 1).hasProperty("param_1"));
}

// A slot index that does not exist is still "slot not found" — the intent path
// must not turn it into a resolver error.
TEST_F(InternalFxIntentRpcTest, UnknownSlotRefused)
{
    const QJsonObject args{ { "trackIndex", 0 }, { "slotIndex", 9 },
                            { "intent", "eq-shape" }, { "value", 1.0 } };
    EXPECT_EQ(failureText("project.setFxSlotParam", args), QString("slot not found"));
}

// paramIndex present + intent present: the index wins and no resolution runs
// (the ambiguous intent must NOT fire).
TEST_F(InternalFxIntentRpcTest, ParamIndexWinsOverIntent)
{
    const auto r = rpc("project.setFxSlotParam",
                       QJsonObject{ { "trackIndex", 0 }, { "slotIndex", 0 },
                                    { "paramIndex", 2 }, { "intent", "filter-sweep" },
                                    { "value", -3.0 } });
    ASSERT_FALSE(r.isError) << r.payload.toObject().value("message").toString().toStdString();
    EXPECT_DOUBLE_EQ(static_cast<double>(slotNode(0, 0).getProperty("param_2", -999.0)), -3.0);
}

// With neither paramIndex nor intent the route keeps its old refusal.
TEST_F(InternalFxIntentRpcTest, NeitherIndexNorIntentRefused)
{
    EXPECT_EQ(failureText("project.setFxSlotParam",
                          QJsonObject{ { "trackIndex", 0 }, { "slotIndex", 0 },
                                       { "value", 1.0 } }),
              QString("trackIndex, slotIndex, paramIndex, value required"));
}

} // namespace
