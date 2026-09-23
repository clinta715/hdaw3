#include "AudioEngineCommands.h"
#include "AudioEngineCommands_Helpers.h"
#include "AudioEngine.h"
#include "MainAudioProcessor.h"
#include "../model/ProjectModel.h"
#include "AutomationPreset.h"

// ─── ProjectCommands — Automation ─────────────────────────────────

bool AudioEngineCommands::addAutomationLane(int trackIndex, const std::string& laneName, int paramID,
                                            bool replace)
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

    // Upsert (replace == true, paramID != 0): the caller declares "the lane
    // bound to paramID is mine, named laneName" — the re-run path for a
    // post-arrangement automation pass. The lane is RENAMED in place, never
    // deleted and recreated: points written outside the pass's windows must
    // survive. Name ownership is checked over the whole list FIRST, so a
    // laneName held by a different paramID is still a conflict and can never
    // be silently stolen (or duplicated) by the rename below. paramID == 0
    // means "unbound" and falls through to the create path unchanged.
    if (replace && paramID != 0)
    {
        for (int i = 0; i < autoList.getNumChildren(); ++i)
        {
            auto existing = autoList.getChild(i);
            if (existing.getProperty(IDs::name, "").toString().toStdString() != laneName)
                continue;
            // The name is already ours when it sits on the same binding
            // (idempotent no-op); on a different one it is a conflict.
            return static_cast<int>(existing.getProperty(IDs::paramID, 0)) == paramID;
        }
        for (int i = 0; i < autoList.getNumChildren(); ++i)
        {
            auto existing = autoList.getChild(i);
            if (static_cast<int>(existing.getProperty(IDs::paramID, 0)) != paramID)
                continue;
            existing.setProperty(IDs::name, juce::String(laneName), &um);
            if (auto* proc = engine_.getMainProcessor())
                proc->rebuildAutomationCache(trackIndex);
            return true;
        }
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

// ─── applyAutomationPreset (P2-3 preset bank) ─────────────────────
// Generates one named recipe per beat window and writes the envelope points
// onto an EXISTING lane as ONE undo unit, enabling the lane so renders honor
// it. Mirrors generateAutomationEnvelope's beats→seconds conversion and
// in-window insertion exactly (docs/architecture.md unit convention); the
// only extra step scales the plan's per-beat density (4.0 = 0.25-beat grid)
// to the seconds domain the generator + ValueTree use, keeping the grid
// locked to beats at any tempo.
//
// Validation happens BEFORE the first tree write, so any error (bad window,
// missing lane) is an atomic no-op that leaves the project untouched.
std::string AudioEngineCommands::applyAutomationPreset(
    int trackIndex, const std::string& laneName,
    const std::vector<HDAW::AutomationPreset::PresetWindow>& windows,
    bool clearWindowBeforeApply, uint64_t seed, int* pointsAdded)
{
    if (pointsAdded) *pointsAdded = 0;

    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren())
        return "track not found: " + std::to_string(trackIndex);

    auto autoLane = findAutomationLane(trackIndex, laneName);
    if (!autoLane.isValid())
        return "lane not found: " + laneName + " (create it with add_automation_lane first)";

    if (windows.empty())
        return "no preset windows given";

    for (const auto& w : windows)
    {
        if (!(w.end > w.start))
            return "bad window: end (" + std::to_string(w.end) +
                   ") must be > start (" + std::to_string(w.start) + ")";
    }

    auto& um = engine_.getProjectModel().getUndoManager();
    um.beginNewTransaction("apply automation preset");

    const int added = writePresetWindowsToLane(trackIndex, autoLane, windows,
                                               clearWindowBeforeApply, seed);
    if (pointsAdded) *pointsAdded = added;
    return "";
}

// Shared NON-transactional core of the preset bank: writes envelope points
// for one or more windows onto an EXISTING lane (assumed valid), enables it,
// and refreshes the automation cache. The caller owns the undo transaction
// boundary — applyAutomationPreset begins its own; applyMovementPlan batches
// many lanes inside ONE. Returns the number of points written.
int AudioEngineCommands::writePresetWindowsToLane(
    int trackIndex, juce::ValueTree autoLane,
    const std::vector<HDAW::AutomationPreset::PresetWindow>& windows,
    bool clearWindowBeforeApply, uint64_t seed)
{
    auto* proc = engine_.getMainProcessor();
    auto& um = engine_.getProjectModel().getUndoManager();

    double bpm = engine_.getProjectModel().getTree().getProperty(IDs::tempo, 120.0);

    auto pointList = autoLane.getChildWithName(IDs::POINT_LIST);
    if (!pointList.isValid())
    {
        pointList = juce::ValueTree(IDs::POINT_LIST);
        autoLane.addChild(pointList, -1, nullptr);
    }

    int added = 0;
    for (const auto& w : windows)
    {
        auto plan = HDAW::AutomationPreset::plan(w, seed);
        for (const auto& seg : plan.segments)
        {
            // Beats at this boundary -> seconds in the tree (same conversion
            // as generateAutomationEnvelope).
            const double startSec = HDAW::beatsToSeconds(seg.startTime, bpm);
            const double endSec = HDAW::beatsToSeconds(seg.endTime, bpm);

            if (clearWindowBeforeApply)
            {
                // Remove existing POINTs in [startSec, endSec] (inclusive),
                // the same replace-in-window insertion generateAutomationEnvelope
                // performs unconditionally.
                for (int i = pointList.getNumChildren() - 1; i >= 0; --i)
                {
                    auto pt = pointList.getChild(i);
                    const double t = static_cast<double>(pt.getProperty(IDs::startTime, 0.0));
                    if (t >= startSec && t <= endSec)
                        pointList.removeChild(i, &um);
                }
            }

            auto genParams = seg;
            genParams.startTime = startSec;
            genParams.endTime = endSec;
            // Density 4.0 in the plan means per beat (0.25-beat grid);
            // per-second density = per-beat * bpm / 60.
            genParams.densityPerSec = seg.densityPerSec * bpm / 60.0;

            auto generated = HDAW::EnvelopeGenerator::generate(genParams);
            for (const auto& [time, value] : generated)
            {
                juce::ValueTree point(IDs::POINT);
                point.setProperty(IDs::startTime, time, nullptr);
                point.setProperty(IDs::gain, value, nullptr);
                pointList.addChild(point, -1, &um);
                ++added;
            }
        }
    }

    // Lanes are disabled by default; enable so playback/render honors the
    // points (same effect as the set_automation_enabled path). One undo unit
    // with the point writes above.
    autoLane.setProperty(IDs::automationEnabled, true, &um);
    if (proc)
        proc->rebuildAutomationCache(trackIndex);
    return added;
}

namespace {

juce::ValueTree findLaneByParamID(const juce::ValueTree& autoList, int paramID)
{
    if (!autoList.isValid()) return {};
    for (int i = 0; i < autoList.getNumChildren(); ++i)
    {
        const auto lane = autoList.getChild(i);
        if (static_cast<int>(lane.getProperty(IDs::paramID, 0)) == paramID)
            return lane;
    }
    return {};
}

bool laneNameExists(const juce::ValueTree& autoList, const juce::String& name)
{
    if (!autoList.isValid()) return false;
    for (int i = 0; i < autoList.getNumChildren(); ++i)
        if (autoList.getChild(i).getProperty(IDs::name, "").toString() == name) return true;
    return false;
}

juce::String firstFreeLaneName(const juce::ValueTree& autoList, const juce::String& base)
{
    if (!laneNameExists(autoList, base)) return base;
    int n = 2;
    while (laneNameExists(autoList, base + juce::String("_") + juce::String(n))) ++n;
    return base + juce::String("_") + juce::String(n);
}

} // namespace

// ─── applyMovementPlan (FX & Automation choreography) ─────────────
// Batch section-aware movement across tracks in ONE undo unit: per event,
// resolve-or-create the lane (reuse the lane already bound to paramID — even
// when an agent supplied a new window-specific laneName — never stack two lanes
// on the same parameter), then write the named preset across
// the beat window via the shared preset-writer (clear=true replaces points
// inside the window and enables the lane). Partial failure keeps the good
// events; each event reports ok/error. Deterministic per-event seed.
AudioEngineCommands::MovementPlanResult AudioEngineCommands::applyMovementPlan(
    const std::vector<MovementEvent>& events)
{
    MovementPlanResult result;
    auto trackList = engine_.getProjectModel().getTrackListTree();

    beginTransaction("Movement plan");
    for (const auto& ev : events)
    {
        MovementEventResult res;
        res.ok = false;

        const auto fail = [&res](const std::string& msg) { res.error = msg; };
        if (ev.trackIndex < 0 || ev.trackIndex >= trackList.getNumChildren())
        {
            fail("track not found");
            result.events.push_back(res); ++result.failCount;
            continue;
        }
        const auto preset = HDAW::AutomationPreset::presetFromName(ev.preset);
        if (!preset)
        {
            fail("unknown preset: " + ev.preset);
            result.events.push_back(res); ++result.failCount;
            continue;
        }
        if (!(ev.endBeats > ev.startBeats))
        {
            fail("bad window: end must be > start");
            result.events.push_back(res); ++result.failCount;
            continue;
        }

        const int paramID = ev.paramID == -1 ? 1 : ev.paramID;
        auto track = trackList.getChild(ev.trackIndex);
        auto autoList = track.getChildWithName(IDs::AUTOMATION_LIST);

        juce::ValueTree lane;
        std::string laneName = ev.laneName;
        if (!laneName.empty())
        {
            lane = findAutomationLane(ev.trackIndex, laneName);
            if (!lane.isValid())
            {
                // Agent callers often name each movement window separately. If
                // the requested parameter already has a lane, reuse that lane
                // instead of failing with a duplicate-param create conflict.
                if (paramID != 0)
                {
                    lane = findLaneByParamID(autoList, paramID);
                    if (lane.isValid())
                        laneName = lane.getProperty(IDs::name, "").toString().toStdString();
                }
                if (!lane.isValid())
                {
                    if (!addAutomationLane(ev.trackIndex, laneName, paramID))
                    {
                        fail("lane create conflict: " + laneName);
                        result.events.push_back(res); ++result.failCount;
                        continue;
                    }
                    lane = findAutomationLane(ev.trackIndex, laneName);
                }
            }
            else
            {
                const int existingPid = static_cast<int>(lane.getProperty(IDs::paramID, 0));
                if (existingPid != 0 && paramID != 0 && existingPid != paramID)
                {
                    fail("paramID conflict on lane " + laneName);
                    result.events.push_back(res); ++result.failCount;
                    continue;
                }
            }
        }
        else
        {
            lane = findLaneByParamID(autoList, paramID);
            if (lane.isValid())
                laneName = lane.getProperty(IDs::name, "").toString().toStdString();
            else
            {
                laneName = firstFreeLaneName(
                    autoList, juce::String("movement-")
                        + juce::String(HDAW::AutomationPreset::presetName(*preset)))
                    .toStdString();
                if (!addAutomationLane(ev.trackIndex, laneName, paramID))
                {
                    fail("lane create failed: " + laneName);
                    result.events.push_back(res); ++result.failCount;
                    continue;
                }
                lane = findAutomationLane(ev.trackIndex, laneName);
            }
        }

        HDAW::AutomationPreset::PresetWindow w;
        w.start = ev.startBeats;
        w.end = ev.endBeats;
        w.preset = *preset;
        w.startValue = ev.startValue;
        w.endValue = ev.endValue;
        const int added = writePresetWindowsToLane(
            ev.trackIndex, lane, { w }, true, ev.seed);
        res.laneName = laneName;
        res.pointsWritten = added;
        res.ok = true;
        result.events.push_back(res);
        ++result.okCount;
    }
    endTransaction();
    return result;
}
