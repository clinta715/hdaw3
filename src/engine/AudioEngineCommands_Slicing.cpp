#include "AudioEngineCommands.h"
#include "../model/ProjectModel.h"
#include "../engine/AudioImport.h"
#include "AudioEngine.h"
#include "TransientDetector.h"
#include "RegionClipboard.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>

// ─── ProjectCommands — Slicing ─────────────────────────────────

void AudioEngineCommands::sliceClipAtTimes(int clipId, const std::vector<double>& times)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;

    auto slices = ProjectModel::sliceClipAtTimes(clip, times, &um);
    
    // Rebuild routing for new clips
    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildRoutingGraph();
}

void AudioEngineCommands::sliceClipAtTransients(int clipId)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;

    juce::String sourceFile = clip.getProperty(IDs::sourceFile);
    if (sourceFile.isEmpty()) return;

    // Load the source file into a buffer for synchronous detection
    auto* pool = &engine_.getProjectPool();
    auto* fm = &pool->getFormatManager();
    
    auto reader = std::unique_ptr<juce::AudioFormatReader>(fm->createReaderFor(juce::File(sourceFile)));
    if (!reader) return;
    
    juce::AudioBuffer<float> buffer(reader->numChannels, static_cast<int>(reader->lengthInSamples));
    reader->read(&buffer, 0, static_cast<int>(reader->lengthInSamples), 0, true, true);

    // Run transient detection synchronously
    HDAW::TransientDetector detector;
    auto result = detector.detect(buffer, reader->sampleRate);

    if (result.transientTimes.empty()) return;

    // TransientDetector returns times relative to the SOURCE FILE's sample 0,
    // but sliceClipAtTimes expects timeline-absolute times (it compares
    // against clipStart..clipEnd). Map each transient into the clip's
    // timeline frame: timelineTime = clipStart + (transientTime - offset).
    // `offset` is the inaudible file head skipped before the clip's audible
    // region, so we only keep transients that fall inside the audible window
    // [clipStart, clipEnd]. Without this mapping every transient is < clipStart
    // (for clips not at timeline 0) and gets filtered out — slicing silently
    // does nothing.
    double clipStart = clip.getProperty(IDs::startTime);
    double clipDur = clip.getProperty(IDs::duration);
    double clipOffset = clip.getProperty(IDs::offset);
    double clipEnd = clipStart + clipDur;

    std::vector<double> timelineTimes;
    timelineTimes.reserve(result.transientTimes.size());
    for (double ft : result.transientTimes)
    {
        double t = clipStart + (ft - clipOffset);
        if (t > clipStart && t < clipEnd)
            timelineTimes.push_back(t);
    }
    if (timelineTimes.empty()) return;

    // Slice at detected transients
    auto slices = ProjectModel::sliceClipAtTimes(clip, timelineTimes, &um);
    
    // Rebuild routing for new clips
    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildRoutingGraph();
}

void AudioEngineCommands::sliceClipAtPlayhead(int clipId)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid()) return;

    double playhead = engine_.getTransportManager().getCurrentPositionSeconds();
    double clipStart = static_cast<double>(clip.getProperty(IDs::startTime));
    double clipEnd = clipStart + static_cast<double>(clip.getProperty(IDs::duration));
    
    if (playhead <= clipStart || playhead >= clipEnd) return;
    
    auto slices = ProjectModel::sliceClipAtTimes(clip, {playhead}, &um);
    
    // Rebuild routing for new clips
    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildRoutingGraph();
}

// ─── ProjectCommands — Region cut/copy/paste ──────────────────

int AudioEngineCommands::copyAudioClipRegion(int clipId, double regionStart, double regionEnd)
{
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid() || trackIdx < 0) return -1;

    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIdx >= trackList.getNumChildren()) return -1;

    // Validate the region against the clip's audible bounds. regionStart/End
    // are offsets within the clip (0 = clip start). Clamp then reject empty
    // regions so a garbage selection can't store a negative-duration or
    // out-of-source region that misbehaves on paste.
    double clipDur = clip.getProperty(IDs::duration, 0.0);
    double rs = std::clamp(regionStart, 0.0, clipDur);
    double re = std::clamp(regionEnd, 0.0, clipDur);
    if (re <= rs) return -1;

    juce::String sourceFile = clip.getProperty(IDs::sourceFile).toString();
    double clipOffset = clip.getProperty(IDs::offset, 0.0);
    double regOffset = clipOffset + rs;
    double regDuration = re - rs;

    HDAW::RegionClipboard::store({sourceFile, regOffset, regDuration});
    return 0;
}

int AudioEngineCommands::cutAudioClipRegion(int clipId, double regionStart, double regionEnd)
{
    if (regionEnd <= regionStart) return -1;

    copyAudioClipRegion(clipId, regionStart, regionEnd);

    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid() || trackIdx < 0) return -1;

    auto& um = engine_.getProjectModel().getUndoManager();
    double startTime = clip.getProperty(IDs::startTime, 0.0);
    double slice1 = startTime + regionStart;
    double slice2 = startTime + regionEnd;

    // sliceClipAtTimes returns the created slices in order. Cutting a region
    // produces up to three slices [head, middle(=the cut region), tail]; the
    // one to delete is the middle slice whose startTime == slice1. We identify
    // it by the returned slice identities rather than by a fuzzy startTime
    // match, which is fragile when multiple clips share near-equal start times.
    auto slices = ProjectModel::sliceClipAtTimes(clip, {slice1, slice2}, &um);

    // Find the slice that starts at slice1 (the cut region) and remove it.
    for (const auto& s : slices)
    {
        double st = s.getProperty(IDs::startTime, 0.0);
        if (std::abs(st - slice1) < 1e-6)
        {
            s.getParent().removeChild(s, &um);
            break;
        }
    }

    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildRoutingGraph();
    return 0;
}

int AudioEngineCommands::pasteAudioClipRegion(int clipId, double pasteTime)
{
    // NOTE: `clipId` is a track LOCATOR (we paste into the source clip's
    // track), not the paste target. The pasted clip is created as a new clip
    // at the absolute timeline `pasteTime` with the cached region's
    // sourceFile/offset/duration. Callers (AudioClipEditorWidget::onPasteRegion)
    // are responsible for choosing a sensible pasteTime.
    if (!HDAW::RegionClipboard::hasContent()) return -1;

    int trackIdx = -1;
    auto srcClip = findClipById(clipId, trackIdx);
    if (!srcClip.isValid() || trackIdx < 0) return -1;

    const auto& reg = HDAW::RegionClipboard::get();
    juce::String clipName = srcClip.getProperty(IDs::name).toString();
    juce::String newName = clipName + " (pasted)";

    int newId = addAudioClip(trackIdx, pasteTime, reg.duration,
                             reg.sourceFile.toStdString(), newName.toStdString());
    if (newId < 0) return -1;

    int newTrackIdx = -1;
    auto newClip = findClipById(newId, newTrackIdx);
    if (newClip.isValid()) {
        auto& um = engine_.getProjectModel().getUndoManager();
        newClip.setProperty(IDs::offset, reg.offset, &um);
    }

    engine_.getMainProcessor()->rebuildRoutingGraph();
    return newId;
}
