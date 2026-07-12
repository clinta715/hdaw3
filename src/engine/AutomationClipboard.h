#pragma once
#include <vector>

namespace HDAW
{

struct AutomationPointEntry
{
    double time = 0.0;
    float  value = 0.0f;
};

struct AutomationClipboardMeta
{
    double minTime = 0.0;
    int    paramID = 0;
};

class AutomationClipboard
{
public:
    static void copyPoints(const std::vector<AutomationPointEntry>& points,
                           int paramID = 0);
    static const std::vector<AutomationPointEntry>& getPoints();
    static const AutomationClipboardMeta& getMeta();
    static bool hasContent();
    static void clear();
};

} // namespace HDAW
