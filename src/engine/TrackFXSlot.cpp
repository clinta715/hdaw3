#include "TrackFXSlot.h"

HDAW::TrackFXSlot::~TrackFXSlot()
{
    HDAW_LOG("FXSlotDtor",
        (juce::String("this=") + juce::String::toHexString((juce::pointer_sized_int)this) +
          " editorWindow(before delete)=" + (editorWindow ? "set" : "null") +
          " rawPtr=0x" + juce::String::toHexString((juce::pointer_sized_int)editorWindow.get()) +
         " pluginInstance=" + (pluginInstance ? "ok" : "null") +
         " slotType=" + slotType.toStdString()).toStdString().c_str());
}

void HDAW::TrackFXSlot::showEditor()
{
    if (editorWindow != nullptr || pluginInstance == nullptr)
        return;

    auto* ed = pluginInstance->createEditor();
    if (ed == nullptr)
        return;

    editorWindow = std::make_unique<juce::DocumentWindow>(
        pluginInstance->getName(),
        juce::Colours::black,
        juce::DocumentWindow::closeButton);
    editorWindow->setContentOwned(ed, true);
    editorWindow->centreWithSize(ed->getWidth(), ed->getHeight());
    editorWindow->setVisible(true);
}
