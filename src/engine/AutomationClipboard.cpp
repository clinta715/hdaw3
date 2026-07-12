#include "AutomationClipboard.h"
#include <algorithm>

namespace HDAW
{
namespace
{
    std::vector<AutomationPointEntry> g_points;
    AutomationClipboardMeta g_meta;
}

void AutomationClipboard::copyPoints(const std::vector<AutomationPointEntry>& points,
                                     int paramID)
{
    g_points = points;
    g_meta.minTime = 0.0;
    g_meta.paramID = paramID;

    if (!g_points.empty())
    {
        g_meta.minTime = g_points[0].time;
        for (const auto& pt : g_points)
            g_meta.minTime = std::min(g_meta.minTime, pt.time);
    }
}

const std::vector<AutomationPointEntry>& AutomationClipboard::getPoints()
{
    return g_points;
}

const AutomationClipboardMeta& AutomationClipboard::getMeta()
{
    return g_meta;
}

bool AutomationClipboard::hasContent()
{
    return !g_points.empty();
}

void AutomationClipboard::clear()
{
    g_points.clear();
    g_meta = AutomationClipboardMeta{};
}

} // namespace HDAW
