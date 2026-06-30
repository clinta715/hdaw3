#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace proxy {

class PluginProxySlot;

class ProxyEditor : public juce::AudioProcessorEditor {
public:
    explicit ProxyEditor(PluginProxySlot& slot);
    ~ProxyEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    PluginProxySlot& slot;

    juce::Label nameLabel;
    juce::TextButton openEditorButton{"Open Editor"};
    juce::TextButton crashButton{"Restart?"};
    juce::ToggleButton bypassButton{"Bypass"};

    void onOpenEditorClicked();
    void onCrashRestart();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProxyEditor)
};

} // namespace proxy
