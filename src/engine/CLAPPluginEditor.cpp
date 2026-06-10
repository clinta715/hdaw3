#include "CLAPPluginEditor.h"
#include "CLAPPluginInstance.h"
#include <juce_gui_basics/juce_gui_basics.h>

CLAPPluginEditor::CLAPPluginEditor(CLAPPluginInstance& instance)
    : AudioProcessorEditor(instance),
      clapInstance(instance)
{
    auto* plugin = clapInstance.getClapPlugin();
    guiExt = static_cast<const clap_plugin_gui_t*>(
        plugin->get_extension(plugin, CLAP_EXT_GUI));

    if (guiExt != nullptr)
    {
        setOpaque(true);
        createUI();
    }
    else
    {
        setSize(400, 300);
    }
}

CLAPPluginEditor::~CLAPPluginEditor()
{
    destroyUI();
}

bool CLAPPluginEditor::createUI()
{
    if (uiCreated || guiExt == nullptr)
        return false;

    auto* plugin = clapInstance.getClapPlugin();
    bool isFloating = false;

#if JUCE_WINDOWS
    if (!guiExt->is_api_supported(plugin, CLAP_WINDOW_API_WIN32, isFloating))
    {
        isFloating = true;
        if (!guiExt->is_api_supported(plugin, CLAP_WINDOW_API_WIN32, isFloating))
            return false;
    }

    if (!guiExt->create(plugin, CLAP_WINDOW_API_WIN32, isFloating))
        return false;

    uint32_t w = 400, h = 300;
    guiExt->get_size(plugin, &w, &h);
    setSize(static_cast<int>(w), static_cast<int>(h));

    if (auto* peer = getPeer())
    {
        clap_window_t window;
        window.api = CLAP_WINDOW_API_WIN32;
        window.win32 = peer->getNativeHandle();

        if (!guiExt->set_parent(plugin, &window))
        {
            guiExt->destroy(plugin);
            return false;
        }
    }
    else
    {
        guiExt->destroy(plugin);
        return false;
    }
#elif JUCE_MAC
    if (!guiExt->is_api_supported(plugin, CLAP_WINDOW_API_COCOA, isFloating))
    {
        isFloating = true;
        if (!guiExt->is_api_supported(plugin, CLAP_WINDOW_API_COCOA, isFloating))
            return false;
    }

    if (!guiExt->create(plugin, CLAP_WINDOW_API_COCOA, isFloating))
        return false;

    uint32_t w = 400, h = 300;
    guiExt->get_size(plugin, &w, &h);
    setSize(static_cast<int>(w), static_cast<int>(h));

    if (auto* peer = getPeer())
    {
        clap_window_t window;
        window.api = CLAP_WINDOW_API_COCOA;
        window.cocoa = peer->getNativeHandle();

        if (!guiExt->set_parent(plugin, &window))
        {
            guiExt->destroy(plugin);
            return false;
        }
    }
    else
    {
        guiExt->destroy(plugin);
        return false;
    }
#else
    if (!guiExt->is_api_supported(plugin, CLAP_WINDOW_API_X11, isFloating))
        return false;

    if (!guiExt->create(plugin, CLAP_WINDOW_API_X11, isFloating))
        return false;

    uint32_t w = 400, h = 300;
    guiExt->get_size(plugin, &w, &h);
    setSize(static_cast<int>(w), static_cast<int>(h));

    if (auto* peer = getPeer())
    {
        clap_window_t window;
        window.api = CLAP_WINDOW_API_X11;
        window.x11 = (unsigned long)peer->getNativeHandle();

        if (!guiExt->set_parent(plugin, &window))
        {
            guiExt->destroy(plugin);
            return false;
        }
    }
    else
    {
        guiExt->destroy(plugin);
        return false;
    }
#endif

    guiExt->show(plugin);
    uiCreated = true;
    return true;
}

void CLAPPluginEditor::destroyUI()
{
    if (!uiCreated || guiExt == nullptr)
        return;

    auto* plugin = clapInstance.getClapPlugin();
    guiExt->destroy(plugin);
    uiCreated = false;
}

void CLAPPluginEditor::paint(juce::Graphics& g)
{
    if (!uiCreated)
    {
        g.fillAll(juce::Colour(0xFF2A2A2E));
        g.setColour(juce::Colours::white);
        g.setFont(14.0f);
        g.drawText("No CLAP GUI available", getLocalBounds(),
                   juce::Justification::centred, false);
    }
}

void CLAPPluginEditor::resized()
{
    if (uiCreated && guiExt != nullptr)
    {
        auto* plugin = clapInstance.getClapPlugin();
        uint32_t w = static_cast<uint32_t>(getWidth());
        uint32_t h = static_cast<uint32_t>(getHeight());
        guiExt->set_size(plugin, w, h);
    }
}
