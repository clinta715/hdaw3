// Engine update/restart MCP tools (Gate 8, plan docs/plans/2026-08-31-psyarp-
// vector-assert-plan.md).
//
// engine_info: read-only JSON report (running binary path/mtime/size, export
// status, version; optional stale check vs a freshly built binary).
//
// engine_restart: refuses while an export renders (a long render is never
// silently cancelled). The happy path schedules QCoreApplication::exit(42)
// 300 ms after the response is flushed - that path is intentionally NOT
// exercised here because it would exit the test binary; it is verified live
// by the orchestrator through mcp-launch (exit code 42 propagation +
// binary re-copy + size verify, lesson 21).

#include <gtest/gtest.h>
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "engine/AudioEngine.h"
#include "engine/ExportManager.h"
#include "engine/PluginManager.h"
#include "engine/ProjectPool.h"
#include "model/ProjectModel.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryFile>
#include <QThread>

#include <chrono>

namespace
{
// handleRequestOnTestThread returns the BARE result payload (see the bare-
// result contract note in McpServer.cpp): isError and content sit at the
// top level, not under "result".
bool isError (const QJsonValue& r) { return r.toObject().value("isError").toBool(); }

QString textOf (const QJsonValue& r)
{
    return r.toObject().value("content").toArray().at(0).toObject()
        .value("text").toString();
}

QJsonObject resultObj (const QJsonValue& r)
{
    return QJsonDocument::fromJson (textOf (r).toUtf8()).object();
}
} // namespace

TEST (EngineTools, EngineInfoReturnsParseableJson)
{
    AudioEngine engine;
    mcp::McpServer s;
    s.setEngine (&engine);
    mcp::registerAllTools (s);

    auto r = s.handleRequestOnTestThread (1, "tools/call",
        QJsonObject { { "name", "engine_info" }, { "arguments", QJsonObject {} } });
    ASSERT_FALSE (isError (r));

    const auto o = resultObj (r);
    EXPECT_FALSE (o.value("runningBinaryPath").toString().isEmpty());
    EXPECT_FALSE (o.value("exporting").toBool (true)) << "no export was started";
    EXPECT_GT (o.value("runningMtime").toDouble(), 0.0);
    EXPECT_GT (o.value("runningSize").toDouble(), 0.0);
    EXPECT_FALSE (o.value("version").toString().isEmpty());
    EXPECT_FALSE (o.contains("stale")) << "stale only applies when buildBinaryPath is given";
}

TEST (EngineTools, EngineInfoStaleFlagWhenBuildNewer)
{
    AudioEngine engine;
    mcp::McpServer s;
    s.setEngine (&engine);
    mcp::registerAllTools (s);

    // A file stamped in the near future plays the freshly built binary.
    QTemporaryFile newer;
    ASSERT_TRUE (newer.open());
    newer.write ("x");   // non-zero size so buildSize is checkable
    newer.flush();
    const QString newerPath = newer.fileName();
    ASSERT_TRUE (newer.setFileTime (QDateTime::currentDateTime().addSecs (120),
                                    QFileDevice::FileModificationTime));
    newer.close();

    auto r = s.handleRequestOnTestThread (1, "tools/call",
        QJsonObject { { "name", "engine_info" },
                      { "arguments", QJsonObject { { "buildBinaryPath", newerPath } } } });
    ASSERT_FALSE (isError (r));
    const auto o = resultObj (r);
    EXPECT_TRUE (o.value("stale").toBool (false))
        << "a build binary newer than the running process must flag stale";
    EXPECT_GT (o.value("buildMtime").toDouble(), o.value("runningMtime").toDouble());
    EXPECT_GT (o.value("buildSize").toDouble(), 0.0);

    // Non-existent build path: reported, never guessed.
    auto r2 = s.handleRequestOnTestThread (2, "tools/call",
        QJsonObject { { "name", "engine_info" },
                      { "arguments", QJsonObject { { "buildBinaryPath", "Z:/nope/HDAW_headless.exe" } } } });
    const auto o2 = resultObj (r2);
    EXPECT_FALSE (o2.value("buildExists").toBool (true));
    EXPECT_FALSE (o2.contains("stale"));
}

TEST (EngineTools, EngineRestartRefusesWhileExporting)
{
    AudioEngine engine;
    mcp::McpServer s;
    s.setEngine (&engine);
    mcp::registerAllTools (s);

    ASSERT_NE (engine.getMainProcessor(), nullptr);
    auto& em = engine.getMainProcessor()->getExportManager();

    // Real in-process render, deliberately absurd length (5 stereo minutes)
    // so it cannot finish before the refusal assert; aborted via cancel().
    QTemporaryFile outFile;
    ASSERT_TRUE (outFile.open());
    const QString outPath = outFile.fileName();
    outFile.close();

    juce::ValueTree projectCopy = engine.getProjectModel().getTree().createCopy();
    auto& formatManager = engine.getProjectPool().getFormatManager();
    auto* pluginManager = &engine.getPluginManager();
    const bool started = em.startExport (projectCopy, formatManager, pluginManager,
        juce::File (outPath.toStdString()), 44100.0, 0.0, 300.0,
        HDAW::ExportManager::WAV, 16);
    ASSERT_TRUE (started);
    ASSERT_TRUE (em.isExporting());

    auto r = s.handleRequestOnTestThread (3, "tools/call",
        QJsonObject { { "name", "engine_restart" }, { "arguments", QJsonObject {} } });
    EXPECT_TRUE (isError (r))
        << "restart must refuse (not silently cancel) while a render is live";
    const QString text = textOf (r);
    EXPECT_TRUE (text.contains("export in progress")) << "got: [" << text.toStdString() << "]";
    EXPECT_TRUE (text.contains("cancel_export"));
    EXPECT_TRUE (text.contains("force"));

    // force=true is accepted by design, but its 300 ms exit(42) would end this
    // test binary - that half is verified live by the orchestrator.

    em.cancel();
    // waitForIdle() returns false as soon as the cancel FLAG is set (not only
    // on timeout), so poll isExporting() directly for the drain.
    const auto drainDeadline = std::chrono::steady_clock::now() + std::chrono::seconds (30);
    while (em.isExporting() && std::chrono::steady_clock::now() < drainDeadline)
        QThread::msleep (50);
    EXPECT_FALSE (em.isExporting());

    // engine_info reflects the clean state again.
    auto info = s.handleRequestOnTestThread (4, "tools/call",
        QJsonObject { { "name", "engine_info" }, { "arguments", QJsonObject {} } });
    ASSERT_FALSE (isError (info));
    EXPECT_FALSE (resultObj (info).value("exporting").toBool (true));
}
