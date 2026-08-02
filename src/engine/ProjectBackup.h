#pragma once
#include <juce_core/juce_core.h>

namespace HDAW {

void backupProject(const juce::File& projectFile, int maxBackups = 10);

} // namespace HDAW