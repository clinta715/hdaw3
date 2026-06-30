#include <gtest/gtest.h>
#include "engine/TrackFXSlot.h"
#include "ui/DebugLog.h"

// Headless diagnostic: construct a TrackFXSlot (the no-plugin ctor) and
// rely on HDAW_LOG("FXSlotCtor",...) in TrackFXSlot.h to write the
// real value of editorWindow (the unique_ptr's stored raw pointer) to
// %TEMP%/hdaw_debug.log. If the ctor sees 0xcdcdcdcdcdcdcdcd, the
// default-init of the member did not run for that object (or the
// memory was 0xcd before the constructor).
TEST(TrackFXSlotDebug, CtorEditorWindowIsNullptr) {
    HDAW_LOG("FXSlotDebugTest", QString::fromStdString(
        (juce::String("constructing slot at ") +
         juce::String::toHexString((juce::pointer_sized_int)(void*)nullptr) +
         " ... look at FXSlotCtor line above for editorWindow raw value")
        .toStdString()));
    HDAW::TrackFXSlot slot(juce::String("eq"));
    HDAW_LOG("FXSlotDebugTest", QString::fromStdString(
        (juce::String("constructed slot at ") +
         juce::String::toHexString((juce::pointer_sized_int)&slot) +
         " isEditorOpen=" + juce::String(slot.isEditorOpen() ? "true" : "false") +
         " (expect false)")
        .toStdString()));
    // If the ctor log printed editorWindow=0xcdcdcdcdcdcdcdcd, that's the
    // bug. If it printed editorWindow=0x0, the default-init ran correctly
    // and the 0xcd must come from somewhere AFTER construction.
    EXPECT_FALSE(slot.isEditorOpen());
}
