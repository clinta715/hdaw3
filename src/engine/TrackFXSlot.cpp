#include "TrackFXSlot.h"
#include "../ui/PluginEditorWindow.h"

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
    juce::Logger::writeToLog("HDAW: TrackFXSlot::showEditor entry: editorWindow=" +
        juce::String(editorWindow != nullptr ? "exists" : "null") +
        " pluginInstance=" + juce::String(pluginInstance != nullptr ? "ok" : "null") +
        " isPlugin=" + juce::String(isPlugin() ? "true" : "false"));

    if (editorWindow != nullptr || pluginInstance == nullptr)
        return;

    auto* ed = pluginInstance->createEditor();
    juce::Logger::writeToLog("HDAW: TrackFXSlot::showEditor: createEditor=" +
        juce::String(ed != nullptr ? "ok" : "null"));
    if (ed == nullptr)
        return;

    auto onClose = [this]() { closeEditor(); };
    editorWindow = std::make_unique<PluginEditorWindow>(
        ed, pluginInstance->getName(), std::move(onClose));
    juce::Logger::writeToLog("HDAW: TrackFXSlot::showEditor: editorWindow created, isEditorOpen=" +
        juce::String(isEditorOpen() ? "true" : "false"));
}
