// HDAW — desktop DAW with HTML frontend served from the engine.
// Usage:
//   HDAW.exe                    (default: engine + browser)
//   HDAW.exe --port=9000        (custom WebSocket port)
//   HDAW.exe --http-port=9001   (custom HTTP port)
//   HDAW.exe --mcp-stdio        (headless MCP over stdin/stdout)
//   HDAW.exe --headless         (headless WebSocket server)
//   HDAW.exe --no-mcp           (default mode without MCP)

#include <QCoreApplication>
#include <QSettings>
#include <QTimer>
#include <cstring>

#include "common/DebugLog.h"
#include "engine/AudioEngine.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "mcp/McpTransport.h"
#include "mcp/McpTransportStdio.h"
#include "frontend/FrontendServer.h"
#include "frontend/FrontendRpc.h"
#include "frontend/UiHttpServer.h"
#include <QDesktopServices>
#include <QUrl>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <thread>

#ifdef _WIN32
#include <io.h>
#endif

// Forward JUCE engine log messages to HDAW_LOG (appears in %TEMP%/hdaw_debug.log)
class HDAW_JuceLogger : public juce::Logger
{
    void logMessage(const juce::String& message) override
    {
        HDAW_LOG("JUCE", message.toRawUTF8());
    }
};

// Default WebSocket port for the HTML frontend.
// Kept distinct from the MCP HTTP port (8765) so the two servers can coexist.
static constexpr quint16 kDefaultFrontendPort = 8766;

static bool parseFlag(int argc, char** argv, const char* name)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], name) == 0)
            return true;
    return false;
}

static const char* parseValue(int argc, char** argv, const char* name)
{
    const QString prefix = QString::fromUtf8(name) + "=";
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromUtf8(argv[i]);
        if (a.startsWith(prefix))
            return argv[i] + prefix.toUtf8().size();
    }
    return nullptr;
}

int main(int argc, char *argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    HDAW_JuceLogger juceLogger;
    juce::Logger::setCurrentLogger(&juceLogger);

    const bool headlessMcp = parseFlag(argc, argv, "--mcp-stdio");
    const bool headlessFrontend = parseFlag(argc, argv, "--headless");
    const bool noMcp = parseFlag(argc, argv, "--no-mcp");

    const char* modeName = "UI (engine + browser)";
    if (headlessMcp) modeName = "HEADLESS MCP (--mcp-stdio)";
    else if (headlessFrontend) modeName = "HEADLESS FRONTEND (--headless)";
    HDAW_LOG("main", QString("Mode: %1").arg(modeName));

    // --- Mode: headless MCP over stdin/stdout ---
    if (headlessMcp) {
        QCoreApplication::setOrganizationName("HDAW");
        QCoreApplication::setApplicationName("HDAW");
        QCoreApplication app(argc, argv);
        AudioEngine engine;
        engine.initialize();
        if (engine.getPluginManager().getPlugins().empty())
        {
            HDAW_LOG("main", "Plugin cache empty; scanning for VST3/CLAP plugins...");
            engine.getPluginManager().scanAll();
            HDAW_LOG("main", QString("Scan complete: %1 plugins found").arg((int)engine.getPluginManager().getPlugins().size()));
        }
        mcp::McpServer server;
        server.setEngine(&engine);
        mcp::registerAllTools(server);
        auto* transport = new mcp::TransportStdio();
        server.setTransport(transport);
        QObject::connect(&app, &QCoreApplication::aboutToQuit, [&] {
            server.stop();
        });
        server.start();
        return app.exec();
    }

    // --- Mode: headless WebSocket server (for external HTML/Electron frontend) ---
    if (headlessFrontend) {
        QCoreApplication::setOrganizationName("HDAW");
        QCoreApplication::setApplicationName("HDAW");
        QCoreApplication app(argc, argv);

        quint16 port = kDefaultFrontendPort;
        if (const char* p = parseValue(argc, argv, "--port")) {
            bool ok = false;
            auto parsed = QString::fromUtf8(p).toUShort(&ok);
            if (ok) port = parsed;
        }

        AudioEngine engine;

        // Bind the WebSocket port BEFORE engine.initialize() so the Electron
        // waitForPort() succeeds immediately. The engine init (audio device
        // enumeration, plugin cache) can take 10+ seconds on some machines.
        // RPC calls only arrive after the frontend window loads, which happens
        // after waitForPort resolves — so the engine is ready by then.
        frontend::FrontendServer server(engine);
        if (!server.start(port)) {
            HDAW_LOG("main", QString("FrontendServer failed to bind port %1").arg(port));
            return 1;
        }
        HDAW_LOG("main", QString("FrontendServer listening on ws://127.0.0.1:%1").arg(server.port()));

        engine.initialize();
        engine.getPluginManager().loadCache();

        // First-launch discovery: if the cache is empty, scan the default
        // VST3/CLAP directories on a background thread. See main_headless.cpp
        // for the full rationale. Routes progress through the FrontendServer
        // so an open Plugin Manager dialog sees live updates.
        if (engine.getPluginManager().getPlugins().empty())
        {
            HDAW_LOG("main", "Plugin cache empty; scanning on background thread...");
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
                server.broadcastNotificationFromAnyThread(
                    frontend::notify::ScanProgress,
                    QJsonObject{ { "fileName", "" }, { "completed", -1 },
                                 { "total", -1 }, { "done", true } });
                HDAW_LOG("main", QString("Startup scan complete: %1 plugins")
                    .arg(static_cast<int>(engine.getPluginManager().getPlugins().size())));
            });
            startupScan.detach();
        }

        QObject::connect(&app, &QCoreApplication::aboutToQuit, [&] {
            server.stop();
        });
        return app.exec();
    }

    // --- Default mode: engine + browser (one executable, no Electron) ---
    {
        QCoreApplication::setOrganizationName("HDAW");
        QCoreApplication::setApplicationName("HDAW");
        QCoreApplication app(argc, argv);

        quint16 wsPort = kDefaultFrontendPort;
        quint16 httpPort = 8765;
        if (const char* p = parseValue(argc, argv, "--port")) {
            bool ok = false;
            auto parsed = QString::fromUtf8(p).toUShort(&ok);
            if (ok) wsPort = parsed;
        }
        if (const char* p = parseValue(argc, argv, "--http-port")) {
            bool ok = false;
            auto parsed = QString::fromUtf8(p).toUShort(&ok);
            if (ok) httpPort = parsed;
        }

        AudioEngine engine;
        engine.initialize();
        engine.getPluginManager().loadCache();

        // Start WebSocket server for frontend RPC.
        frontend::FrontendServer wsServer(engine);
        if (!wsServer.start(wsPort)) {
            HDAW_LOG("main", QString("FrontendServer failed to bind port %1").arg(wsPort));
            return 1;
        }
        HDAW_LOG("main", QString("WebSocket server on ws://127.0.0.1:%1").arg(wsServer.port()));

        // Start HTTP server to serve the bundled HTML frontend.
        frontend::UiHttpServer httpServer(wsPort);
        if (!httpServer.start(httpPort)) {
            HDAW_LOG("main", QString("UiHttpServer failed to bind port %1").arg(httpPort));
            return 1;
        }
        HDAW_LOG("main", QString("HTTP server on http://127.0.0.1:%1").arg(httpServer.port()));

        // Open the system browser.
        QUrl url(QString("http://127.0.0.1:%1").arg(httpServer.port()));
        QDesktopServices::openUrl(url);
        HDAW_LOG("main", QString("Opened browser: %1").arg(url.toString()));

        QObject::connect(&app, &QCoreApplication::aboutToQuit, [&] {
            httpServer.stop();
            wsServer.stop();
            engine.shutdown();
        });

        return app.exec();
    }
}
