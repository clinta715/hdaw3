#include "AudioEngineCommands.h"
#include "AudioEngine.h"
#include "../model/ProjectModel.h"
#include "../engine/StretchCache.h"

void AudioEngineCommands::setClipSourceBpm(int clipId, double bpm)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (clip.isValid())
        clip.setProperty(IDs::sourceBpm, juce::jmax(0.0, bpm), &um);
}

void AudioEngineCommands::setClipStretchMode(int clipId, int mode)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;

    int clamped = juce::jlimit(0, 2, mode);
    clip.setProperty(IDs::stretchMode, clamped, &um);

    // When switching to TempoMatch, derive the ratio immediately from the
    // clip's sourceBpm and the project tempo so the UI/route reflect it.
    if (clamped == 1)
    {
        double sourceBpm = clip.getProperty(IDs::sourceBpm, 0.0);
        if (sourceBpm > 0.0)
        {
            double projectBpm = engine_.getProjectModel().getTree().getProperty(IDs::tempo, 120.0);
            double ratio = sourceBpm / projectBpm;
            double sourceDur = clip.getProperty(IDs::sourceDuration, 0.0);
            clip.setProperty(IDs::stretchRatio, ratio, &um);
            if (sourceDur > 0.0)
                clip.setProperty(IDs::duration, sourceDur * ratio, &um);
        }
    }
    else if (clamped == 0)
    {
        // Off: restore duration to the original source length.
        double sourceDur = clip.getProperty(IDs::sourceDuration, 0.0);
        clip.setProperty(IDs::stretchRatio, 1.0, &um);
        if (sourceDur > 0.0)
            clip.setProperty(IDs::duration, sourceDur, &um);
    }
}

void AudioEngineCommands::setClipStretchRatio(int clipId, double ratio)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;

    double clamped = juce::jlimit(0.25, 4.0, ratio);
    clip.setProperty(IDs::stretchRatio, clamped, &um);

    // Keep the timeline-visible duration consistent with the new ratio.
    double sourceDur = clip.getProperty(IDs::sourceDuration, 0.0);
    if (sourceDur > 0.0)
        clip.setProperty(IDs::duration, sourceDur * clamped, &um);
}

void AudioEngineCommands::tempoMatchClip(int clipId)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;

    double sourceBpm = clip.getProperty(IDs::sourceBpm, 0.0);
    if (sourceBpm <= 0.0)
        return; // can't tempo-match without a known source tempo

    double projectBpm = engine_.getProjectModel().getTree().getProperty(IDs::tempo, 120.0);
    if (projectBpm <= 0.0) return;

    double ratio = sourceBpm / projectBpm;
    double sourceDur = clip.getProperty(IDs::sourceDuration, 0.0);
    clip.setProperty(IDs::stretchMode, 1, &um);
    clip.setProperty(IDs::stretchRatio, ratio, &um);
    if (sourceDur > 0.0)
        clip.setProperty(IDs::duration, sourceDur * ratio, &um);
}

void AudioEngineCommands::fitClipToLoop(int clipId)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;

    auto transport = engine_.getProjectModel().getTransportTree();
    double loopStart = transport.getProperty(IDs::loopStart, 0.0);
    double loopEnd = transport.getProperty(IDs::loopEnd, 0.0);
    double loopLen = loopEnd - loopStart;
    if (loopLen <= 0.0)
        return; // no valid loop region

    double sourceDur = clip.getProperty(IDs::sourceDuration, 0.0);
    if (sourceDur <= 0.0)
        return;

    double ratio = loopLen / sourceDur;
    clip.setProperty(IDs::stretchMode, 2, &um); // ManualRatio
    clip.setProperty(IDs::stretchRatio, ratio, &um);
    clip.setProperty(IDs::duration, loopLen, &um);
    clip.setProperty(IDs::offset, 0.0, &um);
}
