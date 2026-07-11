#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace HDAW {

class PluginEditorWindow : public juce::DocumentWindow
{
public:
    PluginEditorWindow(juce::AudioProcessorEditor* editor,
                       const juce::String& pluginName,
                       std::function<void()> onClose);

    void closeButtonPressed() override;

private:
    std::function<void()> onCloseCallback;
};

} // namespace HDAW
