#include "AudioEngineCommands.h"
#include "AudioEngineCommands_Helpers.h"
#include "AudioEngine.h"
#include "MainAudioProcessor.h"
#include "MidiClipProcessor.h"
#include "ExportManager.h"
#include "PhraseGenerator.h"
#include "RhythmPatternGenerator.h"
#include "PsytranceGenerator.h"
#include "PsytranceMarkovGenerator.h"
#include "../model/ProjectModel.h"
#include "../common/DebugLog.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <limits>
#include <memory>
#include <string>

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
    else if (name == "Percussion") { out = PhraseGenerator::Percussion; return true; }
    return false;
}

// ASCII lowercase for role normalization (role names are ASCII).
std::string toLowerAscii(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// ── Volume-fade lane writing (P0 psytrance ontology) ─────────────────────

// True when the lane's point list is EXACTLY the untouched factory pair the
// default project ships on every Volume lane (hold points 0s→1.0 and
// 16s→1.0). Those placeholders exist to hold the lane at unity when enabled;
// left in place they would ramp a generated fade-out back to 1.0 at 16s, so
// they are dropped before the first generated fade lands.
bool isUntouchedFactoryVolumePoints(const juce::ValueTree& pointList)
{
    if (!pointList.isValid() || pointList.getNumChildren() != 2) return false;
    auto isHold = [](const juce::ValueTree& pt, double t) {
        return std::abs(static_cast<double>(pt.getProperty(IDs::startTime, -1.0)) - t) < 1e-9
            && std::abs(static_cast<double>(pt.getProperty(IDs::gain, -1.0)) - 1.0) < 1e-9;
    };
    auto p0 = pointList.getChild(0);
    auto p1 = pointList.getChild(1);
    return (isHold(p0, 0.0) && isHold(p1, 16.0)) || (isHold(p0, 16.0) && isHold(p1, 0.0));
}

// Upsert one automation point (SECONDS domain) onto a POINT_LIST: a point at
// the exact same time is re-valued, otherwise the point is APPENDED — points
// accumulate across add/remove cycles (the runtime cache sorts by time;
// AutomationManager::rebuildCache). Same property/conversion contract as the
// add_automation_point RPC path.
void upsertAutomationPoint(juce::ValueTree pointList, double timeSec, double value,
                           juce::UndoManager& um)
{
    for (int i = 0; i < pointList.getNumChildren(); ++i)
    {
        auto pt = pointList.getChild(i);
        if (static_cast<double>(pt.getProperty(IDs::startTime, 0.0)) == timeSec)
        {
            pt.setProperty(IDs::gain, value, &um);
            return;
        }
    }
    juce::ValueTree pt(IDs::POINT);
    pt.setProperty(IDs::startTime, timeSec, nullptr);
    pt.setProperty(IDs::gain, value, nullptr);
    pointList.addChild(pt, -1, &um);
}

// Role → typed-preset defaults for the 9 role-defaultable InstrumentPartParams
// fields. Indexed 0=bass, 1=lead, 2=chords, 3=drums. A field whose explicitMask
// bit is NOT set receives its role default; explicit values always win.
struct RoleDefaults
{
    const char* style;
    int lowNote;
    int highNote;
    int density;
    double noteDuration;
    int minVelocity;
    int maxVelocity;
    float targetRms;
    bool allowGlobalScale;
};

const RoleDefaults kRoleDefaults[] = {
    //        style       low high den dur   min max rms     scale
    { "BassLine",  36,  48, 10, 0.5,  70, 110, 0.126f, true  }, // bass
    { "Lead",      60,  76,  6, 0.25, 70, 110, 0.0f,   false }, // lead
    { "ChordStab", 48,  72,  5, 2.0,  60, 100, 0.0f,   false }, // chords
    { "Euclidean", 36,  60, 12, 0.25, 90, 120, 0.0f,   false }, // drums
    // NOTE: FM synth role presets are deliberately deferred. The DX7 init
    // patch (all-99 EG, full output level) is velocity-insensitive — velocity
    // ranges in the role defaults above affect note velocity but the synth
    // ignores it. Shipping guessed DX7 algorithms without timbre-testing
    // would produce unreliable results. See handoff #5, item 3.
};

int roleIndex(const std::string& role)
{
    if      (role == "bass")   return 0;
    else if (role == "lead")   return 1;
    else if (role == "chords") return 2;
    else if (role == "drums")  return 3;
    return -1;
}

// Spectral band presence for verifyPart (low/mid/high energy fractions).
struct BandPresence { bool low = false, mid = false, high = false; };

constexpr int kBandFftOrder = 12;            // 2^12 = 4096-point FFT
constexpr int kBandFftSize = 1 << kBandFftOrder;

// Read a rendered WAV and compute its RMS (sqrt of mean sample^2 across all
// channels) and peak (max |sample|) over the whole file. Returns false when
// the file can't be read or is empty. When outBands != nullptr, also analyses
// spectral band presence (offline FFT on the command thread — same pattern as
// FileLibraryManager's key detection).
bool measureWav(juce::AudioFormatManager& fm, const juce::File& file,
                float& outRms, float& outPeak, BandPresence* outBands = nullptr)
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

    if (outBands != nullptr && peak > 1e-6f && numSamples >= kBandFftSize)
    {
        std::vector<float> mono(static_cast<size_t>(numSamples), 0.0f);
        for (int c = 0; c < numChannels; ++c)
        {
            const float* data = buf.getReadPointer(c);
            for (int s = 0; s < numSamples; ++s)
                mono[static_cast<size_t>(s)] += data[s];
        }
        const float invCh = 1.0f / static_cast<float>(numChannels);
        for (auto& v : mono)
            v *= invCh;

        juce::dsp::FFT fft(kBandFftOrder);
        std::vector<float> fftBuffer(kBandFftSize * 2, 0.0f);
        std::vector<float> window(kBandFftSize);
        for (int i = 0; i < kBandFftSize; ++i)
            window[static_cast<size_t>(i)] = 0.5f * (1.0f - std::cos(2.0 * juce::MathConstants<float>::pi * i / (kBandFftSize - 1)));

        const double sampleRate = reader->sampleRate;
        const double highTop = std::min(20000.0, sampleRate * 0.5);
        double lowE = 0.0, midE = 0.0, highE = 0.0, totalE = 0.0;

        const int space = numSamples - kBandFftSize;
        const int frames = std::min(8, std::max(1, space + 1));
        for (int f = 0; f < frames; ++f)
        {
            const int offset = (frames <= 1) ? 0 : (space * f) / (frames - 1);
            for (int i = 0; i < kBandFftSize; ++i)
                fftBuffer[static_cast<size_t>(i)] = mono[static_cast<size_t>(offset + i)] * window[static_cast<size_t>(i)];
            std::fill(fftBuffer.begin() + kBandFftSize, fftBuffer.end(), 0.0f);
            fft.performRealOnlyForwardTransform(fftBuffer.data());
            for (int bin = 1; bin < kBandFftSize / 2; ++bin)
            {
                const double re = fftBuffer[static_cast<size_t>(bin * 2)];
                const double im = fftBuffer[static_cast<size_t>(bin * 2 + 1)];
                const double magSq = re * re + im * im;
                totalE += magSq;
                const double freq = static_cast<double>(bin) * sampleRate / kBandFftSize;
                if (freq >= 20.0 && freq < 250.0)
                    lowE += magSq;
                else if (freq >= 250.0 && freq < 4000.0)
                    midE += magSq;
                else if (freq >= 4000.0 && freq <= highTop)
                    highE += magSq;
            }
        }

        const bool enough = totalE > 1e-12;
        outBands->low = enough && lowE > 0.005 * totalE;
        outBands->mid = enough && midE > 0.005 * totalE;
        outBands->high = enough && highE > 0.005 * totalE;
    }

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
        const int newId = model.allocateClipID();
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
                noteCopy.setProperty(IDs::noteID, model.allocateNoteID(), &um);
                ghostNoteList.addChild(noteCopy, -1, &um);
            }
        }

        clipList.addChild(newClip, -1, &um);
        ids.push_back(newId);
    }
    return ids;
}

struct RenderWindowResult
{
    juce::File wavPath;   // only set on success (after measureWav); caller owns deletion
    float rms = 0.0f;
    float peak = 0.0f;
    double windowStart = 0.0;   // seconds — the target track's earliest clip start
    std::string error;
};

// Unique temp render target for a track window. The counter keeps consecutive
// (and concurrent) renders from colliding on the same filename.
std::atomic<int> s_renderCounter{ 0 };

// The shared solo-render + measure loop (handoff #5): renders the target
// track's clips over [windowStart, windowStart + windowSeconds) from a tree
// copy and measures the rendered WAV. With `soloMuteOthers` (default) every
// other track is muted and all solos cleared — a SOLO render; with it false,
// solos are cleared but live mutes are kept — a FULL-MIX render (verifyPart).
// `fader` is applied to the target track's volume only when `applyFader` is
// true. `masterScale` (!= 1.0) multiplies the tree copy's root masterGain —
// an attenuated probe so a clipping mix's TRUE peak survives the 24-bit WAV
// clamp (anything >= 1.0 writes as full scale and reads back exactly 1.0,
// hiding how far over the mix is). Never mutates the live graph (Gate 12).
// Errors are reported via `error`; the caller owns deleting `wavPath`.
RenderWindowResult renderTrackWindow(AudioEngine& engine, int trackIndex,
                                     double windowSeconds, float fader,
                                     bool applyFader, bool soloMuteOthers = true,
                                     BandPresence* outBands = nullptr,
                                     float masterScale = 1.0f)
{
    RenderWindowResult result;

    auto& model = engine.getProjectModel();
    auto trackList = model.getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren())
    {
        result.error = "trackIndex out of range";
        return result;
    }
    if (!(windowSeconds > 0.0))
    {
        result.error = "windowSeconds must be > 0";
        return result;
    }

    auto* proc = engine.getMainProcessor();
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
    result.windowStart = windowStart;

    // Tree copy: solos always cleared; other tracks muted only in solo mode.
    // The copy is never written back to the live tree.
    juce::ValueTree treeCopy = model.getTree().createCopy();
    auto copyTrackList = treeCopy.getChildWithName(IDs::TRACK_LIST);
    if (copyTrackList.isValid())
    {
        for (int t = 0; t < copyTrackList.getNumChildren(); ++t)
        {
            auto track = copyTrackList.getChild(t);
            track.setProperty(IDs::isSoloed, false, nullptr);
            if (t != trackIndex)
            {
                if (soloMuteOthers)
                    track.setProperty(IDs::isMuted, true, nullptr);
            }
            else if (applyFader)
                track.setProperty(IDs::volume, static_cast<double>(fader), nullptr);
        }
    }

    if (masterScale != 1.0f)
    {
        const double current = static_cast<double>(treeCopy.getProperty(IDs::masterGain, 1.0));
        treeCopy.setProperty(IDs::masterGain, current * static_cast<double>(masterScale), nullptr);
    }

    auto& fm = engine.getProjectPool().getFormatManager();
    const juce::File tempFile =
        juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("hdaw_render_" + juce::String(trackIndex) + "_"
                          + juce::String(s_renderCounter.fetch_add(1)) + ".wav");
    tempFile.deleteFile();

    // Drain pending live-graph rebuilds before the windowed render starts.
    // AudioEngine coalesces ValueTree->graph rebuilds into an AsyncUpdater
    // serviced on the message pump; a mutating command running on a
    // non-message thread (addTrack / addAudioClip / ...) returns before that
    // rebuild executes, and if it lands while a render is in flight,
    // rebuildRoutingGraph drains and cancels the export (AutoGain read-back
    // failure: "Export cancelled.", no output file). Post a probe to the same
    // queue and wait for it: FIFO dispatch guarantees every rebuild queued
    // before the probe has been applied by the time it fires. Skipped on the
    // message thread itself (a posted probe could never be dispatched while we
    // block it), where rebuilds are already serialized with commands.
    if (juce::MessageManager::getInstance() != nullptr
        && !juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        auto graphSettled = std::make_shared<std::atomic<bool>>(false);
        struct GraphSettleProbe final : public juce::CallbackMessage
        {
            std::shared_ptr<std::atomic<bool>> settled;
            explicit GraphSettleProbe(std::shared_ptr<std::atomic<bool>> s)
                : settled(std::move(s)) {}
            void messageCallback() override
            {
                settled->store(true, std::memory_order_release);
            }
        };
        (new GraphSettleProbe(graphSettled))->post();
        const auto settleDeadline = juce::Time::getMillisecondCounter() + 2000u;
        while (!graphSettled->load(std::memory_order_acquire)
               && juce::Time::getMillisecondCounter() < settleDeadline)
            juce::Thread::sleep(1);
    }

    if (!em.startExport(treeCopy, fm, &engine.getPluginManager(), tempFile,
                        48000.0, windowStart, windowSeconds,
                        HDAW::ExportManager::WAV, 24))
    {
        result.error = "failed to start render";
        return result;
    }

    // Probe for any graph rebuilds that may have been queued during the
    // export start-up window. Mirrors the pre-export settle above; without
    // this, a rebuild arriving after startExport can cancel the export
    // mid-bake via MainAudioProcessor::rebuildRoutingGraph → cancelAndJoin.
    if (juce::MessageManager::getInstance() != nullptr
        && !juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        auto graphSettled = std::make_shared<std::atomic<bool>>(false);
        struct GraphSettleProbe final : public juce::CallbackMessage
        {
            std::shared_ptr<std::atomic<bool>> settled;
            explicit GraphSettleProbe(std::shared_ptr<std::atomic<bool>> s)
                : settled(std::move(s)) {}
            void messageCallback() override
            {
                settled->store(true, std::memory_order_release);
            }
        };
        (new GraphSettleProbe(graphSettled))->post();
        const auto settleDeadline = juce::Time::getMillisecondCounter() + 2000u;
        while (!graphSettled->load(std::memory_order_acquire)
               && juce::Time::getMillisecondCounter() < settleDeadline)
            juce::Thread::sleep(10);
    }

    // Block-wait for the bake + render. The message pump is a separate thread
    // so the render still completes (proven pattern from
    // export_bake_timeout_test.cpp).
    uint32_t waitMs = HDAW::ExportManager::computeBakeWaitMs(treeCopy)
                      + static_cast<uint32_t>(windowSeconds * 1000.0) + 5000u;
    // Allow HDAW_RENDER_WINDOW_WAIT_MS to override the total handler wait budget
    // (mirrors the HDAW_EXPORT_BAKE_TIMEOUT_MS precedent in ExportManager).
    if (const char* envMs = std::getenv("HDAW_RENDER_WINDOW_WAIT_MS"))
    {
        const int parsed = juce::String(envMs).getIntValue();
        if (parsed > 0)
            waitMs = static_cast<uint32_t>(parsed);
    }
    const auto deadline = juce::Time::getMillisecondCounter() + waitMs;
    while (em.isExporting() && juce::Time::getMillisecondCounter() < deadline)
        juce::Thread::sleep(10);
    if (em.isExporting())
    {
        em.cancelAndJoin();
        tempFile.deleteFile();
        result.error = "render timed out";
        return result;
    }

    // Surface the actual render-thread result instead of the generic
    // "no file to measure" message: a failed export now reports the real
    // reason (bake timeout, writer creation failure, thrown exception).
    const juce::String exportMsg = em.getLastExportMessage();
    if (!exportMsg.startsWith("Export complete"))
    {
        tempFile.deleteFile();
        result.error = (juce::String("export failed: ") + exportMsg).toStdString();
        return result;
    }

    if (!measureWav(fm, tempFile, result.rms, result.peak, outBands))
    {
        result.error = "failed to read render";
        return result;
    }

    result.wavPath = tempFile;
    return result;
}

// Set a plugin program on a LIVE slot and snapshot its state back into the
// ValueTree so tree-copy renders (gain stage, audition, export) and save/load
// capture the selection. The write uses nullptr undo — plugin state is volatile
// cache, not a user edit (mirrors Track::rebuildFXChain). Program/state calls
// run on the command/MCP thread — the proven load_plugin_preset path (Gate 16).
bool applyPluginProgram(AudioEngine& engine, int trackIndex, int slotIndex,
                        int programIndex, std::string& error)
{
    auto* proc = engine.getMainProcessor();
    if (proc == nullptr)
    {
        error = "plugin instance unavailable";
        return false;
    }
    auto* track = proc->getTrack(trackIndex);
    if (track == nullptr || slotIndex < 0
        || static_cast<size_t>(slotIndex) >= track->getFXChain().size())
    {
        error = "plugin instance unavailable";
        return false;
    }
    auto& slot = track->getFXChain()[static_cast<size_t>(slotIndex)];
    if (slot == nullptr || !slot->isPlugin() || slot->getPluginInstance() == nullptr)
    {
        error = "plugin instance unavailable";
        return false;
    }

    const int numPrograms = slot->getNumPrograms();
    if (programIndex < 0 || programIndex >= numPrograms)
    {
        error = "programIndex out of range (" + std::to_string(numPrograms) + " programs)";
        return false;
    }

    slot->setCurrentProgram(programIndex);
    auto* inst = slot->getPluginInstance();
    juce::MemoryBlock state;
    inst->getStateInformation(state);
    if (state.getSize() == 0)
    {
        error = "plugin produced empty state";
        return false;
    }

    auto trackList = engine.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren())
    {
        error = "plugin instance unavailable";
        return false;
    }
    auto fxChain = trackList.getChild(trackIndex).getChildWithName(IDs::FX_CHAIN);
    if (!fxChain.isValid() || slotIndex < 0 || slotIndex >= fxChain.getNumChildren())
    {
        error = "plugin instance unavailable";
        return false;
    }
    fxChain.getChild(slotIndex).setProperty(IDs::pluginState, state.toBase64Encoding(), nullptr);
    return true;
}

} // namespace

// ─── ProjectCommands — instrument part composer ───────────────────

ProjectCommands::InstrumentPartResult AudioEngineCommands::addInstrumentPart(const InstrumentPartParams& params)
{
    InstrumentPartResult result;

    // Role presets resolve BEFORE any mutation (Gate 9): normalize + validate
    // the role, then fill defaults for any role-defaultable field the caller
    // did not explicitly provide (explicitMask). A role guarantees a style, so
    // the pipeline below runs exactly as before with a filled-in params copy.
    auto p = params;
    if (!p.role.empty())
    {
        const int idx = roleIndex(toLowerAscii(p.role));
        if (idx < 0)
        {
            result.error = "unknown role: " + p.role;
            return result;
        }
        const RoleDefaults& d = kRoleDefaults[idx];
        if ((p.explicitMask & kRoleBitStyle) == 0)            p.style = d.style;
        if ((p.explicitMask & kRoleBitLowNote) == 0)          p.lowNote = d.lowNote;
        if ((p.explicitMask & kRoleBitHighNote) == 0)         p.highNote = d.highNote;
        if ((p.explicitMask & kRoleBitDensity) == 0)          p.density = d.density;
        if ((p.explicitMask & kRoleBitNoteDuration) == 0)     p.noteDuration = d.noteDuration;
        if ((p.explicitMask & kRoleBitMinVelocity) == 0)      p.minVelocity = d.minVelocity;
        if ((p.explicitMask & kRoleBitMaxVelocity) == 0)      p.maxVelocity = d.maxVelocity;
        if ((p.explicitMask & kRoleBitTargetRms) == 0)        p.targetRms = d.targetRms;
        if ((p.explicitMask & kRoleBitAllowGlobalScale) == 0) p.allowGlobalScale = d.allowGlobalScale;
    }
    else if (p.style.empty())
    {
        result.error = "style or role required";
        return result;
    }

    // Drums role: route to RhythmPatternGenerator (multi-voice polyrhythm)
    // instead of PhraseGenerator (single-voice melodic).
    const bool isDrumsRole = (toLowerAscii(p.role) == "drums");

    // Validate (Gate 9 — bounds-check every param at the command boundary).
    if (p.trackName.empty())
    {
        result.error = "trackName is required";
        return result;
    }
    PhraseGenerator::Style style;
    if (!styleFromName(p.style, style))
    {
        result.error = "unknown style: " + p.style;
        return result;
    }
    if (p.placement != "region" && p.placement != "wholeSong")
    {
        result.error = "placement must be 'region' or 'wholeSong'";
        return result;
    }
    if (p.count < 1)
    {
        result.error = "count must be >= 1";
        return result;
    }
    if (!(p.lengthBeats > 0.0))
    {
        result.error = "lengthBeats must be > 0";
        return result;
    }
    if (p.programIndex >= 0 && p.pluginId.empty())
    {
        result.error = "programIndex requires a pluginId";
        return result;
    }

    auto& model = engine_.getProjectModel();
    const double bpm = engine_.getTransportManager().getBPM();

    beginTransaction("Add instrument part");

    const int trackIndex = addTrack(p.trackName, -1, -1, 0);
    if (trackIndex < 0)
    {
        endTransaction();
        result.error = "failed to add track";
        return result;
    }

    // Instrument FX slot (internal fm_synth by default, or a hosted plugin).
    // addFxSlotInternal builds the slot tree without a per-op rebuild; the
    // single rebuildRoutingGraph at the end covers it (lesson 6).
    addFxSlotInternal(trackIndex, p.pluginId.empty() ? "fm_synth" : "plugin",
                      -1, p.pluginId);

    const int scaleRoot = (p.scaleRoot >= 0) ? p.scaleRoot : model.getScaleRoot();
    const int scaleMode = (p.scaleMode >= 0) ? p.scaleMode : model.getScaleMode();

    std::vector<PhraseGenerator::GeneratedNote> notes;

    if (isDrumsRole)
    {
        // Map role params to RhythmPatternGenerator.
        // density → pulseA (kick voice), default pulseB (hat) = density/2 or 3.
        RhythmPatternGenerator::Params rp;
        rp.grid = 16;                           // 16th-note grid
        rp.bars = static_cast<int>(std::ceil(p.lengthBeats / 4.0)); // 4 beats per bar
        rp.pulseA = p.density;                   // kick density
        rp.pulseB = std::max(3, p.density / 2); // hat density = half of kick, min 3
        rp.rotationA = 1;
        rp.rotationB = 1;
        rp.pitchA = p.lowNote;                   // default C2=36
        rp.pitchB = std::min(42, p.highNote);   // F#2=42 closed hat
        rp.velocityA = p.maxVelocity;
        rp.velocityB = (p.minVelocity + p.maxVelocity) / 2;
        rp.dsl = "";                             // no DSL by default
        rp.dslPitch = 39;                        // C#2 clap
        rp.dslVelocity = (p.minVelocity + p.maxVelocity) / 2;

        const auto drumNotes = RhythmPatternGenerator::generate(rp);
        notes.reserve(drumNotes.size());
        for (const auto& dn : drumNotes)
        {
            PhraseGenerator::GeneratedNote gn;
            gn.noteNumber = dn.pitch;
            gn.velocity = dn.velocity;
            gn.startBeat = dn.startBeat;
            gn.durationBeats = dn.durationBeats;
            notes.push_back(gn);
        }
    }
    else
    {
        PhraseGenerator::PhraseParams pp;
        pp.style = style;
        pp.lengthBeats = p.lengthBeats;
        pp.density = p.density;
        pp.noteDuration = p.noteDuration;
        pp.scaleRoot = scaleRoot;
        pp.scaleMode = scaleMode;
        pp.lowNote = p.lowNote;
        pp.highNote = p.highNote;
        pp.minVelocity = p.minVelocity;
        pp.maxVelocity = p.maxVelocity;
        pp.seed = p.seed;

        notes = PhraseGenerator::generatePhrase(pp);
    }

    const int clipId = addMidiClip(trackIndex, p.startBeat, p.lengthBeats,
                                   "Part: " + p.trackName);
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
    if (p.placement == "region")
    {
        copies = p.count - 1;
    }
    else // wholeSong: cover the whole project with lengthBeats-spaced copies
    {
        const double projectDurSec = HDAW::ExportManager::calculateProjectDuration(model);
        const double projectDurBeats = (bpm > 0) ? projectDurSec * bpm / 60.0 : projectDurSec;
        copies = std::max(0, static_cast<int>(std::ceil(projectDurBeats / p.lengthBeats)) - 1);
    }
    if (copies > 0)
    {
        const auto copyIds = paintGhostCopies(engine_, trackIndex, clipId, copies,
                                              p.lengthBeats, p.startBeat);
        result.clipIds.insert(result.clipIds.end(), copyIds.begin(), copyIds.end());
    }

    rebuildRoutingGraph();

    // Program pick rides the SAME undo unit as the composite: applied on the
    // live slot (it exists after the rebuild above) and snapshotted into the
    // slot's pluginState so the gain-stage tree-copy render hears it. On
    // failure the composite is closed with an error, matching the existing
    // mid-composite error path (addTrack/addMidiClip failures).
    if (p.programIndex >= 0)
    {
        std::string progErr;
        if (!applyPluginProgram(engine_, trackIndex, 0, p.programIndex, progErr))
        {
            // Roll the whole composite back so a bad program pick leaves no
            // dead track behind: undo() reverts every ValueTree action since
            // beginTransaction (track + slot + clip + notes), then close the
            // (now empty) transaction. The live graph still holds the track
            // until the tree-change listener's async rebuild runs — later
            // commands trigger their own rebuild from the tree, which is
            // consistent.
            undo();
            endTransaction();
            result.error = progErr;
            return result;
        }
    }

    endTransaction();

    result.trackIndex = trackIndex;

    // Optional gain staging — a SEPARATE undo unit ("Auto gain stage"), so
    // undo #1 removes the part and undo #2 removes the fader.
    if (p.targetRms > 0.0f)
        result.gain = autoGainToTarget(trackIndex, p.targetRms, p.windowSeconds, p.verify, p.allowGlobalScale);

    return result;
}

ProjectCommands::GainStageResult AudioEngineCommands::autoGainToTarget(int trackIndex, float targetRms, double windowSeconds, bool verify, bool allowGlobalScale)
{
    GainStageResult result;

    // Validate (Gate 9 — bounds-check every param at the command boundary).
    if (trackIndex < 0 || trackIndex >= engine_.getProjectModel().getTrackListTree().getNumChildren())
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

    // Raw render at unity. renderTrackWindow validates the processor/export
    // state, computes the window from the earliest clip, renders a solo tree
    // copy, and measures the WAV (the shared render loop, handoff #5).
    auto raw = renderTrackWindow(engine_, trackIndex, windowSeconds, 1.0f, false);
    if (!raw.error.empty())
    {
        result.error = raw.error;
        return result;
    }

    if (raw.rms <= 1e-6f)
    {
        raw.wavPath.deleteFile();
        result.error = "track is silent";
        return result;
    }

    const float unclamped = targetRms / raw.rms;
    float fader = unclamped;
    result.clamped = (fader > 1.0f);
    if (result.clamped)
        fader = 1.0f;
    result.masterGain = engine_.getProjectModel().getMasterGain();

    // Opt-in global scale: a clamped fader means the track alone wants > unity,
    // so the full mix at that fader may clip. A unity full-mix probe cannot show
    // how far over 1.0 it goes (the 24-bit WAV clamps at full scale), so probe at
    // an attenuated masterScale to recover the TRUE peak, then scale the master
    // bus down by 1/truePeak and raise the fader into the created headroom —
    // capped at the original unclamped target fader.
    if (result.clamped && allowGlobalScale)
    {
        constexpr float kProbeScale = 0.125f;   // measures true peaks up to 8.0
        auto mix = renderTrackWindow(engine_, trackIndex, windowSeconds, 1.0f, true,
                                     false, nullptr, kProbeScale);
        if (!mix.error.empty())
        {
            raw.wavPath.deleteFile();
            result.error = mix.error;
            return result;
        }
        const float trueMixPeak = mix.peak / kProbeScale;
        mix.wavPath.deleteFile();
        if (trueMixPeak >= 1.0f)
        {
            const float scale = 1.0f / trueMixPeak;
            result.globalScale = scale;
            result.masterGain = engine_.getProjectModel().getMasterGain() * scale;
            fader = std::min(unclamped, trueMixPeak);
        }
    }
    result.fader = fader;

    beginTransaction("Auto gain stage");
    setTrackVolume(trackIndex, fader);
    if (result.globalScale < 1.0f)
        setMasterGain(result.masterGain);
    endTransaction();

    if (verify)
    {
        // Re-render the same window with the fader applied, from a fresh tree
        // copy, and report the verified RMS/peak. Best-effort: a failed verify
        // keeps the fader write; only the measured values stay unset.
        auto check = renderTrackWindow(engine_, trackIndex, windowSeconds, fader, true);
        if (check.error.empty())
        {
            result.measuredRms = check.rms;
            result.peak = check.peak;
        }
        check.wavPath.deleteFile();
    }
    else
    {
        result.measuredRms = raw.rms;
        result.peak = raw.peak;
    }

    if (result.globalScale < 1.0f)
    {
        // Re-render the full mix to confirm the created headroom. Runs regardless
        // of `verify` — the global scale must always be confirmed.
        auto check = renderTrackWindow(engine_, trackIndex, windowSeconds, fader, true, false);
        if (check.error.empty())
            result.mixPeak = check.peak;
        check.wavPath.deleteFile();
    }

    raw.wavPath.deleteFile();
    result.ok = true;
    return result;
}


// ── Pattern placement (docs/plans/2026-08-29-jungle-dnb-feature-gaps.md P2-2) ──
// placePatterns: tiles caller-supplied bar-aligned MIDI patterns (the
// patterns[] payload from the analyze_midi_file MCP tool) across a beat range.
// Placement j uses patterns[j % patterns.size()]; each placement applies the
// PatternPlacer transforms (octave shift, velocity scale, retrograde). Notes
// are appended to the clip's MIDI_NOTE_LIST as ONE undo unit (fresh ids via
// createMidiNote -> allocateNoteID, the add_notes path) with the
// MidiClipProcessor note-slot ceiling enforced: notes past MAX_NOTE_SLOTS
// (8192) are skipped and counted, mirroring what the processor itself does at
// cache build. Pure data — no DSP touch, no routing-graph rebuild (note edits
// never rebuild the graph).
bool AudioEngineCommands::placePatterns(
    int clipId,
    const std::vector<std::vector<PatternPlacer::PatternNote>>& patterns,
    const std::vector<PatternPlacer::Placement>& placements,
    PlaceResult& out,
    bool clearExisting)
{
    out = PlaceResult{};
    out.clipId = clipId;

    // Validate every arg at the command boundary (Gate 9).
    if (patterns.empty())
    {
        out.error = "patterns must be non-empty";
        return false;
    }
    if (placements.empty())
    {
        out.error = "placements must be non-empty";
        return false;
    }

    auto& model = engine_.getProjectModel();
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid())
    {
        out.error = "clip not found";
        return false;
    }
    if (clip.getProperty(IDs::clipType).toString() != juce::String("midi"))
    {
        out.error = "clip is not MIDI";
        return false;
    }

    auto& um = model.getUndoManager();
    auto noteList = clip.getChildWithName(IDs::MIDI_NOTE_LIST);
    if (!noteList.isValid())
    {
        noteList = juce::ValueTree(IDs::MIDI_NOTE_LIST);
        clip.addChild(noteList, -1, &um);
    }

    // One undo unit: optional clear + every appended note.
    beginTransaction("Place patterns");
    if (clearExisting && noteList.getNumChildren() > 0)
        noteList.removeAllChildren(&um);

    const int ceiling = HDAW::MidiClipProcessor::MAX_NOTE_SLOTS;
    const int existingCount = clearExisting ? 0 : noteList.getNumChildren();

    for (size_t j = 0; j < placements.size(); ++j)
    {
        const auto& pattern = patterns[j % patterns.size()]; // cyclic

        // Clamp the caller-side placement ranges defensively (the MCP schema
        // already bounds them; a direct command caller can never exceed the
        // MIDI limits either).
        PatternPlacer::Placement p = placements[j];
        p.octaveShift = (std::max)(-PatternPlacer::kMaxOctaveShift,
                          (std::min)(PatternPlacer::kMaxOctaveShift, p.octaveShift));
        p.velocityScale = (std::max)(PatternPlacer::kMinVelocityScale,
                            (std::min)(PatternPlacer::kMaxVelocityScale, p.velocityScale));

        const auto placed = PatternPlacer::place(pattern, p);
        for (const auto& n : placed)
        {
            if (existingCount + out.added >= ceiling)
            {
                ++out.skipped; // past the per-clip note ceiling — skip + report
                continue;
            }
            const int pitch = (std::max)(0, (std::min)(127, n.pitch));
            auto note = model.createMidiNote(pitch,
                                             static_cast<float>(n.velocity) / 127.0f,
                                             n.startBeat, n.durationBeats);
            noteList.addChild(note, -1, &um);
            ++out.added;
        }
    }
    endTransaction();

    out.ok = true;
    return true;
}

ProjectCommands::AuditionResult AudioEngineCommands::auditionPlugin(const AuditionParams& params)
{
    AuditionResult result;

    // Validate (Gate 9 — bounds-check every param at the command boundary).
    const bool tempProbe = (params.trackIndex < 0);
    if (tempProbe && params.pluginId.empty())
    {
        result.error = "pluginId is required when trackIndex < 0";
        return result;
    }
    PhraseGenerator::Style style;
    if (!styleFromName(params.style, style))
    {
        result.error = "unknown style: " + params.style;
        return result;
    }
    if (!(params.lengthBeats > 0.0))
    {
        result.error = "lengthBeats must be > 0";
        return result;
    }
    if (!(params.windowSeconds > 0.0))
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

    int trackIndex = params.trackIndex;
    int slotIndex = params.slotIndex;
    bool probeCommitted = false;

    if (tempProbe)
    {
        auto& model = engine_.getProjectModel();
        beginTransaction("Audition probe");
        trackIndex = addTrack("Audition", -1, -1, 0);
        if (trackIndex < 0)
        {
            endTransaction();
            result.error = "failed to add track";
            return result;
        }
        addFxSlotInternal(trackIndex, "plugin", -1, params.pluginId);
        slotIndex = 0;

        PhraseGenerator::PhraseParams pp;
        pp.style = style;
        pp.lengthBeats = params.lengthBeats;
        pp.density = params.density;
        pp.noteDuration = params.noteDuration;
        pp.scaleRoot = model.getScaleRoot();
        pp.scaleMode = model.getScaleMode();
        pp.lowNote = params.lowNote;
        pp.highNote = params.highNote;
        pp.minVelocity = params.minVelocity;
        pp.maxVelocity = params.maxVelocity;
        pp.seed = params.seed;
        const auto notes = PhraseGenerator::generatePhrase(pp);

        const int clipId = addMidiClip(trackIndex, 0.0, params.lengthBeats, "Audition");
        if (clipId < 0)
        {
            endTransaction();
            result.error = "failed to add MIDI clip";
            return result;
        }
        for (const auto& n : notes)
            addNote(clipId, n.noteNumber, n.velocity, n.startBeat, n.durationBeats);

        rebuildRoutingGraph();
        endTransaction();
        probeCommitted = true;
    }
    else
    {
        auto trackList = engine_.getProjectModel().getTrackListTree();
        if (params.trackIndex < 0 || params.trackIndex >= trackList.getNumChildren())
        {
            result.error = "trackIndex out of range";
            return result;
        }
        auto* track = proc->getTrack(params.trackIndex);
        if (track == nullptr && proc->getRoutingManager() == nullptr)
        {
            // No audio device — validate against the ValueTree instead.
            auto trackTree = trackList.getChild(params.trackIndex);
            auto fxChain = trackTree.getChildWithName(IDs::FX_CHAIN);
            if (!fxChain.isValid() || params.slotIndex < 0
                || params.slotIndex >= fxChain.getNumChildren())
            {
                result.error = "slotIndex out of range";
                return result;
            }
            auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
            if (!clipList.isValid() || clipList.getNumChildren() == 0)
            {
                result.error = "track has no clips";
                return result;
            }
        }
        else
        {
            if (track == nullptr || params.slotIndex < 0
                || static_cast<size_t>(params.slotIndex) >= track->getFXChain().size())
            {
                result.error = "slotIndex out of range";
                return result;
            }
            auto& slot = track->getFXChain()[static_cast<size_t>(params.slotIndex)];
            if (slot == nullptr)
            {
                result.error = "slot is empty";
                return result;
            }
            auto clipList = trackList.getChild(params.trackIndex).getChildWithName(IDs::CLIP_LIST);
            if (!clipList.isValid() || clipList.getNumChildren() == 0)
            {
                result.error = "track has no clips";
                return result;
            }
        }
    }

    // Roll back a committed probe: one undo() reverts the whole "Audition
    // probe" transaction, then the graph is rebuilt from the tree so the live
    // processors match. A failed probe must leave the project untouched.
    auto rollbackProbe = [&]() {
        if (tempProbe && probeCommitted)
        {
            undo();
            if (auto* p = engine_.getMainProcessor())
                p->rebuildRoutingGraph();
            probeCommitted = false;
        }
    };

    // Apply the requested program on the LIVE slot and snapshot its state into
    // the tree (applyPluginProgram). programIndex == -1 reports the current
    // program without touching it.
    auto* track = proc->getTrack(trackIndex);
    if (track == nullptr
        || slotIndex < 0 || static_cast<size_t>(slotIndex) >= track->getFXChain().size()
        || track->getFXChain()[static_cast<size_t>(slotIndex)] == nullptr)
    {
        // When the routing manager is null (no audio device / test environment),
        // the live processor has no tracks. Skip program operations — the render
        // path reads from the ValueTree and will still produce audio.
        if (track == nullptr && proc->getRoutingManager() == nullptr)
        {
            result.numPrograms = 1;
            if (params.programIndex >= 0)
            {
                if (params.programIndex >= result.numPrograms)
                {
                    rollbackProbe();
                    result.error = "programIndex out of range (1 programs)";
                    return result;
                }
                // Can't apply a program without a live slot — reject.
                rollbackProbe();
                result.error = "plugin instance unavailable";
                return result;
            }
            else
            {
                result.programIndex = 0;
            }
        }
        else
        {
            rollbackProbe();
            result.error = "plugin instance unavailable";
            return result;
        }
    }
    else
    {
        auto& slot = track->getFXChain()[static_cast<size_t>(slotIndex)];
        result.numPrograms = slot->getNumPrograms();
        if (params.programIndex >= 0)
        {
            if (params.programIndex >= result.numPrograms)
            {
                rollbackProbe();
                result.error = "programIndex out of range (" + std::to_string(result.numPrograms) + " programs)";
                return result;
            }
            std::string progErr;
            if (!applyPluginProgram(engine_, trackIndex, slotIndex, params.programIndex, progErr))
            {
                rollbackProbe();
                result.error = progErr;
                return result;
            }
            result.programIndex = params.programIndex;
            result.programName = slot->getProgramName(params.programIndex).toStdString();
        }
        else
        {
            result.programIndex = slot->getCurrentProgram();
            if (result.programIndex >= 0 && result.programIndex < result.numPrograms)
                result.programName = slot->getProgramName(result.programIndex).toStdString();
        }
    }

    // Solo-render the window and report the level (audible ≈ peak > -80 dBFS).
    auto r = renderTrackWindow(engine_, trackIndex, params.windowSeconds, 1.0f, false);
    if (!r.error.empty())
    {
        rollbackProbe();
        result.error = r.error;
        return result;
    }
    result.rms = r.rms;
    result.peak = r.peak;
    result.durationSeconds = params.windowSeconds;
    result.audible = (r.peak > 1e-4f);
    r.wavPath.deleteFile();

    if (tempProbe && !params.keepTrack)
    {
        rollbackProbe();
        result.trackIndex = -1;
    }
    else
    {
        result.trackIndex = trackIndex;
        result.slotIndex = slotIndex;
    }

    result.ok = true;
    return result;
}

ProjectCommands::VerifyPartResult AudioEngineCommands::verifyPart(int trackIndex, double windowSeconds)
{
    VerifyPartResult result;

    // Validate (Gate 9 — bounds-check every param at the command boundary).
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren())
    {
        result.error = "trackIndex out of range";
        return result;
    }
    if (!(windowSeconds > 0.0))
    {
        result.error = "windowSeconds must be > 0";
        return result;
    }

    // Solo render (with band analysis) + full-mix render of the same window —
    // both via the shared renderTrackWindow. Read-only: no tree writes, no
    // undo, no rebuild.
    BandPresence bands;
    auto solo = renderTrackWindow(engine_, trackIndex, windowSeconds, 1.0f, false, true, &bands);
    if (!solo.error.empty())
    {
        result.error = solo.error;
        return result;
    }

    auto mix = renderTrackWindow(engine_, trackIndex, windowSeconds, 1.0f, false, false);
    if (!mix.error.empty())
    {
        solo.wavPath.deleteFile();
        result.error = mix.error;
        return result;
    }

    result.soloRms = solo.rms;
    result.soloPeak = solo.peak;
    result.mixRms = mix.rms;
    result.mixPeak = mix.peak;
    result.windowStart = solo.windowStart;
    result.durationSeconds = windowSeconds;
    result.audible = (solo.peak > 1e-4f);
    result.nonClipping = (mix.peak < 1.0f);
    result.bandLow = bands.low;
    result.bandMid = bands.mid;
    result.bandHigh = bands.high;
    result.bandsPresent = bands.low && bands.mid && bands.high;
    result.ok = true;

    solo.wavPath.deleteFile();
    mix.wavPath.deleteFile();
    return result;
}

// ── Break chopper/composer (P2-1) ────────────────────────────────────────
// generateChoppedBreak: reads the sampler slot's DETECTED slice boundaries +
// baseNote from the ValueTree, generates a seeded stylized slice-trigger
// pattern with BreakPatternGenerator (pure, offline — no DSP touch), and
// writes the notes into the clip's MIDI_NOTE_LIST as ONE undo unit. The
// sliced sampler plays them at render (slice index = note - baseNote).
AudioEngineCommands::BreakPatternResult AudioEngineCommands::generateChoppedBreak(const BreakPatternParams& params)
{
    BreakPatternResult result;

    // Validate every arg at the command boundary (Gate 9).
    auto& model = engine_.getProjectModel();
    auto trackList = model.getTrackListTree();
    if (params.trackIndex < 0 || params.trackIndex >= trackList.getNumChildren())
    {
        result.error = "trackIndex out of range";
        return result;
    }
    if (params.bars < BreakPatternGenerator::kMinBars || params.bars > BreakPatternGenerator::kMaxBars)
    {
        result.error = "bars must be in 1..64";
        return result;
    }
    if (params.grid < BreakPatternGenerator::kMinGrid || params.grid > BreakPatternGenerator::kMaxGrid)
    {
        result.error = "grid must be in 1..8";
        return result;
    }
    if (params.velocityMin < 1 || params.velocityMin > 127
        || params.velocityMax < 1 || params.velocityMax > 127)
    {
        result.error = "velocities must be in 1..127";
        return result;
    }
    if (params.velocityMin > params.velocityMax)
    {
        result.error = "velocityMin must be <= velocityMax";
        return result;
    }
    if (params.ghostFills < 0 || params.ghostFills > BreakPatternGenerator::kMaxGhostFills)
    {
        result.error = "ghostFills must be in 0..2";
        return result;
    }

    // Sampler FX slot must exist and be a sampler (read of the ValueTree —
    // the DSP is intentionally untouched here).
    auto slot = findFxSlot(params.trackIndex, params.slotIndex);
    if (!slot.isValid())
    {
        result.error = "slot not found";
        return result;
    }
    if (slot.getProperty(IDs::fxType, "").toString() != juce::String("sampler"))
    {
        result.error = "slot is not a sampler";
        return result;
    }

    // Detected slice boundaries: normalized (0..1), comma-separated, stored by
    // detectSamplerSlices (points include sample start 0 and end; the slice
    // COUNT is points.size()-1 — the number of triggerable slices).
    juce::StringArray tokens = juce::StringArray::fromTokens(
        slot.getProperty("slicePoints", "").toString(), ",", "");
    const int sliceCount = std::max(0, static_cast<int>(tokens.size()) - 1);
    if (sliceCount < 1)
    {
        result.error = "no slices: run detect_sampler_slices first";
        return result;
    }
    result.sliceCount = sliceCount;

    // baseNote for the chromatic slice mapping (slice index = note - baseNote;
    // SamplerEngine::handleNoteOn). Same property the live track reads
    // (TrackFXSlot::loadSamplerState reads "baseNote", default 60); configurable
    // via set_sampler_param baseNote.
    const int baseNote = static_cast<int>(slot.getProperty("baseNote", 60));
    result.baseNote = baseNote;

    // The pattern is written into an existing MIDI clip (notes are data).
    int trackOfClip = -1;
    auto clip = findClipById(params.clipId, trackOfClip);
    if (!clip.isValid())
    {
        result.error = "clip not found";
        return result;
    }
    if (clip.getProperty(IDs::clipType).toString() != juce::String("midi"))
    {
        result.error = "clip is not MIDI";
        return result;
    }

    BreakPatternGenerator::Params gp;
    gp.sliceCount   = sliceCount;
    gp.baseNote     = baseNote;   // caps the slice pool at 128-baseNote (MIDI range)
    gp.bars         = params.bars;
    gp.grid         = params.grid;
    gp.style        = params.style;
    gp.dropFirst    = params.dropFirst;
    gp.ghostFills   = params.ghostFills;
    gp.velocityMin  = params.velocityMin;
    gp.velocityMax  = params.velocityMax;
    gp.seed         = params.seed;

    const auto steps = BreakPatternGenerator::generate(gp);
    if (steps.empty())
    {
        result.error = "pattern produced no notes";
        return result;
    }
    const auto notes = BreakPatternGenerator::asNotes(gp, steps, baseNote);

    // One undo unit: append every note (fresh ids via createMidiNote ->
    // allocateNoteID, the add_notes path). No graph rebuild — note edits are
    // data and never rebuild the routing graph.
    auto& um = model.getUndoManager();
    auto noteList = clip.getChildWithName(IDs::MIDI_NOTE_LIST);
    if (!noteList.isValid())
    {
        noteList = juce::ValueTree(IDs::MIDI_NOTE_LIST);
        clip.addChild(noteList, -1, &um);
    }
    beginTransaction("Generate chopped break");
    for (const auto& n : notes)
    {
        auto note = model.createMidiNote(n.pitch, static_cast<float>(n.velocity) / 127.0f,
                                         n.startBeat, n.durationBeats);
        noteList.addChild(note, -1, &um);
        ++result.added;
        if (result.added == 1)
            result.firstPitch = n.pitch;
        result.lastPitch = n.pitch; // steps arrive sorted by stepIndex
    }
    endTransaction();

    result.ok = true;
    return result;
}

ProjectCommands::PsytranceResult AudioEngineCommands::generatePsytrance(const HDAW::PsytranceParams& params)
{
    ProjectCommands::PsytranceResult result;

    // Pure generation first — if the grammar rejects the params, nothing is
    // written and the caller gets a tool-named error.
    const auto score = HDAW::PsytranceGenerator::generate(params);
    if (!score.error.empty())
    {
        result.error = "generate_psytrance: " + score.error;
        return result;
    }

    auto& model = engine_.getProjectModel();
    const int trackCount = model.getTrackListTree().getNumChildren();

    // Validate every role's track BEFORE any mutation (no partial writes).
    for (const auto& clip : score.clips)
        if (clip.trackIndex < 0 || clip.trackIndex >= trackCount)
        {
            result.error = "generate_psytrance: role '" + clip.role
                         + "' track index out of range";
            return result;
        }

    result.totalBeats = score.totalBeats;
    result.notesTotal = score.notesTotal;
    result.skippedRoles = score.skipped;

    auto& um = model.getUndoManager();
    beginTransaction("Generate psytrance");

    constexpr int kMaxNotesPerClip = 8192; // MidiClipProcessor cache ceiling
    for (const auto& clip : score.clips)
    {
        ProjectCommands::PsytranceResult::Clip out;
        out.role = clip.role;
        out.trackIndex = clip.trackIndex;

        // One clip per role at beat 0 spanning the whole arrangement → note
        // starts are clip-local (= absolute beats; guide §9.1 contract).
        const double bpm = engine_.getTransportManager().getBPM();
        auto c = model.createMidiClipEmpty("Psy" + clip.role,
                                           HDAW::beatsToSeconds(0.0, bpm),
                                           HDAW::beatsToSeconds(score.totalBeats, bpm));
        c.setProperty(IDs::color, static_cast<int>(ProjectModel::trackColorForIndex(clip.trackIndex)), &um);
        auto noteList = c.getChildWithName(IDs::MIDI_NOTE_LIST);
        if (!noteList.isValid())
        {
            noteList = juce::ValueTree(IDs::MIDI_NOTE_LIST);
            c.addChild(noteList, -1, &um);
        }

        int written = 0;
        for (const auto& n : clip.notes)
        {
            if (n.startBeat >= score.totalBeats) { ++result.notesSkipped; continue; }
            if (written >= kMaxNotesPerClip)     { ++result.notesSkipped; continue; }
            auto note = model.createMidiNote(n.pitch,
                                             static_cast<float>(n.velocity) / 127.0f,
                                             n.startBeat, n.durationBeats);
            noteList.addChild(note, -1, &um);
            ++written;
        }
        out.noteCount = written;
        out.clipId = static_cast<int>(c.getProperty(IDs::clipID));
        if (out.clipId < 0) out.clipId = -1;

        model.getTrackListTree().getChild(clip.trackIndex)
            .getChildWithName(IDs::CLIP_LIST).addChild(c, -1, &um);
        result.clips.push_back(std::move(out));
    }

    endTransaction();
    return result;
}

ProjectCommands::PsytranceMarkovResult
AudioEngineCommands::generatePsytranceMarkov(const HDAW::PsytranceMarkovParams& params)
{
    ProjectCommands::PsytranceMarkovResult result;

    // Pure generation first — if the params are rejected, nothing is written
    // and the caller gets a tool-named error (no partial writes).
    const auto score = HDAW::PsytranceMarkovGenerator::generate(params);
    if (!score.error.empty())
    {
        result.error = "generate_psytrance_markov: " + score.error;
        return result;
    }

    auto& model = engine_.getProjectModel();
    const int trackCount = model.getTrackListTree().getNumChildren();

    // Validate every role's track BEFORE any mutation (no partial writes).
    for (const auto& clip : score.clips)
        if (clip.trackIndex < 0 || clip.trackIndex >= trackCount)
        {
            result.error = "generate_psytrance_markov: role '" + clip.role
                         + "' track index out of range";
            return result;
        }

    result.totalBeats = score.totalBeats;
    result.notesTotal = score.notesTotal;
    result.skippedRoles = score.skipped;
    for (const auto& s : score.steps)
    {
        ProjectCommands::PsytranceMarkovResult::Step out;
        out.barStart = s.barStart;
        out.action = HDAW::PsytranceMarkovGenerator::actionName(s.action);
        out.targetRole = s.targetRole;
        out.activeRoles = s.activeRoles;
        out.ages = s.ages;
        out.keyRoot = s.keyRoot;
        out.section = s.section;
        result.steps.push_back(std::move(out));
    }
    for (const auto& a : score.automations)
        result.automations.push_back({ a.role, a.param, a.startBeat, a.value, a.durationBeats });

    auto& um = model.getUndoManager();
    beginTransaction("Generate psytrance (Markov)");

    constexpr int kMaxNotesPerClip = 8192; // MidiClipProcessor cache ceiling
    for (const auto& clip : score.clips)
    {
        ProjectCommands::PsytranceMarkovResult::Clip out;
        out.role = clip.role;
        out.trackIndex = clip.trackIndex;

        // One clip per role at beat 0 spanning the whole arrangement → note
        // starts are clip-local (= absolute beats; guide §9.1 contract).
        const double bpm = engine_.getTransportManager().getBPM();
        auto c = model.createMidiClipEmpty("Psy" + clip.role,
                                           HDAW::beatsToSeconds(0.0, bpm),
                                           HDAW::beatsToSeconds(score.totalBeats, bpm));
        c.setProperty(IDs::color, static_cast<int>(ProjectModel::trackColorForIndex(clip.trackIndex)), &um);
        auto noteList = c.getChildWithName(IDs::MIDI_NOTE_LIST);
        if (!noteList.isValid())
        {
            noteList = juce::ValueTree(IDs::MIDI_NOTE_LIST);
            c.addChild(noteList, -1, &um);
        }

        int written = 0;
        for (const auto& n : clip.notes)
        {
            if (n.startBeat >= score.totalBeats) { ++result.notesSkipped; continue; }
            if (written >= kMaxNotesPerClip)     { ++result.notesSkipped; continue; }
            auto note = model.createMidiNote(n.pitch,
                                             static_cast<float>(n.velocity) / 127.0f,
                                             n.startBeat, n.durationBeats);
            noteList.addChild(note, -1, &um);
            ++written;
        }
        out.noteCount = written;
        out.clipId = static_cast<int>(c.getProperty(IDs::clipID));
        if (out.clipId < 0) out.clipId = -1;

        model.getTrackListTree().getChild(clip.trackIndex)
            .getChildWithName(IDs::CLIP_LIST).addChild(c, -1, &um);
        result.clips.push_back(std::move(out));
    }

    // ── Volume fades: "volume" automations are written FOR REAL onto each
    //    role's Volume lane (paramID 1) inside this same transaction (ONE
    //    undo unit with the clips). filterCutoff entries stay advisory. ──
    {
        // Beats at this boundary, seconds in the tree — the same conversion
        // the add_automation_point RPC performs (docs/architecture.md).
        const double bpm = static_cast<double>(
            model.getTree().getProperty(IDs::tempo, 120.0));
        auto roleToTrack = [&](const std::string& role) -> int {
            if (role == "kick")  return params.kick;
            if (role == "bass")  return params.bass;
            if (role == "hat")   return params.hat;
            if (role == "snare") return params.snare;
            if (role == "rim")   return params.rim;
            if (role == "arp")   return params.arp;
            if (role == "stab")  return params.stab;
            if (role == "pad")   return params.pad;
            if (role == "clap")  return params.clap;
            return -1; // riser/down FX roles carry no volume fades
        };
        std::vector<int> touchedTracks;
        for (const auto& a : score.automations)
        {
            if (a.param != "volume") continue; // filterCutoff: advisory only
            const int trackIdx = roleToTrack(a.role);
            if (trackIdx < 0 || trackIdx >= trackCount)
            { ++result.automationsSkipped; continue; }

            // Ensure the Volume lane exists via the SAME internal command
            // path the add_automation_lane RPC uses (idempotent on the
            // default lane every track ships; false = name/paramID conflict).
            if (!addAutomationLane(trackIdx, "Volume", 1))
            { ++result.automationsSkipped; continue; }
            auto lane = findAutomationLane(trackIdx, "Volume");
            if (!lane.isValid()) { ++result.automationsSkipped; continue; }

            // Fader-authoritative (lane disabled) is cleared for generated
            // fades: writing points into a disabled lane would swallow them
            // silently. Same enable-on-write contract as applyAutomationPreset.
            if (!static_cast<bool>(lane.getProperty(IDs::automationEnabled, true)))
                lane.setProperty(IDs::automationEnabled, true, &um);

            auto pointList = lane.getChildWithName(IDs::POINT_LIST);
            if (!pointList.isValid())
            {
                pointList = juce::ValueTree(IDs::POINT_LIST);
                lane.addChild(pointList, -1, &um);
            }
            if (isUntouchedFactoryVolumePoints(pointList))
            {
                // Drop the factory hold placeholders so they cannot ramp a
                // fade-out back to unity at 16s; user-authored points are
                // kept (accumulate via upsert below).
                while (pointList.getNumChildren() > 0)
                    pointList.removeChild(0, &um);
            }
            upsertAutomationPoint(pointList, HDAW::beatsToSeconds(a.startBeat, bpm),
                                  a.value, um);

            if (std::find(touchedTracks.begin(), touchedTracks.end(), trackIdx)
                == touchedTracks.end())
                touchedTracks.push_back(trackIdx);
        }
        // One automation-cache refresh per touched track (command-thread,
        // same seam every automation RPC uses).
        if (auto* proc = engine_.getMainProcessor())
            for (int t : touchedTracks)
                proc->rebuildAutomationCache(t);
    }

    endTransaction();
    return result;
}
