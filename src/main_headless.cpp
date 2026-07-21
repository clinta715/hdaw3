// HDAW Headless Engine — no Qt Widgets, no Qt GUI dependencies.
// Builds as HDAW_headless.exe. Supports two modes:
//   --mcp-stdio    MCP server over stdin/stdout (for Claude Desktop, opencode, etc.)
//   --headless     WebSocket server for the HTML/Electron frontend (default port 8766)
//   --port=N       Override the WebSocket port (only with --headless)
//
// Without either flag, defaults to --headless (WebSocket) mode.
// This executable links only against Qt Core/Network/HttpServer/WebSockets
// and JUCE — no Qt6::Widgets, no QApplication, no windowing system.

#include <QCoreApplication>
#include <QSettings>
#include <QTimer>
#include "engine/AudioEngine.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "mcp/McpTransport.h"
#include "mcp/McpTransportStdio.h"
#include "frontend/FrontendServer.h"
#include "ui/DebugLog.h"
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <cstring>

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

    const bool mcpStdio = parseFlag(argc, argv, "--mcp-stdio");
    // Default to headless (WebSocket) mode when no flag is specified
    const bool headlessFrontend = !mcpStdio;

    QCoreApplication::setOrganizationName("HDAW");
    QCoreApplication::setApplicationName("HDAW");
    QCoreApplication app(argc, argv);

    HDAW_LOG("main_headless", QString("Mode: %1").arg(
        mcpStdio ? "MCP STDIO" : "HEADLESS FRONTEND (WebSocket)"));

    if (mcpStdio) {
        AudioEngine engine;
        engine.initialize();
        if (engine.getPluginManager().getPlugins().empty())
        {
            HDAW_LOG("main_headless", "Plugin cache empty; scanning...");
            engine.getPluginManager().scanAll();
            HDAW_LOG("main_headless", QString("Scan complete: %1 plugins").arg(
                (int)engine.getPluginManager().getPlugins().size()));
        }
        mcp::McpServer server;
        server.setEngine(&engine);
        mcp::registerAllTools(server);
        // McpServer holds a non-owning pointer to the transport, so we keep
        // ownership in a unique_ptr and stop+delete it on quit (mirroring the
        // aboutToQuit hook). Using bare `new` here leaks the transport.
        auto transport = std::make_unique<mcp::TransportStdio>();
        server.setTransport(transport.get());
        QObject::connect(&app, &QCoreApplication::aboutToQuit, [&] {
            server.stop();
        });
        server.start();
        return app.exec();
    }

    // Headless WebSocket mode
    quint16 port = kDefaultFrontendPort;
    if (const char* p = parseValue(argc, argv, "--port")) {
        bool ok = false;
        auto parsed = QString::fromUtf8(p).toUShort(&ok);
        if (ok) port = parsed;
    }

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

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&] {
        server.stop();
    });
    return app.exec();
}
