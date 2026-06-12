#include <QApplication>
#include <QTimer>
#include "ui/MainWindow.h"
#include "ui/ScanProgressDialog.h"
#include "ui/Theme.h"
#include "ui/DebugLog.h"
#include "engine/AudioEngine.h"
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

// Forward JUCE engine log messages to HDAW_LOG (appears in %TEMP%/hdaw_debug.log)
class HDAW_JuceLogger : public juce::Logger
{
    void logMessage(const juce::String& message) override
    {
        HDAW_LOG("JUCE", message.toRawUTF8());
    }
};

int main(int argc, char *argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    HDAW_JuceLogger juceLogger;
    juce::Logger::setCurrentLogger(&juceLogger);

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
