#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <juce_core/juce_core.h>
#include "../engine/EnvelopeGenerator.h"
#include "../engine/AutomationPreset.h"
#include "../engine/PsytranceGenerator.h"
#include "../engine/PsytranceMarkovGenerator.h"

namespace HDAW { struct ArrangementParams; struct ChainPreset; }

class ProjectCommands
{
public:
    virtual ~ProjectCommands() = default;

    // Track operations
    virtual int addTrack(const std::string& name, int color = -1, int parentBus = -1, int trackType = 0) = 0;
    virtual void removeTrack(int trackIndex) = 0;
    virtual void moveTrack(int trackIndex, int newIndex) = 0;
    virtual void setTrackName(int trackIndex, const std::string& name) = 0;
    virtual void setTrackColor(int trackIndex, int color) = 0;
    virtual void setTrackVolume(int trackIndex, float volume) = 0;
    // Master-bus gain (linear, clamped >= 0). Undoable root-tree write; the
    // live processor follows via the AudioEngine listener, rebuilds restore it.
    virtual void setMasterGain(float gain) = 0;
    virtual void setTrackPan(int trackIndex, float pan) = 0;
    virtual void setTrackMuted(int trackIndex, bool muted) = 0;
    virtual void setTrackSoloed(int trackIndex, bool soloed) = 0;
    virtual void setTrackArmed(int trackIndex, bool armed) = 0;
    virtual void setTrackInputMonitor(int trackIndex, bool monitor) = 0;
    virtual void setTrackHeight(int trackIndex, int height) = 0;
    virtual void setTrackMidiChannel(int trackIndex, int channel) = 0;
    virtual void setTrackType(int trackIndex, int type) = 0;
    virtual void setTrackCollapsed(int trackIndex, bool collapsed) = 0;
    virtual void setTrackHidden(int trackIndex, bool hidden) = 0;
    virtual void moveTrackIntoFolder(int trackIndex, int folderIndex) = 0;
    virtual void moveTrackOutOfFolder(int trackIndex) = 0;

    // Send operations
    virtual void setTrackSendLevel(int trackIndex, int sendIndex, float level) = 0;
    virtual void setTrackSendMode(int trackIndex, int sendIndex, bool isPreFader) = 0;
    virtual void setTrackSendBypassed(int trackIndex, int sendIndex, bool bypassed) = 0;

    // Session
    virtual void setClipScene(int clipId, int sceneIndex) = 0;
    virtual int createSessionClip(int trackIndex, int sceneIndex, bool isMidi) = 0;
    virtual void launchScene(int sceneIndex) = 0;
    virtual void stopAllSessionClips() = 0;

    // Clip operations
    virtual int addAudioClip(int trackIndex, double start, double duration,
                             const std::string& sourceFile, const std::string& name) = 0;
    virtual int addMidiClip(int trackIndex, double start, double duration,
                            const std::string& name) = 0;
    virtual std::vector<int> importMidiFile(const std::string& filePath, int trackIndex = -1) = 0;
    virtual void removeClip(int clipId) = 0;
    virtual void moveClip(int clipId, int newTrackIndex, double newStart) = 0;
    virtual void moveClipWithOverlap(int clipId, int newTrackIndex, double newStart) = 0;
    virtual void setClipStart(int clipId, double start) = 0;
    virtual void setClipDuration(int clipId, double duration) = 0;
    virtual void setClipGain(int clipId, float gain) = 0;
    virtual void setClipFadeIn(int clipId, double fadeIn) = 0;
    virtual void setClipFadeOut(int clipId, double fadeOut) = 0;
    virtual void setClipOffset(int clipId, double offset) = 0;
    virtual void setClipLooping(int clipId, bool looping) = 0;
    virtual void setClipMuted(int clipId, bool muted) = 0;
    virtual int duplicateClip(int clipId) = 0;
    // Duplicate a clip directly to a target position + track. Combines
    // duplicateClip + moveClipWithOverlap into one call so the frontend can
    // place a ctrl-drag copy in a single round trip instead of two. Returns
    // the new clip id, or -1 on failure (invalid source/track).
    virtual int duplicateClipTo(int clipId, double newStart, int newTrackIndex) = 0;
    virtual int createGhostClip(int sourceClipId, double newStart, int newTrackIndex) = 0;
    virtual std::vector<int> paintClips(const std::vector<int>& sourceClipIds, double originBeat, double spacing, int targetTrackIndex, int count) = 0;
    // Batch duplicate: duplicate multiple clips to specified positions in one transaction.
    // Each clipId[i] is duplicated to newStarts[i] on newTrackIndices[i].
    // Returns the new clip IDs (same order as input), or empty on error.
    virtual std::vector<int> duplicateClips(const std::vector<int>& clipIds, const std::vector<double>& newStarts, const std::vector<int>& newTrackIndices) = 0;
    // Batch move: move multiple clips to new positions in one transaction.
    // Each clipId[i] is moved to newStarts[i] on newTrackIndices[i].
    // Uses moveClipWithOverlap semantics (trims/splits overlapping clips).
    virtual void moveClips(const std::vector<int>& clipIds, const std::vector<double>& newStarts, const std::vector<int>& newTrackIndices) = 0;
    // Batch remove: remove multiple clips in one transaction.
    virtual void removeClips(const std::vector<int>& clipIds) = 0;
    // Ripple delete: remove all clip content within [startBeat, endBeat) and
    // close the gap by shifting every clip that starts at or after endBeat
    // leftward by (endBeat - startBeat). Beats at this boundary (lesson #1).
    virtual void rippleDelete(double startBeat, double endBeat) = 0;
    // Insert silence: split any clip crossing startBeat, then shift every clip
    // that starts at or after startBeat rightward by (endBeat - startBeat),
    // opening an empty gap [startBeat, endBeat). Beats at this boundary.
    virtual void insertSilence(double startBeat, double endBeat) = 0;
    // Duplicate region: copy all clip content within [startBeat, endBeat) and
    // paste it at endBeat, shifting every clip that starts at or after endBeat
    // rightward by (endBeat - startBeat). Beats at this boundary.
    virtual void duplicateRegion(double startBeat, double endBeat) = 0;
    // Batch add: add multiple clips in one transaction (for clipboard paste).
    // When sourceFiles[i] is non-empty, creates an audio clip; otherwise MIDI.
    // Returns the new clip IDs.
    virtual std::vector<int> addClips(int trackIndex, const std::vector<double>& starts, const std::vector<double>& durations, const std::vector<std::string>& names, const std::vector<std::string>& sourceFiles = {}) = 0;

    // Audio clip timestretch. Stretch is resolved at graph-build time and
    // rendered off-thread via StretchCache; it is NOT RT-parametric (no
    // SPSC update), so changing these triggers rebuildRoutingGraph.
    // setClipSourceBpm stores the source file's musical tempo (0=unknown).
    virtual void setClipSourceBpm(int clipId, double bpm) = 0;
    // setClipStretchMode: 0=Off, 1=TempoMatch, 2=ManualRatio.
    virtual void setClipStretchMode(int clipId, int mode) = 0;
    // setClipStretchRatio sets the manual time-stretch ratio (target/source).
    virtual void setClipStretchRatio(int clipId, double ratio) = 0;
    // tempoMatchClip sets Mode=TempoMatch and derives the ratio from
    // sourceBpm/projectBpm (no-op if sourceBpm<=0).
    virtual void tempoMatchClip(int clipId) = 0;
    // fitClipToLoop stretches the entire source to span the loop region
    // exactly (Mode=ManualRatio, ratio=loopLength/sourceDuration).
    virtual void fitClipToLoop(int clipId) = 0;

    // MIDI note operations
    virtual int addNote(int clipId, int pitch, int velocity,
                        double startBeat, double durationBeats) = 0;
    virtual void removeNote(int noteId) = 0;
    virtual void setNotePitch(int noteId, int pitch) = 0;
    virtual void setNoteVelocity(int noteId, int velocity) = 0;
    virtual void setNoteStart(int noteId, double startBeat) = 0;
    virtual void setNoteDuration(int noteId, double durationBeats) = 0;
    virtual void setNoteChance(int noteId, float chance) = 0;
    virtual void setNoteRepeatCount(int noteId, int repeatCount) = 0;
    virtual void setNoteRepeatRate(int noteId, float repeatRate) = 0;
    virtual void setNoteRepeatCurve(int noteId, float repeatCurve) = 0;
    virtual void setNoteOccurrence(int noteId, int occurrence) = 0;
    virtual void setNoteRecurrence(int noteId, int recurrence) = 0;
    virtual void setNoteGain(int noteId, float gain) = 0;
    virtual void setNotePan(int noteId, float pan) = 0;
    virtual void setNotePitchOffset(int noteId, float pitchOffset) = 0;
    virtual void setNoteTimbre(int noteId, float timbre) = 0;
    virtual void setNotePressure(int noteId, float pressure) = 0;
    virtual void setNotesExpression(int noteId, float gain, float pan, float pitchOffset, float timbre, float pressure) = 0;
    virtual void setClipSeed(int clipId, uint64_t seed) = 0;
    virtual void setNotesOperator(int clipId, int noteId, float chance, int repeatCount, float repeatRate, float repeatCurve, int occurrence, int recurrence) = 0;
    virtual void clearNotes(int clipId) = 0;
    virtual int mergeClips(const std::vector<int>& clipIds) = 0;

    // FX operations (integer type: 0=eq, 1=compressor, 2=reverb, 3=delay, else=plugin)
    virtual void addFxSlot(int trackIndex, int type, int position = -1,
                           const std::string& pluginId = "") = 0;
    // FX operations (string type: "eq", "compressor", "reverb", "delay", "plugin")
    virtual void addFxSlot(int trackIndex, const std::string& type,
                           int position = -1, const std::string& pluginId = "") = 0;
    // MIDI FX operations (type: "arpeggiator")
    virtual void addMidiFxSlot(int trackIndex, const std::string& type, int position = -1) = 0;
    virtual void removeMidiFxSlot(int trackIndex, int slotIndex) = 0;
    virtual void setMidiFxSlotBypassed(int trackIndex, int slotIndex, bool bypassed) = 0;
    virtual void setMidiFxSlotParam(int trackIndex, int slotIndex,
                                    const std::string& paramName, double value) = 0;
    virtual void removeFxSlot(int trackIndex, int slotIndex) = 0;
    virtual void setFxSlotBypassed(int trackIndex, int slotIndex, bool bypassed) = 0;
    virtual void setFxSlotParam(int trackIndex, int slotIndex, int paramIndex,
                                float value) = 0;
    virtual void reorderFxSlots(int trackIndex, int fromSlot, int toSlot) = 0;
    // Restart a crashed isolated plugin FX slot via the crash-recovery manager.
    virtual void respawnFxSlot(int trackIndex, int slotIndex) = 0;
    // FX chain presets (plan 2026-09-02-fx-chain-presets, Task 2). exportFxChain
    // snapshots a track's chain into an HDAW::ChainPreset (read-only: no tree
    // mutation, no undo, no rebuild; live plugin state is captured into the
    // tree first, ProjectSerializer.cpp:64-84 pattern). applyFxChain validates
    // the whole preset (Gate 9) before any write, replaces the chain in ONE
    // undo transaction with a SINGLE rebuildTrackFX at the end, and routes
    // param writes through setFxSlotParam (write-side clamp, lesson 23).
    // Missing sampler samples are skipped with an HDAW_LOG warning (never a
    // silent pass, Gate 2). Full type lives in engine/ChainLibrary.h (forward
    // declared above to avoid an include cycle).
    virtual HDAW::ChainPreset exportFxChain(int trackIndex) = 0;
    virtual bool applyFxChain(int trackIndex, const HDAW::ChainPreset& preset,
                              juce::String* error = nullptr) = 0;

    // Automation
    virtual bool addAutomationLane(int trackIndex, const std::string& laneName, int paramID = 0) = 0;
    virtual void removeAutomationLane(int trackIndex, const std::string& laneName) = 0;
    virtual void addAutomationPoint(int trackIndex, const std::string& lane,
                                     double time, float value) = 0;
    virtual void removeAutomationPoint(int trackIndex, const std::string& lane,
                                        double time) = 0;
    virtual void setAutomationEnabled(int trackIndex, const std::string& lane,
                                       bool enabled) = 0;
    // Disable/enable ALL Volume automation lanes on a track so the fader is
    // authoritative again (trackIndex -1 = every track). Non-destructive: only
    // toggles automationEnabled; automation points are kept. One undo unit.
    virtual void setFaderAuthoritative(int trackIndex, bool authoritative) = 0;
    virtual void setAutomationMode(int trackIndex, const std::string& laneName,
                                    const std::string& mode) = 0;
    virtual void notifyAutomationTouch(int trackIndex, int paramID, bool touching) = 0;

    // Transport properties
    virtual void setTempo(double bpm) = 0;

    // Tempo point operations (tempo map)
    virtual int addTempoPoint(double timeSeconds, double bpm) = 0;
    virtual void removeTempoPoint(int index) = 0;
    virtual void setTempoPointBpm(int index, double bpm) = 0;
    virtual void setTempoPointTime(int index, double timeSeconds) = 0;

    virtual void setLoopStart(double beat) = 0;
    virtual void setLoopEnd(double beat) = 0;
    virtual void setLooping(bool looping) = 0;
    virtual void setMetronomeEnabled(bool enabled) = 0;

    // Time signature
    virtual void setTimeSignature(int numerator, int denominator) = 0;

    // Markers
    virtual int addMarker(const std::string& name, double time, int color = 0xFF59e0c4) = 0;
    virtual void removeMarker(int index) = 0;
    virtual void setMarkerName(int index, const std::string& name) = 0;
    virtual void setMarkerTime(int index, double time) = 0;
    virtual void setClipName(int clipId, const std::string& name) = 0;

    // Arranger Regions
    virtual std::string addArrangerRegion(const std::string& name, double startTime, double duration, int color = 0xFFd97706) = 0;
    virtual void removeArrangerRegion(const std::string& regionID) = 0;
    virtual void setArrangerRegionName(const std::string& regionID, const std::string& name) = 0;
    virtual void setArrangerRegionBounds(const std::string& regionID, double startTime, double duration) = 0;
    virtual void setArrangerRegionColor(const std::string& regionID, int color) = 0;

    // Arranger Chains
    virtual std::string addArrangerChain(const std::string& name) = 0;
    virtual void removeArrangerChain(const std::string& chainID) = 0;
    virtual void setArrangerChainName(const std::string& chainID, const std::string& name) = 0;
    virtual void setArrangerChainActive(const std::string& chainID) = 0;

    // Chain Entries
    virtual int addChainEntry(const std::string& chainID, const std::string& regionID, int repeatCount = 1) = 0;
    virtual void removeChainEntry(const std::string& chainID, int entryIndex) = 0;
    virtual void reorderChainEntry(const std::string& chainID, int fromIndex, int toIndex) = 0;
    virtual void setChainEntryRepeat(const std::string& chainID, int entryIndex, int repeatCount) = 0;

    // Flatten
    virtual void flattenArranger() = 0;

    // Track operations — advanced
    virtual int duplicateTrack(int trackIndex) = 0;

    // FX — advanced
    virtual void setFxSlotPlugin(int trackIndex, int slotIndex, const std::string& fxType,
        const std::string& pluginID, const std::string& pluginFormat,
        const std::string& pluginPath) = 0;

    // Sampler — set sample file on a sampler FX slot
    virtual void setSamplerSample(int trackIndex, int slotIndex,
        const std::string& filePath, int rootNote = 60) = 0;

    // Sampler — set MIDI key range for multi-sampler routing (-1 = full range)
    virtual void setSamplerKeyRange(int trackIndex, int slotIndex,
        int keyLow, int keyHigh) = 0;

    // Automation — point mutation by time (for drag)
    virtual void setAutomationPointValue(int trackIndex, const std::string& lane,
        double time, float value) = 0;

    // Gain envelope (per-clip volume automation)
    virtual void addGainEnvelopePoint(int clipId, double time, double gain) = 0;
    virtual void moveGainEnvelopePoint(int clipId, int pointIndex, double time, double gain) = 0;
    virtual void removeGainEnvelopePoint(int clipId, int pointIndex) = 0;
    virtual void clearGainEnvelope(int clipId) = 0;
    // setClipGainEnvelope replaces the entire envelope with the given points.
    // Each pair is (time, gain). Implemented as clear + add-each inside a
    // single transaction so the whole replacement is one undo step.
    virtual void setClipGainEnvelope(int clipId,
                                     const std::vector<std::pair<double, double>>& points) = 0;
    virtual void notifyClipGainEnvelopeChanged(int clipId) = 0;

    // Envelope generation (beats at RPC boundary, seconds in tree)
    virtual void generateAutomationEnvelope(int trackIndex, const std::string& lane,
                                             const HDAW::EnvelopeGenerator::Params& params) = 0;
    // Automation preset bank (P2-3): generate named envelope recipes (pump /
    // macro / openClose / riser / sine / square) over BEAT windows and write
    // the points onto an EXISTING lane in ONE undo unit, enabling the lane.
    // Windows and times are beats at this boundary; the tree stores seconds
    // (the same beatsToSeconds conversion generateAutomationEnvelope uses,
    // with density scaled from per-beat to per-second). Returns "" on success
    // or an actionable error string (track/lane missing, bad window, no
    // windows); *pointsAdded receives the total points written (0 on error).
    virtual std::string applyAutomationPreset(int trackIndex, const std::string& laneName,
                                              const std::vector<HDAW::AutomationPreset::PresetWindow>& windows,
                                              bool clearWindowBeforeApply, uint64_t seed,
                                              int* pointsAdded) = 0;
    virtual void generateClipGainEnvelope(int clipId,
                                           const HDAW::EnvelopeGenerator::Params& params) = 0;
    virtual void generateClipCcLane(int clipId, int controllerNumber,
                                     const HDAW::EnvelopeGenerator::Params& params) = 0;

    // Modulation (LFO)
    virtual void addLfo(int trackIndex) = 0;
    virtual void removeLfo(int trackIndex, int lfoIndex) = 0;
    virtual void setLfoParam(int trackIndex, int lfoIndex,
                             const std::string& paramName, double value) = 0;

    // Slicing
    virtual void sliceClipAtTimes(int clipId, const std::vector<double>& times) = 0;
    virtual void sliceClipAtTransients(int clipId) = 0;
    virtual void sliceClipAtPlayhead(int clipId) = 0;
    // Batch slice: slice multiple clips at the playhead in one transaction.
    // Only one rebuildRoutingGraph() call at the end.
    virtual void sliceClipsAtPlayhead(const std::vector<int>& clipIds) = 0;
    // Batch slice: slice multiple clips at their detected transients in one transaction.
    virtual void sliceClipsAtTransients(const std::vector<int>& clipIds) = 0;

    // Region cut/copy/paste (audio clip editor)
    virtual int copyAudioClipRegion(int clipId, double regionStart, double regionEnd) = 0;
    virtual int cutAudioClipRegion(int clipId, double regionStart, double regionEnd) = 0;
    virtual int pasteAudioClipRegion(int clipId, double pasteTime) = 0;

    // MIDI CC
    virtual void addCcPoint(int clipId, int controllerNumber, double beat, int value) = 0;
    virtual void setCcPoint(int ccId, double beat, int value) = 0;
    virtual void removeCcPoint(int ccId) = 0;
    virtual void setCcRecordArmed(bool armed) = 0;
    virtual void setMidiNoteRecordArmed(bool armed) = 0;

    // Undo/redo
    virtual void undo() = 0;
    virtual void redo() = 0;
    virtual bool canUndo() const = 0;
    virtual bool canRedo() const = 0;
    // Undo/redo history (transaction names, oldest-first).
    virtual std::vector<std::string> getUndoDescriptions() const = 0;
    virtual std::vector<std::string> getRedoDescriptions() const = 0;

    // Transaction lifecycle (wraps UndoManager::beginNewTransaction / endTransaction)
    virtual void beginTransaction(const std::string& name) = 0;
    virtual void endTransaction() = 0;

    // Project lifecycle
    virtual void newProject() = 0;
    virtual bool saveProject(const std::string& filePath) = 0;
    virtual bool loadProject(const std::string& filePath) = 0;

    // Scale
    virtual void setScaleRoot(int root) = 0;
    virtual void setScaleMode(int mode) = 0;

    // Generative arrangement: create/find a track per part and place one MIDI
    // clip per part, all in a single transaction + graph rebuild.
    struct ArrangementResult {
        std::vector<int> trackIndices;
        std::vector<int> clipIds;
        std::vector<std::string> roleNames;
        int noteCount = 0;
        uint64_t seed = 0;
    };
    virtual ArrangementResult generateArrangement(const HDAW::ArrangementParams& params) = 0;

    // ── Instrument part composer ──
    // One command that builds a complete "instrument part": a track with an
    // instrument FX slot, a generated phrase, ghost copies painted across the
    // arrangement, and (optionally) gain-staging to a target RMS. All in beats
    // at this boundary (lesson #1); only the gain-staging render window is in
    // seconds. The composite is ONE undo unit ("Add instrument part"); the
    // gain-stage fader write is a SEPARATE undo unit ("Auto gain stage").
    // Role presets: a role turns one word into a full typed preset (Bass/Lead/
    // Chords/Drums → style + range + density + velocities + gain staging).
    // explicitMask marks which of the 9 role-defaultable fields the caller
    // explicitly provided; explicit values always win over role defaults.
    enum InstrumentPartRoleBit : uint32_t {
        kRoleBitStyle            = 1u << 0,
        kRoleBitLowNote          = 1u << 1,
        kRoleBitHighNote         = 1u << 2,
        kRoleBitDensity          = 1u << 3,
        kRoleBitNoteDuration     = 1u << 4,
        kRoleBitMinVelocity      = 1u << 5,
        kRoleBitMaxVelocity      = 1u << 6,
        kRoleBitTargetRms        = 1u << 7,
        kRoleBitAllowGlobalScale = 1u << 8,
    };

    struct InstrumentPartParams {
        std::string trackName;
        std::string style;              // PhraseGenerator style name
        std::string pluginId;           // empty = internal "fm_synth"
        int programIndex = -1;          // -1 = default program; >=0 requires pluginId
        double lengthBeats = 4.0;
        std::string placement = "region"; // "wholeSong" | "region"
        double startBeat = 0.0;
        int count = 1;                  // "region": total copies to paint (>=1)
        int scaleRoot = -1;             // -1 = use project scale root
        int scaleMode = -1;             // -1 = use project scale mode
        int density = 8;
        double noteDuration = 0.5;
        int lowNote = 48;
        int highNote = 84;
        int minVelocity = 60;
        int maxVelocity = 110;
        uint64_t seed = 0;
        float targetRms = 0.0f;         // >0 → run autoGainToTarget after building
        double windowSeconds = 4.0;
        bool verify = false;
        bool allowGlobalScale = false;  // permit the master-bus global-scale fallback
        std::string role;               // "" = none; else "bass"|"lead"|"chords"|"drums" (case-insensitive)
        uint32_t explicitMask = 0;      // bits set = caller explicitly provided the field
    };

    struct GainStageResult {
        bool ok = false;
        float fader = 1.0f;             // applied track volume
        float measuredRms = 0.0f;
        float peak = 0.0f;
        bool clamped = false;           // true if fader would exceed 1.0
        float globalScale = 1.0f;       // <1.0 when the master bus was scaled down
        float masterGain = 1.0f;        // resulting master gain (baseline when not scaled)
        float mixPeak = 0.0f;           // post-scale full-mix peak; 0 = not measured
        std::string error;
    };

    struct InstrumentPartResult {
        int trackIndex = -1;
        std::vector<int> clipIds;
        int noteCount = 0;
        std::string error;
        GainStageResult gain;           // populated only when targetRms > 0
    };

    virtual InstrumentPartResult addInstrumentPart(const InstrumentPartParams& params) = 0;
    virtual GainStageResult autoGainToTarget(int trackIndex, float targetRms,
                                             double windowSeconds = 4.0,
                                             bool verify = false,
                                             bool allowGlobalScale = false) = 0;

    // ── Psytrance score generation (plan 2026-08-30, W1) ──
    // Composes the FULL psytrance score (guide §4 grammar) onto the caller's
    // palette tracks in ONE undo unit: one clip per mapped role at beat 0
    // spanning the whole arrangement, key-disciplined notes, riser/downlifter
    // schedule into the drops. Notes only — kit + FX/LFO/automation stay
    // separate MCP-side steps. See engine/PsytranceGenerator.h.
    struct PsytranceResult {
        struct Clip { std::string role; int trackIndex = -1; int clipId = -1; int noteCount = 0; };
        std::vector<Clip> clips;
        std::vector<std::string> skippedRoles; // unmapped roles and roles with no notes
        double totalBeats = 0.0;
        int notesTotal = 0;
        int notesSkipped = 0;   // notes dropped past a clip's note ceiling
        std::string error;      // non-empty → nothing was written
    };

    virtual PsytranceResult generatePsytrance(const HDAW::PsytranceParams& params) = 0;

    // ── Psytrance INCREMENTAL Markov generation (guide §4B) ──
    // Same clip-writing contract as generatePsytrance (one clip per produced
    // role at beat 0 spanning totalBeats, notes clip-local, one undo unit),
    // but the score comes from the incremental 2-bar Markov engine: a pool of
    // role layers grows/changes window by window under min/max + percussive
    // sublimits, with age-biased replacement and a slow section-energy tier.
    // steps[] records every 2-bar decision (debug/verify); automations[] are
    // returned as DATA (FilterSweep filterCutoff points) — apply them with
    // the standard automation tools.
    struct PsytranceMarkovResult {
        struct Clip { std::string role; int trackIndex = -1; int clipId = -1; int noteCount = 0; };
        struct Step {
            int barStart = 0;
            std::string action;              // MarkovAction name
            std::string targetRole;
            std::vector<std::string> activeRoles;
            std::vector<int> ages;           // parallel to activeRoles (running bars)
            int keyRoot = -1;
            std::string section;             // sparse|build|peak|breakdown
        };
        struct Automation {
            std::string role;
            std::string param;               // "filterCutoff"
            double startBeat = 0.0;
            double value = 0.0;
            double durationBeats = 0.0;
        };
        std::vector<Clip> clips;
        std::vector<std::string> skippedRoles;
        std::vector<Step> steps;
        std::vector<Automation> automations;
        double totalBeats = 0.0;
        int notesTotal = 0;
        int notesSkipped = 0;   // notes dropped past a clip's note ceiling
        std::string error;      // non-empty → nothing was written
    };

    virtual PsytranceMarkovResult
    generatePsytranceMarkov(const HDAW::PsytranceMarkovParams& params) = 0;

    // ── Plugin preset audition ──
    // Solo-renders a plugin (on a temp probe track when trackIndex < 0, or an
    // existing plugin slot) over a short window and reports peak/rms/audible so
    // silent-at-default plugins stop being a blocker for composition. Probe
    // mode wraps the whole build in one undo unit that is reverted when
    // keepTrack=false (reporting trackIndex=-1) or on any failure — a failed
    // probe leaves the project untouched. Existing-slot mode (trackIndex >= 0)
    // sets the program + persists its state snapshot (same semantics as
    // load_plugin_preset) and does not roll back.
    struct AuditionParams {
        std::string pluginId;        // required when trackIndex < 0
        int programIndex = -1;       // -1 = current/default
        int trackIndex = -1;         // <0 = temp probe track
        int slotIndex = 0;           // used with trackIndex (existing plugin slot)
        std::string style = "Arpeggio";
        double lengthBeats = 4.0;
        int density = 8;
        double noteDuration = 0.5;
        int lowNote = 48;
        int highNote = 84;
        int minVelocity = 60;
        int maxVelocity = 110;
        uint64_t seed = 0;
        double windowSeconds = 4.0;
        bool keepTrack = false;
    };
    struct AuditionResult {
        bool ok = false;
        int trackIndex = -1;         // -1 when the probe track was removed
        int slotIndex = 0;
        int programIndex = -1;
        std::string programName;
        int numPrograms = 0;
        float rms = 0.0f;
        float peak = 0.0f;
        double durationSeconds = 0.0;
        bool audible = false;
        std::string error;
    };
    virtual AuditionResult auditionPlugin(const AuditionParams& params) = 0;

    // ── Part verification ──
    // Self-verification for composed parts: solo-renders the track's window AND
    // renders the full mix at the same window (both via the shared
    // renderTrackWindow), then reports levels, clipping, audibility and spectral
    // band presence. Read-only — never mutates the project.
    struct VerifyPartResult {
        bool ok = false;
        float soloRms = 0.0f, soloPeak = 0.0f;
        float mixRms = 0.0f, mixPeak = 0.0f;
        bool nonClipping = false;   // mixPeak < 1.0
        bool audible = false;       // soloPeak > 1e-4 (~ -80 dBFS)
        bool bandsPresent = false;  // bandLow && bandMid && bandHigh
        bool bandLow = false, bandMid = false, bandHigh = false;
        double windowStart = 0.0;
        double durationSeconds = 0.0;
        std::string error;
    };
    virtual VerifyPartResult verifyPart(int trackIndex, double windowSeconds = 4.0) = 0;

    // Missing source-file relinking. Searches the given directory (recursively)
    // for a file matching either (a) the exact filename, or (b) the same
    // basename with a different audio extension (wav/aiff/aif/mp3/flac/ogg).
    // Returns the found path or empty string if no match.
    virtual std::string findMissingClipSourceFile(int clipId, const std::string& searchDir) = 0;
    // relinkAllMissingFiles runs findMissingClipSourceFile for every clip
    // whose source file is missing and updates sourceFile on each hit.
    // Returns {foundCount, totalMissing}.
    struct RelinkResult { int found; int totalMissing; };
    virtual RelinkResult relinkAllMissingFiles(const std::string& searchDir) = 0;
};
