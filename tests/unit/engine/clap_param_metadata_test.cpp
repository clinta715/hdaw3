#include <gtest/gtest.h>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/Track.h"
#include "engine/TrackFXSlot.h"
#include "model/ProjectModel.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "mcp/McpTransportLoopback.h"

// Gates 1-3 for CLAP param metadata (scope B, plan 2026-09-14): isolated +
// in-process paths expose min/max/default/stepped via PluginParamSnapshot;
// list_fx_params surfaces them as JSON; list_plugins filters by kind.
// TyrellN6 CLAP is the subject — param plumbing is format-identical for
// effects. Gated on HDAW_REAL_PLUGIN_TESTS + file existence (clap_program
// precedent); the default path is ISOLATED, in-process via env knob.

namespace {

const char* kTyrellN6Clap = "C:\\Program Files\\Common Files\\CLAP\\u-he\\TyrellN6.clap";

bool clapAvailable()
{
    const char* env = getenv("HDAW_REAL_PLUGIN_TESTS");
    if (env == nullptr)
        return false;
    const juce::String s(env);
    if (s.trim().isEmpty() || s.trim() == "0")
        return false;
    return juce::File(kTyrellN6Clap).existsAsFile();
}

struct SlotRef {
    int trackIndex = -1;
    int slotIndex = 0;
    std::string pluginId;
};

// keepTrack probe leaves a live slot behind; returns its address.
SlotRef createClapSlot(AudioEngine& engine)
{
    SlotRef ref;
    ProjectCommands::AuditionParams p;
    p.pluginId = kTyrellN6Clap;
    p.programIndex = -1;
    p.lengthBeats = 4.0;
    p.windowSeconds = 2.0;
    p.seed = 42;
    p.keepTrack = true;
    auto res = engine.getProjectCommands().auditionPlugin(p);
    EXPECT_TRUE(res.error.empty()) << res.error;
    EXPECT_TRUE(res.ok);
    if (!res.ok)
        return ref;
    ref.trackIndex = res.trackIndex;
    ref.slotIndex = res.slotIndex;
    auto fxSlots = engine.getReadModel().getFxSlots(res.trackIndex);
    if (static_cast<size_t>(res.slotIndex) < fxSlots.size())
        ref.pluginId = fxSlots[static_cast<size_t>(res.slotIndex)].pluginId;
    return ref;
}

void assertRangeConsistency(const std::vector<PluginParamSnapshot>& params)
{
    ASSERT_FALSE(params.empty()) << "plugin exposed zero params";
    int ranged = 0;
    for (const auto& s : params)
    {
        if (!s.hasRange)
            continue;
        ++ranged;
        EXPECT_LT(s.minVal, s.maxVal) << "param " << s.index << " (" << s.name << ")";
        EXPECT_GE(s.defaultVal, s.minVal);
        EXPECT_LE(s.defaultVal, s.maxVal);
        EXPECT_GE(s.plainValue, s.minVal);
        EXPECT_LE(s.plainValue, s.maxVal);
    }
    EXPECT_GT(ranged, 0) << "no ranged params on a CLAP plugin";
}

QJsonObject callTool(mcp::TransportLoopback& tp, int id, const char* name, const char* args)
{
    tp.drainOutgoing();
    QString req = QString(R"({"jsonrpc":"2.0","id":%1,"method":"tools/call",)"
                          R"("params":{"name":"%2","arguments":%3}})")
                      .arg(id).arg(name).arg(args);
    tp.pumpIncoming(req.toUtf8());
    QByteArray out;
    EXPECT_TRUE(tp.waitForOutgoing(5000, &out));
    return QJsonDocument::fromJson(out).object();
}

QString toolText(const QJsonObject& r)
{
    return r.value("result").toObject()
        .value("content").toArray().at(0).toObject()
        .value("text").toString();
}

bool toolIsError(const QJsonObject& r)
{
    return r.value("result").toObject().value("isError").toBool(false);
}

} // namespace

// Gate 1 (isolated path): live child metadata arrives via GET_PARAM_INFO.
TEST(ClapParamMetadata, IsolatedSlotParamsCarryRanges)
{
    if (!clapAvailable())
        GTEST_SKIP() << "HDAW_REAL_PLUGIN_TESTS not set or TyrellN6.clap missing";
    AudioEngine engine;
    engine.initialize();
    SlotRef ref = createClapSlot(engine);
    ASSERT_GE(ref.trackIndex, 0);
    auto params = engine.getPluginParamService().getParams(ref.trackIndex, ref.pluginId);
    assertRangeConsistency(params);
    engine.shutdown();
}

// Gate 1 (tool surface): list_fx_params JSON carries the range fields.
TEST(ClapParamMetadata, ListFxParamsJsonCarriesRanges)
{
    if (!clapAvailable())
        GTEST_SKIP() << "HDAW_REAL_PLUGIN_TESTS not set or TyrellN6.clap missing";
    AudioEngine engine;
    engine.initialize();
    SlotRef ref = createClapSlot(engine);
    ASSERT_GE(ref.trackIndex, 0);

    mcp::TransportLoopback tp;
    mcp::McpServer s;
    s.setEngine(&engine);
    mcp::registerAllTools(s);
    tp.start(&s);
    s.setTransport(&tp);
    s.start();
    const std::string args = "{\"trackId\":" + std::to_string(ref.trackIndex)
        + ",\"slotIndex\":" + std::to_string(ref.slotIndex) + "}";
    auto r = callTool(tp, 1, "list_fx_params", args.c_str());
    EXPECT_FALSE(r.value("error").isObject());
    EXPECT_FALSE(toolIsError(r));
    auto params = QJsonDocument::fromJson(toolText(r).toUtf8()).object().value("params").toArray();
    EXPECT_GT(params.size(), 0);
    int ranged = 0;
    for (const auto& v : params)
    {
        const QJsonObject o = v.toObject();
        ASSERT_TRUE(o.contains("hasRange"));
        if (!o.value("hasRange").toBool())
            continue;
        ++ranged;
        EXPECT_TRUE(o.contains("minVal"));
        EXPECT_TRUE(o.contains("maxVal"));
        EXPECT_TRUE(o.contains("defaultVal"));
        EXPECT_TRUE(o.contains("plainValue"));
        EXPECT_TRUE(o.contains("stepped"));
        EXPECT_LT(o.value("minVal").toDouble(), o.value("maxVal").toDouble());
    }
    EXPECT_GT(ranged, 0) << "no ranged params in list_fx_params output";
    s.stop();
    s.setTransport(nullptr);
    engine.shutdown();
}

// NOTE (follow-up, not this change): loading TyrellN6 IN-PROCESS
// (HDAW_NO_PLUGIN_ISOLATION=1) and running an audition render crashes the
// headless test process (0xC0000005 during plugin startup logging) —
// independent of param metadata (no getParams call precedes the crash,
// and the repo has zero precedent for in-process CLAP tests). Suspect a
// main-thread/init gap on the isolation-off audition path (lesson 16
// family). The in-process CLAPParameter branch is therefore reviewed +
// compiled but not live-executed; the isolated path (the production
// default) is fully covered above on both sides of the pipe.

// Gate 2: list_plugins kind filter (works off the scan cache; the
// invalid-kind error branch is covered even with an empty cache).
TEST(ClapParamMetadata, ListPluginsKindFilter)
{
    AudioEngine engine;
    engine.initialize();
    mcp::TransportLoopback tp;
    mcp::McpServer s;
    s.setEngine(&engine);
    mcp::registerAllTools(s);
    tp.start(&s);
    s.setTransport(&tp);
    s.start();
    auto bad = callTool(tp, 1, "list_plugins", R"({"kind":"synth"})");
    EXPECT_TRUE(toolIsError(bad));
    auto all = QJsonDocument::fromJson(toolText(callTool(tp, 2, "list_plugins", "{}")).toUtf8())
                     .object().value("plugins").toArray();
    auto fx = QJsonDocument::fromJson(toolText(callTool(tp, 3, "list_plugins", R"({"kind":"effect"})")).toUtf8())
                    .object().value("plugins").toArray();
    auto instr = QJsonDocument::fromJson(toolText(callTool(tp, 4, "list_plugins", R"({"kind":"instrument"})")).toUtf8())
                       .object().value("plugins").toArray();
    EXPECT_EQ(all.size(), fx.size() + instr.size());
    for (const auto& v : fx)
        EXPECT_EQ(v.toObject().value("kind").toString().toStdString(), "effect");
    for (const auto& v : instr)
        EXPECT_EQ(v.toObject().value("kind").toString().toStdString(), "instrument");
    s.stop();
    s.setTransport(nullptr);
    engine.shutdown();
}
