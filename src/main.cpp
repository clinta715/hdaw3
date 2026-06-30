#include <QApplication>
#include <QCoreApplication>
#include <QSettings>
#include <QTimer>
#include "ui/MainWindow.h"
#include "ui/ScanProgressDialog.h"
#include "ui/Theme.h"
#include "ui/DebugLog.h"
#include "engine/AudioEngine.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "mcp/McpTransport.h"
#include "mcp/McpTransportStdio.h"
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <cstring>
#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

// Forward JUCE engine log messages to HDAW_LOG (appears in %TEMP%/hdaw_debug.log)
class HDAW_JuceLogger : public juce::Logger
{
    void logMessage(const juce::String& message) override
    {
        HDAW_LOG("JUCE", message.toRawUTF8());
    }
};

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

    const bool noMcp      = parseFlag(argc, argv, "--no-mcp");
    const bool forceStdio = parseFlag(argc, argv, "--mcp-stdio");
    const bool stdioAuto  = !isatty(fileno(stdin)) || !isatty(fileno(stdout));
    const bool headlessMcp = !noMcp && (forceStdio || stdioAuto);

    if (headlessMcp) {
        QCoreApplication::setOrganizationName("HDAW");
        QCoreApplication::setApplicationName("HDAW");
        QCoreApplication app(argc, argv);
        AudioEngine engine;
        engine.initialize();
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

    if (const char* p = parseValue(argc, argv, "--mcp-http-port")) {
        QSettings settings;
        settings.setValue("mcp/httpPort", QString::fromUtf8(p));
    }

    QApplication app(argc, argv);

    // Apply global dark theme
    app.setStyleSheet(getGlobalStyleSheet());

    AudioEngine engine;
    engine.initialize();

    int result;
    {
        MainWindow window(engine);
        window.show();

        // Fire scan progress dialog on next event loop iteration
        // (after the main window has painted)
        QTimer::singleShot(0, [&engine]() {
            ScanProgressDialog dialog(engine.getPluginManager());
            dialog.exec();
        });

        result = app.exec();
    }
    // window destroyed here, all UI widgets cleaned up

    juce::Logger::setCurrentLogger(nullptr);

    engine.shutdown();
    return result;
}
