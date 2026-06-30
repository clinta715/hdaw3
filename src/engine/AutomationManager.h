#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <algorithm>
#include "../model/ProjectModel.h"

namespace HDAW {

class AutomationManager
{
public:
    AutomationManager() = default;
    ~AutomationManager() = default;

    void setAutomationTree(const juce::ValueTree& tree)
    {
        automationTree = tree;
        rebuildCache();
    }

    juce::ValueTree getAutomationTree() const { return automationTree; }

    void setEnabled(bool en) { enabled = en; }
    bool isEnabled() const { return enabled; }

    void setParamID(int id) { paramID = id; }
    int getParamID() const { return paramID; }

    double getValueAtTime(double timeSeconds) const
    {
        double result = -1.0;
        if (cacheLock.tryEnter())
        {
            if (!enabled)
            {
                cacheLock.exit();
                return -1.0;
            }

            if (!points.empty())
            {
                auto it = std::lower_bound(points.begin(), points.end(), timeSeconds,
                    [](const std::pair<double, double>& p, double t) { return p.first < t; });
                if (it == points.end())
                    result = points.back().second;
                else if (it == points.begin())
                    result = points.front().second;
                else
                {
                    const auto& a = *(it - 1);
                    const auto& b = *it;
                    double t = (timeSeconds - a.first) / (b.first - a.first);
                    result = a.second + t * (b.second - a.second);
                }
            }
            cacheLock.exit();
        }
        return result;
    }

    int getNumPoints() const { return static_cast<int>(points.size()); }
    const std::vector<std::pair<double, double>>& getPoints() const { return points; }

    void addPoint(double time, double value)
    {
        if (!automationTree.isValid()) return;
        auto pointList = automationTree.getChildWithName(IDs::POINT_LIST);
        if (!pointList.isValid())
        {
            pointList = juce::ValueTree(IDs::POINT_LIST);
            automationTree.addChild(pointList, -1, nullptr);
        }
        juce::ValueTree point(IDs::POINT);
        point.setProperty(IDs::startTime, time, nullptr);
        point.setProperty(IDs::gain, value, nullptr);
        pointList.addChild(point, -1, nullptr);
        rebuildCache();
    }

    void removePoint(int index)
    {
        if (!automationTree.isValid()) return;
        auto pointList = automationTree.getChildWithName(IDs::POINT_LIST);
        if (pointList.isValid() && index >= 0 && index < pointList.getNumChildren())
        {
            pointList.removeChild(index, nullptr);
            rebuildCache();
        }
    }

    void updatePoint(int index, double time, double value)
    {
        if (!automationTree.isValid()) return;
        auto pointList = automationTree.getChildWithName(IDs::POINT_LIST);
        if (pointList.isValid() && index >= 0 && index < pointList.getNumChildren())
        {
            auto p = pointList.getChild(index);
            p.setProperty(IDs::startTime, time, nullptr);
            p.setProperty(IDs::gain, value, nullptr);
            rebuildCache();
        }
    }

    void rebuildCache()
    {
        std::vector<std::pair<double, double>> newPoints;
        if (automationTree.isValid())
        {
            auto pointList = automationTree.getChildWithName(IDs::POINT_LIST);
            if (pointList.isValid())
            {
                for (int i = 0; i < pointList.getNumChildren(); ++i)
                {
                    auto p = pointList.getChild(i);
                    double t = p.getProperty(IDs::startTime);
                    double v = p.getProperty(IDs::gain);
                    newPoints.push_back({t, v});
                }

                std::sort(newPoints.begin(), newPoints.end(),
                    [](const auto& a, const auto& b) { return a.first < b.first; });
            }
        }

        juce::SpinLock::ScopedLockType lock(cacheLock);
        points.swap(newPoints);
        enabled = automationTree.isValid()
            ? static_cast<bool>(automationTree.getProperty(IDs::automationEnabled))
            : false;
    }

private:
    juce::ValueTree automationTree;
    mutable juce::SpinLock cacheLock;
    std::vector<std::pair<double, double>> points;
    bool enabled = false;
    int paramID = 1;
};

} // namespace HDAW
