#include "AudioEngineCommands.h"
#include "AudioEngineCommands_Helpers.h"
#include "AudioEngine.h"
#include "MainAudioProcessor.h"
#include "../model/ProjectModel.h"
#include "EnvelopeGenerator.h"
#include <cmath>

// ─── setClipCcPoints (private bulk writer) ────────────────────────

void AudioEngineCommands::setClipCcPoints(int clipId, int controllerNumber,
                                           const std::vector<std::pair<double, double>>& points)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;

    um.beginNewTransaction("setClipCcPoints");

    auto ccList = clip.getChildWithName(IDs::CC_LIST);
    if (!ccList.isValid())
    {
        ccList = juce::ValueTree(IDs::CC_LIST);
        clip.addChild(ccList, -1, nullptr);
    }

    // Compute range from points to remove existing CC_POINTs in that range.
    if (!points.empty())
    {
        double minBeat = points.front().first;
        double maxBeat = points.front().first;
        for (const auto& [t, v] : points)
        {
            if (t < minBeat) minBeat = t;
            if (t > maxBeat) maxBeat = t;
        }

        // Remove existing points for this controller in [minBeat, maxBeat].
        for (int i = ccList.getNumChildren() - 1; i >= 0; --i)
        {
            auto pt = ccList.getChild(i);
            if (static_cast<int>(pt.getProperty(IDs::controllerNumber, -1)) == controllerNumber)
            {
                double beat = static_cast<double>(pt.getProperty(IDs::beat, 0.0));
                if (beat >= minBeat && beat <= maxBeat)
                    ccList.removeChild(i, &um);
            }
        }
    }

    // Add new points.
    for (const auto& [time, value] : points)
    {
        juce::ValueTree pt(IDs::CC_POINT);
        pt.setProperty(IDs::ccID, engine_.getProjectModel().allocateCcID(), nullptr);
        pt.setProperty(IDs::controllerNumber, controllerNumber, &um);
        pt.setProperty(IDs::beat, time, &um);
        pt.setProperty(IDs::value, static_cast<int>(std::round(value)), &um);
        ccList.addChild(pt, -1, &um);
    }
}

// ─── generateAutomationEnvelope ────────────────────────────────────

void AudioEngineCommands::generateAutomationEnvelope(int trackIndex, const std::string& lane,
                                                      const HDAW::EnvelopeGenerator::Params& params)
{
    auto* proc = engine_.getMainProcessor();
    if (!proc) return;

    auto autoLane = findAutomationLane(trackIndex, lane);
    if (!autoLane.isValid()) return;

    double bpm = engine_.getProjectModel().getTree().getProperty(IDs::tempo, 120.0);

    // Convert params.startTime/endTime from beats to seconds for the tree.
    double startSec = HDAW::beatsToSeconds(params.startTime, bpm);
    double endSec = HDAW::beatsToSeconds(params.endTime, bpm);

    // Prepare generator params with seconds-based time axis.
    auto genParams = params;
    genParams.startTime = startSec;
    genParams.endTime = endSec;

    // Generate points (generator domain is 0..1, automation domain is 0..1 — no scaling).
    auto generated = HDAW::EnvelopeGenerator::generate(genParams);

    auto& um = engine_.getProjectModel().getUndoManager();
    um.beginNewTransaction("generate envelope");

    auto pointList = autoLane.getChildWithName(IDs::POINT_LIST);
    if (pointList.isValid())
    {
        // Remove existing POINTs in [startSec, endSec] (inclusive).
        for (int i = pointList.getNumChildren() - 1; i >= 0; --i)
        {
            auto pt = pointList.getChild(i);
            double t = static_cast<double>(pt.getProperty(IDs::startTime, 0.0));
            if (t >= startSec && t <= endSec)
                pointList.removeChild(i, &um);
        }
    }
    else
    {
        pointList = juce::ValueTree(IDs::POINT_LIST);
        autoLane.addChild(pointList, -1, nullptr);
    }

    // Add generated points.
    for (const auto& [time, value] : generated)
    {
        juce::ValueTree point(IDs::POINT);
        point.setProperty(IDs::startTime, time, nullptr);
        point.setProperty(IDs::gain, value, nullptr);
        pointList.addChild(point, -1, &um);
    }

    proc->rebuildAutomationCache(trackIndex);
}

// ─── generateClipGainEnvelope ─────────────────────────────────────

void AudioEngineCommands::generateClipGainEnvelope(int clipId,
                                                    const HDAW::EnvelopeGenerator::Params& params)
{
    auto* proc = engine_.getMainProcessor();
    if (!proc) return;

    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;

    double bpm = engine_.getProjectModel().getTree().getProperty(IDs::tempo, 120.0);

    // Convert params.startTime/endTime from beats to seconds.
    double startSec = HDAW::beatsToSeconds(params.startTime, bpm);
    double endSec = HDAW::beatsToSeconds(params.endTime, bpm);

    // Prepare generator params with seconds-based time axis.
    auto genParams = params;
    genParams.startTime = startSec;
    genParams.endTime = endSec;

    // Scale domain: editor 0..2 ↔ generator 0..1.
    genParams.startValue = params.startValue / 2.0;
    genParams.endValue = params.endValue / 2.0;

    auto generated = HDAW::EnvelopeGenerator::generate(genParams);

    // Scale back to editor domain (0..2).
    std::vector<std::pair<double, double>> scaledPoints;
    scaledPoints.reserve(generated.size());
    for (const auto& [t, v] : generated)
        scaledPoints.emplace_back(t, v * 2.0);

    auto& um = engine_.getProjectModel().getUndoManager();
    um.beginNewTransaction("generate clip gain envelope");

    // Remove existing GAIN_ENVELOPE child.
    auto envelope = clip.getChildWithName(IDs::GAIN_ENVELOPE);
    if (envelope.isValid())
        clip.removeChild(envelope, &um);

    // Create new envelope and add points (times already in seconds).
    envelope = ProjectModel::ensureGainEnvelope(clip, &um);
    for (const auto& [time, gain] : scaledPoints)
        ProjectModel::addGainEnvelopePoint(envelope, time, gain, &um);

    notifyClipGainEnvelopeChanged(clipId);
}

// ─── generateClipCcLane ───────────────────────────────────────────

void AudioEngineCommands::generateClipCcLane(int clipId, int controllerNumber,
                                              const HDAW::EnvelopeGenerator::Params& params)
{
    // CC times stay in beats — NO beats↔seconds conversion.
    // Generator time axis is unit-agnostic, so pass params.startTime/endTime directly.
    auto genParams = params;

    // Scale domain: CC 0..127 int ↔ generator 0..1.
    genParams.startValue = params.startValue / 127.0;
    genParams.endValue = params.endValue / 127.0;

    auto generated = HDAW::EnvelopeGenerator::generate(genParams);

    // Scale back to CC domain (0..127, rounded).
    std::vector<std::pair<double, double>> scaledPoints;
    scaledPoints.reserve(generated.size());
    for (const auto& [t, v] : generated)
        scaledPoints.emplace_back(t, std::round(v * 127.0));

    setClipCcPoints(clipId, controllerNumber, scaledPoints);
}
