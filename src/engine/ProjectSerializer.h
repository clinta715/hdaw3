#pragma once
#include <juce_core/juce_core.h>
#include "../model/ProjectModel.h"

namespace HDAW {

class ProjectSerializer
{
public:
    static bool save(ProjectModel& model, const juce::File& file);
    static bool load(ProjectModel& model, const juce::File& file);
    static void createNew(ProjectModel& model);
};

} // namespace HDAW
