#include <QApplication>
#include "ui/MainWindow.h"
#include "ui/Theme.h"
#include "engine/AudioEngine.h"
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

int main(int argc, char *argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    QApplication app(argc, argv);

    // Apply global dark theme
    app.setStyleSheet(getGlobalStyleSheet());

    AudioEngine engine;
    engine.initialize();

    int result;
    {
        MainWindow window(engine);
        window.show();
        result = app.exec();
    }
    // window destroyed here, all UI widgets cleaned up

    engine.shutdown();
    return result;
}
