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
        if (!enabled || points.empty())
            return -1.0;

        if (timeSeconds <= points.front().first)
            return points.front().second;
        if (timeSeconds >= points.back().first)
            return points.back().second;

        for (size_t i = 0; i < points.size() - 1; ++i)
        {
            if (timeSeconds >= points[i].first && timeSeconds <= points[i + 1].first)
            {
                double t = (timeSeconds - points[i].first) / (points[i + 1].first - points[i].first);
                return points[i].second + t * (points[i + 1].second - points[i].second);
            }
        }

        return points.back().second;
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
        points.clear();
        if (!automationTree.isValid()) return;

        auto pointList = automationTree.getChildWithName(IDs::POINT_LIST);
        if (!pointList.isValid()) return;

        for (int i = 0; i < pointList.getNumChildren(); ++i)
        {
            auto p = pointList.getChild(i);
            double t = p.getProperty(IDs::startTime);
            double v = p.getProperty(IDs::gain);
            points.push_back({t, v});
        }

        std::sort(points.begin(), points.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

        enabled = automationTree.getProperty(IDs::automationEnabled);
    }

private:
    juce::ValueTree automationTree;
    std::vector<std::pair<double, double>> points;
    bool enabled = false;
    int paramID = 1;
};

} // namespace HDAW
