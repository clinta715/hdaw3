// HDAW Headless Engine — no Qt Widgets, no Qt GUI dependencies.
// Builds as HDAW_headless.exe. Supports two modes:
//   --mcp-stdio    MCP server over stdin/stdout (for Claude Desktop, opencode, etc.)
//   --mcp-http     start loopback MCP HTTP and persist the setting
//   --headless     WebSocket server for the HTML/Electron frontend (default port 8766)
//   --port=N       Override the WebSocket port (only with --headless)
//
// Without either flag, defaults to --headless (WebSocket) mode.
// This executable links only against Qt Core/Network/HttpServer/WebSockets
// and JUCE — no Qt6::Widgets, no QApplication, no windowing system.

#include <QCoreApplication>
#include <QTimer>
#include "engine/AudioEngine.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "mcp/McpTransport.h"
#include "mcp/McpTransportStdio.h"
#include "frontend/FrontendServer.h"
#include "frontend/FrontendRpc.h"
#include "common/DebugLog.h"
#include "common/HeadlessArgs.h"
#include "common/MessagePumpThread.h"
#include "common/ScopedComInit.h"
#include "common/SettingsKeys.h"
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <cstring>
#include <thread>

class HDAW_JuceLogger : public juce::Logger
{
    void logMessage(const juce::String& message) override
    {
        HDAW_LOG("JUCE", message.toRawUTF8());
    }
};

static constexpr quint16 kDefaultFrontendPort = 8766;

static bool parseFlag(int argc, char** argv, const char* name)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], name) == 0)
            return true;
    return false;
}

// NOTE: the old `parseValue` (`--name=value` ONLY) is gone — every value-taking
// argument now goes through HDAW::parseHeadlessArgs (src/common/HeadlessArgs.h),
// which accepts BOTH spellings and refuses a malformed value loudly. The
// `=`-only parser silently ignored `--port 8799` and fell back to the DEFAULT
// port (measured 2026-09-30: the process then collided with the live session
// engine and exited 1 with a bind error that never mentioned the argument).

int main(int argc, char *argv[])
{
    // COM init is host-app responsibility for JUCE 8's WASAPI (see
    // common/ScopedComInit.h). Constructed first so the AudioDeviceManager
    // lifecycle + audio.* RPCs (main Qt event loop) hit COM-initialised paths.
    HDAW::ScopedComInit comInit;
    (void) comInit;

    // MUST be the process' first JUCE use: the pump thread owns the
    // MessageManager queue so AudioProcessorGraph render sequences and
    // AsyncUpdaters can bake (exports render on a worker thread; without a
    // pump, Pimpl::processBlock clears audio -> silent WAV). This process runs
    // a Qt app.exec() loop; nothing else pumps the JUCE queue.
    if (!HDAW::MessagePumpThread::start())
        HDAW_LOG("main_headless", "Warning: JUCE message pump failed to start");

    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    HDAW_JuceLogger juceLogger;
    juce::Logger::setCurrentLogger(&juceLogger);

    QCoreApplication::setOrganizationName("HDAW");
    QCoreApplication::setApplicationName("HDAW");

    const bool mcpStdio = parseFlag(argc, argv, "--mcp-stdio");
    const bool enableMcpHttp = parseFlag(argc, argv, "--mcp-http");
    // Default to headless (WebSocket) mode when no flag is specified
    const bool headlessFrontend = !mcpStdio;

    // Bootstrap args (--project + the listen ports) — ONE parser, both spellings,
    // malformed values are HARD errors. It runs BEFORE any socket binds (including
    // the MCP-HTTP server start below), so a typo cannot silently fall back to a
    // default port and collide with the live session engine (measured 2026-09-30:
    // `--port 8799` was ignored and the process tried 8766).
    const HDAW::HeadlessArgs bootArgs = HDAW::parseHeadlessArgs(argc, argv);
    if (!bootArgs.ok) {
        HDAW_LOG("main_headless", QString("Argument error: %1")
            .arg(QString::fromStdString(bootArgs.error)));
        return 2;
    }

    QString mcpHttpHost = QString::fromUtf8(SettingsKeys::kDefaultMcpHttpHost);
    quint16 mcpHttpPort = SettingsKeys::kDefaultMcpHttpPort;
    if (bootArgs.hasMcpHttpHost)
        mcpHttpHost = QString::fromStdString(bootArgs.mcpHttpHost);
    if (bootArgs.hasMcpHttpPort)
        mcpHttpPort = static_cast<quint16>(bootArgs.mcpHttpPort);

    QCoreApplication::setOrganizationName("HDAW");
    QCoreApplication::setApplicationName("HDAW");
    QCoreApplication app(argc, argv);

    HDAW_LOG("main_headless", QString("Mode: %1").arg(
        mcpStdio ? "MCP STDIO" : "HEADLESS FRONTEND (WebSocket)"));

    // One-shot session bootstrap (docs/plans/2026-09-28-agent-mechanization.md §6):
    // `--project <file>` / `--project=<file>` loads a project right after the
    // deferred engine init. bootArgs was parsed and validated above (before any
    // socket binds); a malformed value is a HARD error — never a silently empty
    // project and never a silently default port.
    if (bootArgs.hasProject && !mcpStdio)
        HDAW_LOG("main_headless", "--project is only honored with --mcp-stdio; ignoring it");

    if (mcpStdio) {
        AudioEngine engine;
        mcp::McpServer server;
        server.setEngine(&engine);
        server.setTransportName("stdio");
        mcp::registerAllTools(server);
        auto transport = std::make_unique<mcp::TransportStdio>();
        server.setTransport(transport.get());
        QObject::connect(&app, &QCoreApplication::aboutToQuit, [&] {
            server.stop();
        });
        server.start();

        // Defer engine init + plugin scan so MCP can respond to initialize/tools/list
        // immediately. Tools that need the engine will return errors until it's ready.
        const QString projectToLoad = QString::fromStdString(bootArgs.projectPath);
        QTimer::singleShot(0, [&engine, projectToLoad] {
            engine.initialize();
            if (engine.getPluginManager().getPlugins().empty())
            {
                HDAW_LOG("main_headless", "Plugin cache empty; scanning...");
                engine.getPluginManager().scanAll();
                HDAW_LOG("main_headless", QString("Scan complete: %1 plugins").arg(
                    (int)engine.getPluginManager().getPlugins().size()));
            }

            // --project bootstrap: load AFTER initialize() and AFTER the plugin
            // scan (plugin state restore needs the scanned plugins). A failed
            // load exits 2 so the caller sees a NON-ZERO status instead of a
            // silently empty project.
            if (!projectToLoad.isEmpty()) {
                const bool loaded = engine.getProjectCommands().loadProject(
                    projectToLoad.toStdString());
                if (!loaded) {
                    HDAW_LOG("main_headless", QString("--project: FAILED to load %1")
                        .arg(projectToLoad));
                    QCoreApplication::exit(2);
                    return;
                }
                HDAW_LOG("main_headless", QString("--project: loaded %1").arg(projectToLoad));
            }
        });

        return app.exec();
    }

    // Headless WebSocket mode. `--port 0` (or `--port=0`) is a VALID request for
    // an OS-assigned free port — read it back from server.port() in the log line.
    quint16 port = kDefaultFrontendPort;
    if (bootArgs.hasFrontendPort)
        port = static_cast<quint16>(bootArgs.frontendPort);

    AudioEngine engine;

    // Bind the WebSocket port BEFORE engine.initialize() so the Electron
    // waitForPort() succeeds immediately. Audio device init can take 10+ seconds.
    frontend::FrontendServer server(engine);
    if (!server.start(port)) {
        HDAW_LOG("main_headless", QString("FrontendServer failed to bind port %1").arg(port));
        return 1;
    }
    HDAW_LOG("main_headless", QString("FrontendServer listening on ws://127.0.0.1:%1").arg(server.port()));

    engine.initialize();
    engine.getPluginManager().loadCache();

    if (enableMcpHttp) {
        // The command line is authoritative for MCP HTTP. Do NOT round-trip the CLI
        // host/port through QSettings: a settings write made before the engine starts
        // has proven unreliable here (measured 2026-10-01 — `--mcp-http-port 18861`
        // still bound the persisted 18765 and the registry value never changed), and a
        // silently-ignored port is how a mute engine ends up masquerading as healthy.
        // setMcpHttpConfig stops any settings-derived server and starts on these values.
        QString mcpErr;
        const bool started = engine.setMcpHttpConfig(true, mcpHttpHost, mcpHttpPort, &mcpErr);
        const auto cfg = engine.getMcpHttpConfig();
        if (!started || !cfg.running || cfg.port != mcpHttpPort) {
            HDAW_LOG("main_headless", QString("MCP HTTP NOT serving on the requested port %1 (started=%2 running=%3 port=%4 error=%5) — exiting")
                .arg(mcpHttpPort).arg(started).arg(cfg.running).arg(cfg.port).arg(mcpErr.isEmpty() ? cfg.lastError : mcpErr));
            return 1;
        }
        HDAW_LOG("main_headless", QString("MCP HTTP listening on %1:%2").arg(cfg.host).arg(cfg.port));
    }

    // First-launch discovery: if the cache is empty, scan the default VST3/CLAP
    // directories on a background thread. We can't block here (the engine main
    // thread must service RPCs), and we want the Plugin Manager dialog (if the
    // user opens it during the scan) to see live progress, so we route the
    // scan's progress callback through the FrontendServer's cross-thread
    // broadcast. The scan short-circuits already-known and blacklisted files,
    // so on a second launch with a populated cache this whole block is skipped
    // (loadCache above filled the list). The thread is detached; it only
    // touches the PluginManager (which outlives app.exec) and the server (same).
    if (engine.getPluginManager().getPlugins().empty())
    {
        HDAW_LOG("main_headless", "Plugin cache empty; scanning on background thread...");
        std::thread startupScan([&engine, &server]() {
            engine.getPluginService().scanAll(
                [&server](const std::string& fileName, int completed, int total) {
                    QJsonObject payload{
                        { "fileName", QString::fromStdString(fileName) },
                        { "completed", completed },
                        { "total", total },
                    };
                    server.broadcastNotificationFromAnyThread(
                        frontend::notify::ScanProgress, payload);
                });
            // Broadcast completion so any open Plugin Manager dialog
            // re-fetches the (now populated) plugin list.
            server.broadcastNotificationFromAnyThread(
                frontend::notify::ScanProgress,
                QJsonObject{ { "fileName", "" }, { "completed", -1 },
                             { "total", -1 }, { "done", true } });
            HDAW_LOG("main_headless", QString("Startup scan complete: %1 plugins")
                .arg(static_cast<int>(engine.getPluginManager().getPlugins().size())));
        });
        startupScan.detach();
    }

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&] {
        server.stop();
    });
    return app.exec();
}
