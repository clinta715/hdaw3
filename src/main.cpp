#include <QApplication>
#include <QCoreApplication>
#include <QSettings>
#include <QTimer>
#include <QIcon>
#include "ui/MainWindow.h"
#include "ui/ScanProgressDialog.h"
#include "ui/Theme.h"
#include "ui/DebugLog.h"
#include "engine/AudioEngine.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "mcp/McpTransport.h"
#include "mcp/McpTransportStdio.h"
#include "frontend/FrontendServer.h"
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <cstring>
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

// Default WebSocket port for the HTML/Electron frontend (--headless mode).
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

    HDAW_LOG("main", QString("Mode: %1").arg(
        headlessMcp ? "HEADLESS MCP (--mcp-stdio)" :
        headlessFrontend ? "HEADLESS FRONTEND (--headless)" :
        "GUI"));

    if (headlessMcp) {
        QCoreApplication::setOrganizationName("HDAW");
        QCoreApplication::setApplicationName("HDAW");
        QCoreApplication app(argc, argv);
        AudioEngine engine;
        engine.initialize();
        // If plugin cache is empty, scan synchronously now
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

    if (headlessFrontend) {
        // Headless WebSocket server mode for the HTML/Electron frontend.
        // Runs the engine without any Qt Widgets; the frontend connects to
        // ws://127.0.0.1:<port> (default 8766) and drives it via JSON-RPC.
        // Mirrors the --mcp-stdio shape: QCoreApplication, AudioEngine, then
        // the FrontendServer instead of the MCP server.
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
        engine.initialize();
        // Restore the plugin cache silently; if empty the frontend can
        // trigger a rescan via the plugin.scanAll RPC.
        engine.getPluginManager().loadCache();

        frontend::FrontendServer server(engine);
        if (!server.start(port)) {
            HDAW_LOG("main", QString("FrontendServer failed to bind port %1").arg(port));
            return 1;
        }
        HDAW_LOG("main", QString("FrontendServer listening on ws://127.0.0.1:%1").arg(server.port()));

        QObject::connect(&app, &QCoreApplication::aboutToQuit, [&] {
            server.stop();
        });
        return app.exec();
    }

    if (const char* p = parseValue(argc, argv, "--mcp-http-port")) {
        QSettings settings;
        settings.setValue("mcp/httpPort", QString::fromUtf8(p));
    }

    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/app.ico"));

    if (noMcp) {
        app.setProperty("hdaw.noMcp", true);
    }

    // Apply global dark theme
    app.setStyleSheet(getGlobalStyleSheet());

    AudioEngine engine;
    engine.initialize();

    int result;
    {
        MainWindow window(engine);
        window.show();

        // Restore the plugin cache silently. If the cache exists and has
        // plugins registered, skip the full scan entirely — the user can
        // trigger a rescan via the Preferences dialog or the MCP tools.
        engine.getPluginManager().loadCache();

        if (engine.getPluginManager().getPlugins().empty())
        {
            // Fire scan progress dialog on next event loop iteration
            // (after the main window has painted)
            QTimer::singleShot(0, [&engine]() {
                ScanProgressDialog dialog(engine.getPluginManager());
                dialog.exec();
            });
        }
        else
        {
            HDAW_LOG("main", QString("Plugin cache loaded: %1 plugins").arg(
                static_cast<int>(engine.getPluginManager().getPlugins().size())));
        }

        result = app.exec();
    }
    // window destroyed here, all UI widgets cleaned up

    juce::Logger::setCurrentLogger(nullptr);

    engine.shutdown();
    return result;
}
