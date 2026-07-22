#include "PluginEditorWindow.h"

namespace HDAW {

PluginEditorWindow::PluginEditorWindow(juce::AudioProcessorEditor* editor,
                                       const juce::String& pluginName,
                                       std::function<void()> onClose)
    : juce::DocumentWindow(pluginName,
                           juce::Colour::fromRGB(40, 40, 40),
                           juce::DocumentWindow::closeButton),
      onCloseCallback(std::move(onClose))
{
    setUsingNativeTitleBar(true);
    setContentOwned(editor, true);
    setResizable(true, false);

    int editorW = editor->getWidth();
    int editorH = editor->getHeight();
    juce::Logger::writeToLog("HDAW: PluginEditorWindow ctor: editor size=" +
        juce::String(editorW) + "x" + juce::String(editorH));

    if (auto* peer = getPeer())
    {
        juce::Logger::writeToLog("HDAW: PluginEditorWindow ctor: peer HWND=" +
            juce::String::toHexString((juce::pointer_sized_int)peer->getNativeHandle()));
    }
    else
    {
        juce::Logger::writeToLog("HDAW: PluginEditorWindow ctor: peer=null after setContentOwned!");
    }

    centreWithSize(editorW, editorH);
    juce::Logger::writeToLog("HDAW: PluginEditorWindow ctor: bounds=" +
        getBounds().toString());

    setVisible(true);
    toFront(true);

    // JUCE's toFront() uses SWP_NOACTIVATE internally, so on Windows
    // the window often stays behind the active foreground window.
    // Flash via WS_EX_TOPMOST to force it above everything, then
    // immediately release so the window doesn't stay pinned.
    setAlwaysOnTop(true);
    setAlwaysOnTop(false);

    juce::Logger::writeToLog("HDAW: PluginEditorWindow ctor: setVisible(true) called, isVisible=" +
        juce::String(isVisible() ? "true" : "false"));
}

void PluginEditorWindow::closeButtonPressed()
{
    if (onCloseCallback)
        onCloseCallback();
}

} // namespace HDAW
