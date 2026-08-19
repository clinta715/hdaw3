#include "AudioEngineCommands.h"
#include "AudioEngineCommands_Helpers.h"
#include "AudioEngine.h"
#include "MainAudioProcessor.h"
#include "ExportManager.h"
#include "PhraseGenerator.h"
#include "../model/ProjectModel.h"
#include "../common/DebugLog.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{

// Map the composer's style-name strings (camelCase, no spaces) to the
// PhraseGenerator style enum. Returns false on unknown names.
bool styleFromName(const std::string& name, PhraseGenerator::Style& out)
{
    if      (name == "Standard")   { out = PhraseGenerator::Standard;   return true; }
    else if (name == "Arpeggio")   { out = PhraseGenerator::Arpeggio;   return true; }
    else if (name == "BassLine")   { out = PhraseGenerator::BassLine;   return true; }
    else if (name == "ChordStab")  { out = PhraseGenerator::ChordStab;  return true; }
    else if (name == "Pad")        { out = PhraseGenerator::Pad;        return true; }
    else if (name == "Lead")       { out = PhraseGenerator::Lead;       return true; }
    else if (name == "RandomWalk") { out = PhraseGenerator::RandomWalk; return true; }
    else if (name == "Buildup")    { out = PhraseGenerator::Buildup;    return true; }
    else if (name == "Euclidean")  { out = PhraseGenerator::Euclidean;  return true; }
    return false;
}

// Read a rendered WAV and compute its RMS (sqrt of mean sample^2 across all
// channels) and peak (max |sample|) over the whole file. Returns false when
// the file can't be read or is empty.
bool measureWav(juce::AudioFormatManager& fm, const juce::File& file,
                float& outRms, float& outPeak)
{
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;

    const int numChannels = static_cast<int>(reader->numChannels);
    const int numSamples = static_cast<int>(reader->lengthInSamples);
    juce::AudioBuffer<float> buf(numChannels, numSamples);
    reader->read(&buf, 0, numSamples, 0, true, true);

    double sumSq = 0.0;
    double count = 0.0;
    float peak = 0.0f;
    for (int c = 0; c < numChannels; ++c)
    {
        const float* data = buf.getReadPointer(c);
        for (int s = 0; s < numSamples; ++s)
        {
            const float v = data[s];
            sumSq += static_cast<double>(v) * static_cast<double>(v);
            count += 1.0;
            peak = std::max(peak, std::abs(v));
        }
    }
    if (count <= 0.0)
        return false;

    outRms = static_cast<float>(std::sqrt(sumSq / count));
    outPeak = peak;
    return true;
}

// Paint `copies` ghost copies of the root MIDI clip at lengthBeats spacing,
// starting at startBeat + lengthBeats. Mirrors paintClips' copy/offset/id
// allocation logic (AudioEngineCommands_GhostPaint.cpp) but WITHOUT its own
// beginTransaction and WITHOUT a rebuild — the caller owns both so the whole
// composite lands in a single undo unit with a single graph rebuild (lesson 6).
// Returns the new clip ids.
std::vector<int> paintGhostCopies(AudioEngine& engine, int trackIndex,
                                  int rootClipId, int copies,
                                  double lengthBeats, double startBeat)
{
    std::vector<int> ids;
    if (copies <= 0)
        return ids;

    auto& model = engine.getProjectModel();
    auto& um = model.getUndoManager();
    auto trackList = model.getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren())
        return ids;

    auto clipList = trackList.getChild(trackIndex).getChildWithName(IDs::CLIP_LIST);
    if (!clipList.isValid())
        return ids;

    int rootIdx = -1;
    for (int c = 0; c < clipList.getNumChildren(); ++c)
        if (static_cast<int>(clipList.getChild(c).getProperty(IDs::clipID, 0)) == rootClipId)
        {
            rootIdx = c;
            break;
        }
    if (rootIdx < 0)
        return ids;

    auto rootClip = clipList.getChild(rootIdx);
    auto rootNoteList = rootClip.getChildWithName(IDs::MIDI_NOTE_LIST);
    const double bpm = engine.getTransportManager().getBPM();
    const double factor = (bpm > 0) ? 60.0 / bpm : 1.0;

    for (int i = 0; i < copies; ++i)
    {
        const double copyStartBeat = startBeat + (i + 1) * lengthBeats;
        auto newClip = rootClip.createCopy();
        const int newId = ProjectModel::allocateClipID();
        newClip.setProperty(IDs::clipID, newId, &um);
        newClip.setProperty(IDs::ghostSourceId, rootClipId, &um);
        newClip.setProperty(IDs::isGhost, 1, &um);
        newClip.setProperty(IDs::startTime, copyStartBeat * factor, &um);

        // Remove the copied MIDI_NOTE_LIST; we re-add fresh notes with new ids.
        auto existingNoteList = newClip.getChildWithName(IDs::MIDI_NOTE_LIST);
        if (existingNoteList.isValid())
            newClip.removeChild(existingNoteList, &um);

        if (rootNoteList.isValid())
        {
            auto ghostNoteList = juce::ValueTree(IDs::MIDI_NOTE_LIST);
            newClip.addChild(ghostNoteList, -1, &um);
            for (int n = 0; n < rootNoteList.getNumChildren(); ++n)
            {
                auto noteCopy = rootNoteList.getChild(n).createCopy();
                noteCopy.setProperty(IDs::noteID, ProjectModel::allocateNoteID(), &um);
                ghostNoteList.addChild(noteCopy, -1, &um);
            }
        }

        clipList.addChild(newClip, -1, &um);
        ids.push_back(newId);
    }
    return ids;
}

} // namespace

// ─── ProjectCommands — instrument part composer ───────────────────

ProjectCommands::InstrumentPartResult AudioEngineCommands::addInstrumentPart(const InstrumentPartParams& params)
{
    InstrumentPartResult result;

    // Validate (Gate 9 — bounds-check every param at the command boundary).
    if (params.trackName.empty())
    {
        result.error = "trackName is required";
        return result;
    }
    PhraseGenerator::Style style;
    if (!styleFromName(params.style, style))
    {
        result.error = "unknown style: " + params.style;
        return result;
    }
    if (params.placement != "region" && params.placement != "wholeSong")
    {
        result.error = "placement must be 'region' or 'wholeSong'";
        return result;
    }
    if (params.count < 1)
    {
        result.error = "count must be >= 1";
        return result;
    }
    if (!(params.lengthBeats > 0.0))
    {
        result.error = "lengthBeats must be > 0";
        return result;
    }

    auto& model = engine_.getProjectModel();
    const double bpm = engine_.getTransportManager().getBPM();

    beginTransaction("Add instrument part");

    const int trackIndex = addTrack(params.trackName, -1, -1, 0);
    if (trackIndex < 0)
    {
        endTransaction();
        result.error = "failed to add track";
        return result;
    }

    // Instrument FX slot (internal fm_synth by default, or a hosted plugin).
    // addFxSlotInternal builds the slot tree without a per-op rebuild; the
    // single rebuildRoutingGraph at the end covers it (lesson 6).
    addFxSlotInternal(trackIndex, params.pluginId.empty() ? "fm_synth" : "plugin",
                      -1, params.pluginId);

    const int scaleRoot = (params.scaleRoot >= 0) ? params.scaleRoot : model.getScaleRoot();
    const int scaleMode = (params.scaleMode >= 0) ? params.scaleMode : model.getScaleMode();

    PhraseGenerator::PhraseParams pp;
    pp.style = style;
    pp.lengthBeats = params.lengthBeats;
    pp.density = params.density;
    pp.noteDuration = params.noteDuration;
    pp.scaleRoot = scaleRoot;
    pp.scaleMode = scaleMode;
    pp.lowNote = params.lowNote;
    pp.highNote = params.highNote;
    pp.minVelocity = params.minVelocity;
    pp.maxVelocity = params.maxVelocity;
    pp.seed = params.seed;

    const auto notes = PhraseGenerator::generatePhrase(pp);

    const int clipId = addMidiClip(trackIndex, params.startBeat, params.lengthBeats,
                                   "Part: " + params.trackName);
    if (clipId < 0)
    {
        endTransaction();
        result.error = "failed to add MIDI clip";
        return result;
    }
    result.clipIds.push_back(clipId);
    for (const auto& n : notes)
    {
        addNote(clipId, n.noteNumber, n.velocity, n.startBeat, n.durationBeats);
        ++result.noteCount;
    }

    // Placement — paint ghost copies inline in the SAME transaction so the
    // whole part is one undo unit and one graph rebuild.
    int copies = 0;
    if (params.placement == "region")
    {
        copies = params.count - 1;
    }
    else // wholeSong: cover the whole project with lengthBeats-spaced copies
    {
        const double projectDurSec = HDAW::ExportManager::calculateProjectDuration(model);
        const double projectDurBeats = (bpm > 0) ? projectDurSec * bpm / 60.0 : projectDurSec;
        copies = std::max(0, static_cast<int>(std::ceil(projectDurBeats / params.lengthBeats)) - 1);
    }
    if (copies > 0)
    {
        const auto copyIds = paintGhostCopies(engine_, trackIndex, clipId, copies,
                                              params.lengthBeats, params.startBeat);
        result.clipIds.insert(result.clipIds.end(), copyIds.begin(), copyIds.end());
    }

    rebuildRoutingGraph();
    endTransaction();

    result.trackIndex = trackIndex;

    // Optional gain staging — a SEPARATE undo unit ("Auto gain stage"), so
    // undo #1 removes the part and undo #2 removes the fader.
    if (params.targetRms > 0.0f)
        result.gain = autoGainToTarget(trackIndex, params.targetRms, params.windowSeconds, params.verify);

    return result;
}

ProjectCommands::GainStageResult AudioEngineCommands::autoGainToTarget(int trackIndex, float targetRms, double windowSeconds, bool verify)
{
    GainStageResult result;

    auto& model = engine_.getProjectModel();
    auto trackList = model.getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren())
    {
        result.error = "trackIndex out of range";
        return result;
    }
    if (!(targetRms > 0.0f))
    {
        result.error = "targetRms must be > 0";
        return result;
    }
    if (!(windowSeconds > 0.0))
    {
        result.error = "windowSeconds must be > 0";
        return result;
    }

    auto* proc = engine_.getMainProcessor();
    if (proc == nullptr)
    {
        result.error = "audio processor unavailable";
        return result;
    }
    auto& em = proc->getExportManager();
    if (em.isExporting())
    {
        result.error = "export already in progress";
        return result;
    }

    // Window start = the target track's earliest clip startTime (seconds).
    double windowStart = std::numeric_limits<double>::max();
    auto clipList = trackList.getChild(trackIndex).getChildWithName(IDs::CLIP_LIST);
    bool hasClip = false;
    if (clipList.isValid())
    {
        for (int c = 0; c < clipList.getNumChildren(); ++c)
        {
            const double s = static_cast<double>(clipList.getChild(c).getProperty(IDs::startTime, 0.0));
            windowStart = std::min(windowStart, s);
            hasClip = true;
        }
    }
    if (!hasClip)
    {
        result.error = "track has no clips";
        return result;
    }

    // Solo tree copy: only the target track renders, regardless of live
    // mute/solo state. The copy is never written back to the live tree.
    juce::ValueTree treeCopy = model.getTree().createCopy();
    auto copyTrackList = treeCopy.getChildWithName(IDs::TRACK_LIST);
    if (copyTrackList.isValid())
    {
        for (int t = 0; t < copyTrackList.getNumChildren(); ++t)
        {
            auto track = copyTrackList.getChild(t);
            track.setProperty(IDs::isSoloed, false, nullptr);
            if (t != trackIndex)
                track.setProperty(IDs::isMuted, true, nullptr);
        }
    }

    auto& fm = engine_.getProjectPool().getFormatManager();
    const juce::File tempFile =
        juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("hdaw_gainstage_" + juce::String(trackIndex) + ".wav");
    tempFile.deleteFile();

    if (!em.startExport(treeCopy, fm, &engine_.getPluginManager(), tempFile,
                        48000.0, windowStart, windowSeconds,
                        HDAW::ExportManager::WAV, 24))
    {
        result.error = "failed to start gain-stage render";
        return result;
    }

    // Block-wait for the bake + render. The message pump is a separate thread
    // so the render still completes (proven pattern from
    // export_bake_timeout_test.cpp).
    const uint32_t waitMs = HDAW::ExportManager::computeBakeWaitMs(treeCopy)
                            + static_cast<uint32_t>(windowSeconds * 1000.0) + 5000u;
    const auto deadline = juce::Time::getMillisecondCounter() + waitMs;
    while (em.isExporting() && juce::Time::getMillisecondCounter() < deadline)
        juce::Thread::sleep(10);
    if (em.isExporting())
    {
        em.cancel();
        result.error = "gain-stage render timed out";
        return result;
    }

    float measuredRms = 0.0f, measuredPeak = 0.0f;
    if (!measureWav(fm, tempFile, measuredRms, measuredPeak))
    {
        result.error = "failed to read gain-stage render";
        return result;
    }

    if (measuredRms <= 1e-6f)
    {
        result.error = "track is silent";
        return result;
    }

    float fader = targetRms / measuredRms;
    result.clamped = (fader > 1.0f);
    if (result.clamped)
        fader = 1.0f;
    result.fader = fader;

    beginTransaction("Auto gain stage");
    setTrackVolume(trackIndex, fader);
    endTransaction();

    if (verify)
    {
        // Re-render the same window with the fader applied, from a fresh tree
        // copy, and report the verified RMS/peak.
        juce::ValueTree verifyCopy = model.getTree().createCopy();
        auto vTrackList = verifyCopy.getChildWithName(IDs::TRACK_LIST);
        if (vTrackList.isValid())
        {
            for (int t = 0; t < vTrackList.getNumChildren(); ++t)
            {
                auto track = vTrackList.getChild(t);
                track.setProperty(IDs::isSoloed, false, nullptr);
                if (t != trackIndex)
                    track.setProperty(IDs::isMuted, true, nullptr);
                else
                    track.setProperty(IDs::volume, static_cast<double>(fader), nullptr);
            }
        }

        const juce::File verifyFile =
            juce::File::getSpecialLocation(juce::File::tempDirectory)
                .getChildFile("hdaw_gainstage_verify_" + juce::String(trackIndex) + ".wav");
        verifyFile.deleteFile();

        if (em.startExport(verifyCopy, fm, &engine_.getPluginManager(), verifyFile,
                           48000.0, windowStart, windowSeconds,
                           HDAW::ExportManager::WAV, 24))
        {
            const auto vDeadline = juce::Time::getMillisecondCounter()
                + HDAW::ExportManager::computeBakeWaitMs(verifyCopy)
                + static_cast<uint32_t>(windowSeconds * 1000.0) + 5000u;
            while (em.isExporting() && juce::Time::getMillisecondCounter() < vDeadline)
                juce::Thread::sleep(10);
            if (!em.isExporting())
            {
                float vRms = 0.0f, vPeak = 0.0f;
                if (measureWav(fm, verifyFile, vRms, vPeak))
                {
                    result.measuredRms = vRms;
                    result.peak = vPeak;
                }
            }
            verifyFile.deleteFile();
        }
    }
    else
    {
        result.measuredRms = measuredRms;
        result.peak = measuredPeak;
    }

    tempFile.deleteFile();
    result.ok = true;
    return result;
}
