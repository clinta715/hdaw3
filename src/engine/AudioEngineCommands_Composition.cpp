#include "AudioEngineCommands.h"
#include "AudioEngineCommands_Helpers.h"
#include "AudioEngine.h"
#include "MainAudioProcessor.h"
#include "ExportManager.h"
#include "PhraseGenerator.h"
#include "RhythmPatternGenerator.h"
#include "../model/ProjectModel.h"
#include "../common/DebugLog.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <limits>
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

    if (!em.startExport(treeCopy, fm, &engine.getPluginManager(), tempFile,
                        48000.0, windowStart, windowSeconds,
                        HDAW::ExportManager::WAV, 24))
    {
        result.error = "failed to start render";
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
        result.error = "render timed out";
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
