#include "AudioEngineCommands.h"
#include "AudioEngineCommands_Helpers.h"
#include "AudioEngine.h"
#include "MainAudioProcessor.h"
#include "../model/ProjectModel.h"

// ─── ProjectCommands — Automation ─────────────────────────────────

bool AudioEngineCommands::addAutomationLane(int trackIndex, const std::string& laneName, int paramID)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren()) return false;

    auto track = trackList.getChild(trackIndex);
    auto autoList = track.getChildWithName(IDs::AUTOMATION_LIST);
    if (!autoList.isValid())
    {
        autoList = juce::ValueTree(IDs::AUTOMATION_LIST);
        track.addChild(autoList, -1, &um);
    }

    // Don't add duplicate lanes. Same-name collision is an idempotent no-op
    // when the requested paramID matches the existing binding (0 = unbound,
    // the legacy default, matches any) and a conflict when it differs. A
    // different name with a requested nonzero paramID matching an existing
    // lane's binding is a conflict so two lanes can't drive the same plugin
    // parameter.
    for (int i = 0; i < autoList.getNumChildren(); ++i)
    {
        auto existing = autoList.getChild(i);
        int existingParam = static_cast<int>(existing.getProperty(IDs::paramID, 0));
        if (existing.getProperty(IDs::name, "").toString().toStdString() == laneName)
        {
            if (paramID == 0 || paramID == existingParam)
                return true;
            return false;
        }
        if (paramID != 0 && existingParam == paramID)
            return false;
    }

    juce::ValueTree lane(IDs::AUTOMATION);
    lane.setProperty(IDs::name, juce::String(laneName), &um);
    lane.setProperty(IDs::automationEnabled, true, &um);
    if (paramID != 0)
        lane.setProperty(IDs::paramID, paramID, &um);
    lane.addChild(juce::ValueTree(IDs::POINT_LIST), -1, nullptr);
    autoList.addChild(lane, -1, &um);
    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildAutomationCache(trackIndex);
    return true;
}

void AudioEngineCommands::removeAutomationLane(int trackIndex, const std::string& laneName)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto autoLane = findAutomationLane(trackIndex, laneName);
    if (autoLane.isValid())
        autoLane.getParent().removeChild(autoLane, &um);
    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildAutomationCache(trackIndex);
}

void AudioEngineCommands::addAutomationPoint(int trackIndex, const std::string& lane,
                                             double time, float value)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto autoLane = findAutomationLane(trackIndex, lane);
    if (!autoLane.isValid()) return;

    // RPC/command boundary speaks beats; the ValueTree and the audio engine
    // store seconds. Convert before the tree write (docs/architecture.md).
    double bpm = engine_.getProjectModel().getTree().getProperty(IDs::tempo, 120.0);
    time = HDAW::beatsToSeconds(time, bpm);

    auto pointList = autoLane.getChildWithName(IDs::POINT_LIST);
    if (!pointList.isValid())
    {
        pointList = juce::ValueTree(IDs::POINT_LIST);
        autoLane.addChild(pointList, -1, nullptr);
    }

    juce::ValueTree point(IDs::POINT);
    point.setProperty(IDs::startTime, time, nullptr);
    point.setProperty(IDs::gain, static_cast<double>(value), nullptr);
    pointList.addChild(point, -1, &um);
    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildAutomationCache(trackIndex);
}

void AudioEngineCommands::removeAutomationPoint(int trackIndex, const std::string& lane,
                                                double time)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto autoLane = findAutomationLane(trackIndex, lane);
    if (!autoLane.isValid()) return;

    // RPC/command boundary speaks beats; the ValueTree stores seconds. Convert
    // before the match so an identical conversion round-trips exactly.
    double bpm = engine_.getProjectModel().getTree().getProperty(IDs::tempo, 120.0);
    time = HDAW::beatsToSeconds(time, bpm);

    auto pointList = autoLane.getChildWithName(IDs::POINT_LIST);
    if (!pointList.isValid()) return;

    for (int i = 0; i < pointList.getNumChildren(); ++i)
    {
        auto pt = pointList.getChild(i);
        if (static_cast<double>(pt.getProperty(IDs::startTime, 0.0)) == time)
        {
            pointList.removeChild(i, &um);
            if (auto* proc = engine_.getMainProcessor())
                proc->rebuildAutomationCache(trackIndex);
            return;
        }
    }
}

void AudioEngineCommands::setAutomationEnabled(int trackIndex, const std::string& lane,
                                               bool enabled)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto autoLane = findAutomationLane(trackIndex, lane);
    if (autoLane.isValid())
    {
        autoLane.setProperty(IDs::automationEnabled, enabled, &um);
        if (auto* proc = engine_.getMainProcessor())
            proc->rebuildAutomationCache(trackIndex);
    }
}

void AudioEngineCommands::setFaderAuthoritative(int trackIndex, bool authoritative)
{
    auto trackList = engine_.getProjectModel().getTrackListTree();
    auto& um = engine_.getProjectModel().getUndoManager();

    auto disableTrack = [&](int ti) {
        if (ti < 0 || ti >= trackList.getNumChildren()) return;
        auto autoList = trackList.getChild(ti).getChildWithName(IDs::AUTOMATION_LIST);
        if (!autoList.isValid()) return;
        bool changed = false;
        for (int i = 0; i < autoList.getNumChildren(); ++i)
        {
            auto lane = autoList.getChild(i);
            const juce::String laneName = lane.getProperty(IDs::name, "").toString();
            const int paramID = lane.getProperty(IDs::paramID, 0);
            if (laneName == "Volume" || paramID == 1) // the volume target
            {
                // authoritative=true means the fader wins -> automation OFF.
                const bool targetEnabled = !authoritative;
                if (static_cast<bool>(lane.getProperty(IDs::automationEnabled, true)) != targetEnabled)
                {
                    lane.setProperty(IDs::automationEnabled, targetEnabled, &um);
                    changed = true;
                }
            }
        }
        if (changed)
            if (auto* proc = engine_.getMainProcessor())
                proc->rebuildAutomationCache(ti);
    };

    if (trackIndex == -1)
        for (int t = 0; t < trackList.getNumChildren(); ++t)
            disableTrack(t);
    else
        disableTrack(trackIndex);
}

void AudioEngineCommands::setAutomationMode(int trackIndex, const std::string& laneName,
                                             const std::string& mode)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto autoLane = findAutomationLane(trackIndex, laneName);
    if (!autoLane.isValid()) return;

    autoLane.setProperty(IDs::automationMode, juce::String(mode), &um);

    if (auto* proc = engine_.getMainProcessor())
    {
        if (auto* track = proc->getTrack(trackIndex))
        {
            for (int i = 0; i < track->getNumAutomations(); ++i)
            {
                auto& am = track->getAutomation(i);
                if (am.getAutomationTree().getProperty(IDs::name, "").toString().toStdString() == laneName)
                {
                    HDAW::AutomationManager::Mode m = HDAW::AutomationManager::Mode::Read;
                    if (mode == "write") m = HDAW::AutomationManager::Mode::Write;
                    else if (mode == "touch") m = HDAW::AutomationManager::Mode::Touch;
                    else if (mode == "latch") m = HDAW::AutomationManager::Mode::Latch;
                    am.setMode(m);
                    break;
                }
            }
        }
    }
}

void AudioEngineCommands::notifyAutomationTouch(int trackIndex, int paramID, bool touching)
{
    auto* proc = engine_.getMainProcessor();
    if (!proc) return;
    auto* track = proc->getTrack(trackIndex);
    if (!track) return;

    for (int i = 0; i < track->getNumAutomations(); ++i)
    {
        auto& am = track->getAutomation(i);
        if (am.getParamID() == paramID)
        {
            am.setTouching(touching);
            break;
        }
    }
}

void AudioEngineCommands::setAutomationPointValue(int trackIndex, const std::string& lane,
                                                   double time, float value)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto autoLane = findAutomationLane(trackIndex, lane);
    if (!autoLane.isValid()) return;

    // RPC/command boundary speaks beats; the ValueTree stores seconds. Convert
    // once, then use the same converted value for the match AND the write so
    // the round trip is exact.
    double bpm = engine_.getProjectModel().getTree().getProperty(IDs::tempo, 120.0);
    time = HDAW::beatsToSeconds(time, bpm);

    auto pointList = autoLane.getChildWithName(IDs::POINT_LIST);
    if (!pointList.isValid()) return;

    for (int i = 0; i < pointList.getNumChildren(); ++i)
    {
        auto pt = pointList.getChild(i);
        if (static_cast<double>(pt.getProperty(IDs::startTime, 0.0)) == time)
        {
            pt.setProperty(IDs::startTime, time, &um);
            pt.setProperty(IDs::gain, static_cast<double>(value), &um);
            if (auto* proc = engine_.getMainProcessor())
                proc->rebuildAutomationCache(trackIndex);
            return;
        }
    }
}
