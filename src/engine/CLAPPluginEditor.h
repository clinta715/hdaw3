#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <clap/all.h>

class CLAPPluginInstance;

class CLAPPluginEditor : public juce::AudioProcessorEditor
{
public:
    explicit CLAPPluginEditor(CLAPPluginInstance& instance);
    ~CLAPPluginEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    bool createUI();
    void destroyUI();

    CLAPPluginInstance& clapInstance;
    const clap_plugin_gui_t* guiExt = nullptr;
    bool uiCreated = false;

#if JUCE_WINDOWS
    void* pluginHWND = nullptr;
#endif
};
