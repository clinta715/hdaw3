#include "AudioEngineCommands.h"
#include "AudioEngine.h"
#include "LoopAnalyzer.h"
#include "../model/ProjectModel.h"
#include "../engine/StretchCache.h"
#include "../common/DebugLog.h"

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

AudioEngineCommands::AlignGridResult AudioEngineCommands::alignClipToGrid(int clipId)
{
    AlignGridResult result;
    beginTransaction("Align clip to grid");

    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid())
    {
        endTransaction();
        result.error = "clip not found";
        HDAW_LOG("AlignGrid", "alignClipToGrid: clip not found: " + juce::String(clipId));
        return result;
    }

    juce::String sourceFile = clip.getProperty(IDs::sourceFile).toString();
    if (sourceFile.isEmpty())
    {
        endTransaction();
        result.error = "no source file";
        HDAW_LOG("AlignGrid", "alignClipToGrid: clip has no source file: " + juce::String(clipId));
        return result;
    }

    HDAW::LoopAnalysis analysis = HDAW::LoopAnalyzer::analyze(
        sourceFile, engine_.getProjectPool().getFormatManager());
    if (!analysis.ok)
    {
        endTransaction();
        result.error = "could not detect a musical grid (too few onsets or low confidence)";
        HDAW_LOG("AlignGrid", "alignClipToGrid: grid detection failed for " + sourceFile
                 + ": " + juce::String(result.error));
        return result;
    }

    auto& um = engine_.getProjectModel().getUndoManager();
    double projectBpm = engine_.getProjectModel().getTree().getProperty(IDs::tempo, 120.0);
    double targetDuration = analysis.bars * analysis.beatsPerBar * (60.0 / projectBpm);
    double ratio = juce::jlimit(0.25, 4.0, targetDuration / analysis.loopSpanSourceSeconds);
    double duration = analysis.loopSpanSourceSeconds * ratio;
    double offset = analysis.downbeatOffset * ratio;

    // Write placement props first, then the stretch props LAST so the
    // stretchMode/stretchRatio listener rebuilds (AudioEngine.cpp) pick up the
    // final offset/duration, and the stretchRatio write adopts the buffer.
    clip.setProperty(IDs::sourceBpm, analysis.bpm, &um);
    clip.setProperty(IDs::offset, offset, &um);
    clip.setProperty(IDs::duration, duration, &um);
    clip.setProperty(IDs::stretchMode, 2, &um);
    clip.setProperty(IDs::stretchRatio, ratio, &um);

    endTransaction();

    result.ok = true;
    result.bpm = analysis.bpm;
    result.confidence = analysis.confidence;
    result.bars = analysis.bars;
    result.beatsPerBar = analysis.beatsPerBar;
    result.ratio = ratio;
    result.offset = offset;
    result.duration = duration;
    return result;
}
