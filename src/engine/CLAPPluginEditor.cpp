#include "CLAPPluginEditor.h"
#include "CLAPPluginInstance.h"
#include <juce_gui_basics/juce_gui_basics.h>

#if JUCE_WINDOWS
#include <windows.h>
#endif

CLAPPluginEditor::CLAPPluginEditor(CLAPPluginInstance& instance)
    : AudioProcessorEditor(instance),
      clapInstance(instance)
{
    auto* plugin = clapInstance.getClapPlugin();
    guiExt = static_cast<const clap_plugin_gui_t*>(
        plugin->get_extension(plugin, CLAP_EXT_GUI));

    juce::Logger::writeToLog("HDAW: CLAPPluginEditor ctor: guiExt=" +
        juce::String(guiExt != nullptr ? "ok" : "null"));

    setSize(400, 300);
    startTimer(100);
}

CLAPPluginEditor::~CLAPPluginEditor()
{
    stopTimer();
    destroyUI();
}

void CLAPPluginEditor::parentHierarchyChanged()
{
    AudioProcessorEditor::parentHierarchyChanged();
}

void CLAPPluginEditor::timerCallback()
{
    if (uiCreated)
    {
        stopTimer();
        return;
    }

    if (guiExt == nullptr)
    {
        stopTimer();
        return;
    }

    if (getPeer() != nullptr && getWidth() > 0 && getHeight() > 0)
    {
        stopTimer();
        setOpaque(true);
        juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<CLAPPluginEditor>(this)]()
        {
            if (safeThis != nullptr && !safeThis->uiCreated && safeThis->getPeer() != nullptr)
                safeThis->createUI();
        });
    }
}

bool CLAPPluginEditor::createUI()
{
    if (uiCreated || guiExt == nullptr)
        return false;

    auto* plugin = clapInstance.getClapPlugin();
    bool isFloating = false;

    juce::Logger::writeToLog("HDAW: CLAPPluginEditor::createUI start");

#if JUCE_WINDOWS
    if (!guiExt->is_api_supported(plugin, CLAP_WINDOW_API_WIN32, isFloating))
    {
        juce::Logger::writeToLog("HDAW: CLAP createUI: is_api_supported(embedded)=false, retrying floating");
        isFloating = true;
        if (!guiExt->is_api_supported(plugin, CLAP_WINDOW_API_WIN32, isFloating))
        {
            juce::Logger::writeToLog("HDAW: CLAP createUI: is_api_supported(floating)=false, aborting");
            return false;
        }
    }

    juce::Logger::writeToLog("HDAW: CLAP createUI: is_api_supported ok, isFloating=" +
        juce::String(isFloating ? "true" : "false"));

    if (!guiExt->create(plugin, CLAP_WINDOW_API_WIN32, isFloating))
    {
        juce::Logger::writeToLog("HDAW: CLAP createUI: create() failed");
        return false;
    }

    juce::Logger::writeToLog("HDAW: CLAP createUI: create() ok");

    uint32_t w = 400, h = 300;
    guiExt->get_size(plugin, &w, &h);
    if (w == 0) w = 800;
    if (h == 0) h = 600;
    juce::Logger::writeToLog("HDAW: CLAP createUI: get_size=" +
        juce::String(static_cast<int>(w)) + "x" + juce::String(static_cast<int>(h)));
    setSize(static_cast<int>(w), static_cast<int>(h));

    if (auto* peer = getPeer())
    {
        clap_window_t window;
        window.api = CLAP_WINDOW_API_WIN32;
        window.win32 = peer->getNativeHandle();
        juce::Logger::writeToLog("HDAW: CLAP createUI: peer HWND=" +
            juce::String::toHexString((juce::pointer_sized_int)peer->getNativeHandle()));

        if (!guiExt->set_parent(plugin, &window))
        {
            juce::Logger::writeToLog("HDAW: CLAP createUI: set_parent() failed");
            guiExt->destroy(plugin);
            return false;
        }
        juce::Logger::writeToLog("HDAW: CLAP createUI: set_parent() ok");

        auto editorScreenPos = getScreenPosition();
        auto peerScreenPos = peer->getBounds().getPosition();
        auto offset = editorScreenPos - peerScreenPos;

        struct EnumData
        {
            HWND parentHwnd;
            int offsetX;
            int offsetY;
            int width;
            int height;
            HWND foundChild;
        };
        EnumData enumData = { (HWND)peer->getNativeHandle(),
                              offset.getX(), offset.getY(),
                              getWidth(), getHeight(), nullptr };

        EnumChildWindows((HWND)peer->getNativeHandle(),
            [](HWND child, LPARAM lParam) -> BOOL
            {
                auto* data = reinterpret_cast<EnumData*>(lParam);
                DWORD style = GetWindowLong(child, GWL_STYLE);
                if (style & WS_VISIBLE)
                {
                    data->foundChild = child;
                    MoveWindow(child, data->offsetX, data->offsetY,
                               data->width, data->height, TRUE);
                    return FALSE;
                }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&enumData));

        if (enumData.foundChild != nullptr)
        {
            pluginHWND = enumData.foundChild;
            juce::Logger::writeToLog("HDAW: CLAP createUI: repositioned child HWND to offset=" +
                juce::String(offset.getX()) + "," + juce::String(offset.getY()) +
                " size=" + juce::String(getWidth()) + "x" + juce::String(getHeight()));
        }
    }
    else
    {
        juce::Logger::writeToLog("HDAW: CLAP createUI: getPeer()=null at set_parent");
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
    if (w == 0) w = 800;
    if (h == 0) h = 600;
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
    if (w == 0) w = 800;
    if (h == 0) h = 600;
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

    juce::Logger::writeToLog("HDAW: CLAP createUI: show() calling");
    guiExt->show(plugin);
    uiCreated = true;
    juce::Logger::writeToLog("HDAW: CLAP createUI: complete, uiCreated=true");
    return true;
}

void CLAPPluginEditor::destroyUI()
{
    if (!uiCreated || guiExt == nullptr)
        return;

    auto* plugin = clapInstance.getClapPlugin();
    guiExt->hide(plugin);
    guiExt->destroy(plugin);
    uiCreated = false;
#if JUCE_WINDOWS
    pluginHWND = nullptr;
#endif
}

void CLAPPluginEditor::paint(juce::Graphics& g)
{
    if (!uiCreated)
    {
        g.fillAll(juce::Colour(0xFF2A2A2E));
        if (guiExt == nullptr || getPeer() != nullptr)
        {
            g.setColour(juce::Colours::white);
            g.setFont(14.0f);
            g.drawText("No CLAP GUI available", getLocalBounds(),
                       juce::Justification::centred, false);
        }
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

#if JUCE_WINDOWS
        if (pluginHWND != nullptr)
        {
            auto* peer = getPeer();
            if (peer != nullptr)
            {
                auto editorScreenPos = getScreenPosition();
                auto peerScreenPos = peer->getBounds().getPosition();
                auto offset = editorScreenPos - peerScreenPos;
                MoveWindow((HWND)pluginHWND, offset.getX(), offset.getY(),
                           getWidth(), getHeight(), TRUE);
            }
        }
#endif
    }
}
