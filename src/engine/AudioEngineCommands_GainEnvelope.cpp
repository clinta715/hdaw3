#include "AudioEngineCommands.h"
#include "AudioEngine.h"
#include "MainAudioProcessor.h"
#include "../model/ProjectModel.h"

void AudioEngineCommands::addGainEnvelopePoint(int clipId, double time, double gain)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;

    auto envelope = ProjectModel::ensureGainEnvelope(clip, &um);
    ProjectModel::addGainEnvelopePoint(envelope, time, gain, &um);
    notifyClipGainEnvelopeChanged(clipId);
}

void AudioEngineCommands::moveGainEnvelopePoint(int clipId, int pointIndex, double time, double gain)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;

    auto envelope = clip.getChildWithName(IDs::GAIN_ENVELOPE);
    if (!envelope.isValid() || pointIndex < 0 || pointIndex >= envelope.getNumChildren()) return;

    ProjectModel::removeGainEnvelopePoint(envelope, pointIndex, &um);
    ProjectModel::addGainEnvelopePoint(envelope, time, gain, &um);
    notifyClipGainEnvelopeChanged(clipId);
}

void AudioEngineCommands::removeGainEnvelopePoint(int clipId, int pointIndex)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;

    auto envelope = clip.getChildWithName(IDs::GAIN_ENVELOPE);
    ProjectModel::removeGainEnvelopePoint(envelope, pointIndex, &um);
    notifyClipGainEnvelopeChanged(clipId);
}

void AudioEngineCommands::clearGainEnvelope(int clipId)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;

    auto envelope = clip.getChildWithName(IDs::GAIN_ENVELOPE);
    if (envelope.isValid())
        clip.removeChild(envelope, &um);
    notifyClipGainEnvelopeChanged(clipId);
}

void AudioEngineCommands::setClipGainEnvelope(int clipId,
                                              const std::vector<std::pair<double, double>>& points)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;

    um.beginNewTransaction("setClipGainEnvelope");
    auto envelope = clip.getChildWithName(IDs::GAIN_ENVELOPE);
    if (envelope.isValid())
        clip.removeChild(envelope, &um);
    envelope = ProjectModel::ensureGainEnvelope(clip, &um);
    for (const auto& [time, gain] : points)
        ProjectModel::addGainEnvelopePoint(envelope, time, gain, &um);
    notifyClipGainEnvelopeChanged(clipId);
}

void AudioEngineCommands::notifyClipGainEnvelopeChanged(int clipId)
{
    auto* proc = engine_.getMainProcessor();
    if (proc)
    {
        auto points = getGainEnvelopePoints(clipId);
        std::vector<HDAW::ClipSourceProcessor::GainPoint> pointsToSend;
        pointsToSend.reserve(points.size());
        for (const auto& p : points)
            pointsToSend.push_back({p.time, p.gain});
        proc->updateClipGainEnvelope(clipId, pointsToSend);
    }
}
