#include "TrackFXSlot.h"

HDAW::TrackFXSlot::~TrackFXSlot()
{
    HDAW_LOG("FXSlotDtor", QString::fromStdString(
        (juce::String("this=") + juce::String::toHexString((juce::pointer_sized_int)this) +
         " editorWindow(before delete)=" + (editorWindow ? "set" : "null") +
         " rawPtr=0x" + juce::String::toHexString((juce::pointer_sized_int)editorWindow.get()) +
         " pluginInstance=" + (pluginInstance ? "ok" : "null") +
         " slotType=" + slotType.toStdString()).toStdString()));
}
