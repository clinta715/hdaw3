#pragma once
// Central, coverage-gated, PROOF-CARRYING unit annotation for MCP tool schemas.
//
// Slice S1 of docs/plans/2026-09-28-agent-mechanization.md (rev 2: proven
// classification). This file is header-only (inline) so no CMakeLists change is
// needed. It classifies the unit of every numeric (number/integer) property of
// every MCP tool schema WITHOUT any per-tool edits:
//
//   1. an explicit OVERRIDE table keyed "<tool>.<field>" for fields whose unit is
//      ambiguous or contradicts a grouped rule (each entry cites the handler
//      file:line that proves it);
//   2. exact NAME rules and camel-TOKEN suffix rules, kept ONLY where the full
//      live instance list agrees -- the instance list (tool.field + file:line) is
//      the comment above each rule;
//   3. a DIMENSIONLESS (scalar) classification for unitless ratios/indexes/counts
//      /enums/thresholds, again with per-rule live instance lists.
//
// resolveFieldUnit returns the unit string, "" for a classified scalar, or
// "?unclassified". Two gates protect this table:
//   * ToolRegistry.SchemaUnitsCoverEveryNumericField -- ?unclassified never
//     survives on the live surface;
//   * ToolRegistry.SchemaUnitsAreProvenClassified -- GATE E re-derives every
//     unit-bearing field's label from a hand-audited EXPECTED ledger, so a
//     mislabel or a silently-added unit-bearing field fails the build.
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace mcp {
namespace toolunits {

// ---- 0. PROOF CONVENTION ---------------------------------------------------
// Every rule below carries the COMPLETE list of live fields it resolves (the
// "instance list"), each with the handler/engine file:line that PROVES the unit.
// A grouped rule is kept only because its whole instance list agrees; the one
// field that disagrees is moved to overrideTable with its own proof.
// resolveFieldUnit consults: override -> nameUnits -> tokenUnits -> scalarNames
// -> scalarTokens. The ToolRegistry.SchemaUnitsAreProvenClassified gate
// re-derives this whole ledger from the live tools/list and fails on any drift.

// ---- 1. explicit "<tool>.<field>" overrides (ambiguous names + exceptions) ---
//
//   export_audio.start/.end -> seconds  (McpExportTool.cpp:68-69 feeds the value
//        to ExportManager in seconds: setCurrentSample(startTime*sr) at
//        ExportManager.cpp:390; totalSamples = duration*sampleRate at :541)
//   mix_report/.mix_verdict/.mix_diff .start/.end -> seconds  (section windows
//        parsed at McpTools_AudioRead.cpp:54-55/473-474 and scaled by sampleRate
//        at MixReport.h:308-309 -- NOT converted from beats)
//   seek.position -> seconds  (McpTools_Transport.cpp:67 sets IDs::position;
//        AudioEngine.cpp:986 consumes it as pos*sampleRate)
//   add_arranger_region / set_arranger_region_bounds .startTime/.duration ->
//        seconds  (Arranger.cpp:100-101/289-290: region times compared DIRECTLY
//        against clip startTime/duration, which are seconds)
//   set_audio_sample_rate.rate -> Hz  (McpTools_Settings.cpp:166)
//        NOTE: this is the ONLY live numeric `rate`; set_lfo_param has NO numeric
//        rate property (its rate travels inside the string `param`), so a global
//        rate->Hz rule would be a guess -- it is pinned here per instance instead.
//   automation_preset.midPoint -> beats  (AutomationPreset.h:161 clamps it INTO
//        the [start,end] BEAT window via clamp01_mid (:257-260), defaulting to
//        start+len*0.5 -- it is NOT a 0..1 fraction: was mislabelled scalar.)
//   set_note_pitch_offset.pitchOffset -> semitones (McpTools_Note.cpp:448 tool
//        text "per-note pitch offset in semitones"; was mislabelled scalar.)
//   add_cc_point.beatSec / set_cc_point.beatSec -> seconds  (S6 twin of the bare
//        `beat` (beats) argument; tokenization would otherwise see the leading
//        "beat" token and call it beats -- pinned here with its own proof.)
inline const QHash<QString, QString>& overrideTable()
{
    static const QHash<QString, QString> t{
        {"export_audio.start", "seconds"},
        {"export_audio.end", "seconds"},
        {"mix_report.start", "seconds"},
        {"mix_report.end", "seconds"},
        {"mix_verdict.start", "seconds"},
        {"mix_verdict.end", "seconds"},
        {"mix_diff.start", "seconds"},
        {"mix_diff.end", "seconds"},
        {"seek.position", "seconds"},
        {"add_arranger_region.starttime", "seconds"},
        {"add_arranger_region.duration", "seconds"},
        {"set_arranger_region_bounds.starttime", "seconds"},
        {"set_arranger_region_bounds.duration", "seconds"},
        {"set_audio_sample_rate.rate", "Hz"},
        {"automation_preset.midpoint", "beats"},
        {"set_note_pitch_offset.pitchoffset", "semitones"},
        {"add_cc_point.beatsec", "seconds"},
        {"set_cc_point.beatsec", "seconds"},
    };
    return t;
}

// ---- 2a. exact (lowercased) field-name units, with instance lists ----------
//
//   {"length", "beats"} -- clip length in beats (beatsToSeconds at the call sites)
//     add_audio_clip.length -> McpTools_Clip.cpp:68
//     add_midi_clip.length -> McpTools_Clip.cpp:41
//     generate_chord.length -> McpTools_CompositionGenerate.cpp:72
//     generate_phrase.length -> McpTools_CompositionGenerate.cpp:72
//   {"noteduration", "beats"} -- role/phrase note length in beats
//     add_instrument_part.noteDuration -> McpTools_CompositionInstrument.cpp:139
//     audition_plugin.noteDuration -> AudioEngineCommands_Composition.cpp:1259
//     generate_phrase.noteDuration -> PhraseGenerator.cpp:507
//   {"repeatrate", "beats"} -- note repeat rate = beat fraction
//     set_note_repeat_rate.repeatRate -> McpTools_Note.cpp:384
//   {"arpeggiorate", "beats"} -- arp note onset advance in beats (PhraseGenerator.cpp:1618)
//     generate_chord.arpeggioRate -> PhraseGenerator.cpp:1618
//     generate_progression.arpeggioRate -> PhraseGenerator.cpp:1618
//   {"times", "beats"} -- timeline-absolute beat positions
//     slice_clip_at_times.times -> McpTools_Clip.cpp:301
//   {"time", "beats"} -- automation point time in beats
//     add_automation_point.time -> McpTools_Automation.cpp:53
//     set_automation_points.time -> McpTools_Automation.cpp:97
//   {"startgte", "beats"} -- clip-local note start lower bound, in beats
//     list_notes.startGte -> McpTools_Note.cpp:324
//     remove_notes.startGte -> McpTools_Note.cpp:249
//     set_note_velocities.startGte -> McpTools_Note.cpp:186
//   {"startlt", "beats"} -- clip-local note start upper bound, in beats
//     list_notes.startLt -> McpTools_Note.cpp:325
//     remove_notes.startLt -> McpTools_Note.cpp:250
//     set_note_velocities.startLt -> McpTools_Note.cpp:187
//   {"start", "beats"} -- clip/note/automation/composition start in beats; the SECONDS exceptions are the override entries above
//     add_audio_clip.start -> McpTools_Clip.cpp:67
//     add_midi_clip.start -> McpTools_Clip.cpp:40
//     add_note.start -> McpTools_Note.cpp:50
//     add_notes.start -> McpTools_Note.cpp:128
//     apply_movement_plan.start -> MovementPlanJson.h:85
//     automation_preset.start -> AutomationPresetRequest.h:154/AudioEngineCommands_Automation.cpp:357
//     duplicate_clip.start -> McpTools_Clip.cpp:208
//     generate_automation_envelope.start -> AudioEngineCommands_Envelope.cpp:78
//     generate_chord.start -> McpTools_CompositionGenerate.cpp:70
//     generate_clip_cc_lane.start -> AudioEngineCommands_Envelope.cpp:178
//     generate_clip_gain_envelope.start -> AudioEngineCommands_Envelope.cpp:137
//     generate_phrase.start -> McpTools_CompositionGenerate.cpp:71
//     generate_progression.start -> McpTools_CompositionGenerate.cpp:70
//     generate_psytrance.start -> PsytranceGenerator.h:34
//     generate_rhythm_pattern.start -> McpTools_CompositionGenerate.cpp:70
//     move_clip.start -> McpTools_Clip.cpp:106
//     place_patterns.start -> PatternPlacer.h:83
//     set_clip.start -> McpTools_Clip.cpp:187
//     set_note.start -> McpTools_Note.cpp:154
//   {"end", "beats"} -- same convention as `start`
//     apply_movement_plan.end -> MovementPlanJson.h:86
//     automation_preset.end -> AutomationPresetRequest.h:155/AudioEngineCommands_Automation.cpp:358
//     generate_automation_envelope.end -> AudioEngineCommands_Envelope.cpp:79
//     generate_clip_cc_lane.end -> AudioEngineCommands_Envelope.cpp:178
//     generate_clip_gain_envelope.end -> AudioEngineCommands_Envelope.cpp:138
//     generate_psytrance.end -> PsytranceGenerator.h:35
//   {"duration", "beats"} -- clip/note duration in beats; arranger durations are override entries
//     add_note.duration -> McpTools_Note.cpp:51
//     add_notes.duration -> McpTools_Note.cpp:129
//     set_clip.duration -> McpTools_Clip.cpp:188
//     set_note.duration -> McpTools_Note.cpp:155
//   {"samplerate", "Hz"} -- audio sample rate in Hz
//     export_audio.sampleRate -> McpExportTool.cpp:146/ExportManager.cpp:387
//     rave_start_training.sampleRate -> RaveTrainingJobManager.cpp:240
//   {"fadein", "seconds"} -- clip fade-in seconds (fadeIn*sr at ClipSourceProcessor.h:431)
//     set_clip.fadeIn -> ClipSourceProcessor.h:431
//   {"fadeout", "seconds"} -- clip fade-out seconds (fadeOut*sr at ClipSourceProcessor.h:432)
//     set_clip.fadeOut -> ClipSourceProcessor.h:432
//   {"durationmin", "seconds"} -- audio-file duration filter in seconds
//     search_library.durationMin -> FileLibraryManager.cpp:1256
//   {"durationmax", "seconds"} -- audio-file duration filter in seconds
//     search_library.durationMax -> FileLibraryManager.cpp:1257
//   {"loopstart", "seconds"} -- transport loop region in seconds
//     transport.loopStart -> ReadModelImpl.cpp:476
//   {"loopend", "seconds"} -- transport loop region in seconds
//     transport.loopEnd -> ReadModelImpl.cpp:477
inline const QHash<QString, QString>& nameUnits()
{
    static const QHash<QString, QString> t{
        {"length", "beats"},
        {"noteduration", "beats"},
        {"repeatrate", "beats"},
        {"arpeggiorate", "beats"},
        {"times", "beats"},
        {"time", "beats"},
        {"startgte", "beats"},
        {"startlt", "beats"},
        {"start", "beats"},
        {"end", "beats"},
        {"duration", "beats"},
        {"samplerate", "Hz"},
        {"fadein", "seconds"},
        {"fadeout", "seconds"},
        {"durationmin", "seconds"},
        {"durationmax", "seconds"},
        {"loopstart", "seconds"},
        {"loopend", "seconds"},
    };
    return t;
}

// ---- 2b. camel-token convention table (+ live instance lists) --------------
// Tokens whose live-instance list is EMPTY are suffix VOCABULARY only: no live
// numeric field currently ends in them, so they are inert (a future such field
// would still be labelled by its explicit unit suffix, which is not a guess).
//
//   {"beat", "beats"} -- 18 live instance(s)
//     add_cc_point.beat -> McpTools_Cc.cpp:47
//     add_instrument_part.startBeat -> AudioEngineCommands_Composition.cpp:880
//     create_section.endBeat -> McpTools_Composition.cpp:173
//     create_section.startBeat -> McpTools_Composition.cpp:173
//     duplicate_region.endBeat -> McpTools_Clip.cpp:167
//     duplicate_region.startBeat -> McpTools_Clip.cpp:167
//     import_audio_file.startBeat -> AudioEngineCommands_Clips.cpp:48
//     insert_silence.endBeat -> AudioEngineCommands_Clips.cpp:565
//     insert_silence.startBeat -> AudioEngineCommands_Clips.cpp:564
//     param_verity.startBeat -> AudioEngineCommands_Composition.cpp:1664
//     param_verity_corpus.startBeat -> AudioEngineCommands_Composition.cpp:1925
//     place_patterns.startBeat -> AudioEngineCommands_Composition.cpp:1202
//     ripple_delete.endBeat -> AudioEngineCommands_Clips.cpp:479
//     ripple_delete.startBeat -> AudioEngineCommands_Clips.cpp:478
//     set_cc_point.beat -> McpTools_Cc.cpp:90
//     tone_verity.startBeat -> AudioEngineCommands_Composition.cpp:1805
//     verify_part.endBeat -> AudioEngineCommands_Composition.cpp:1511
//     verify_part.startBeat -> AudioEngineCommands_Composition.cpp:1511
//   {"beat", "beats"} -- 18 live instance(s)
//     add_cc_point.beat -> McpTools_Cc.cpp:47
//     add_instrument_part.startBeat -> AudioEngineCommands_Composition.cpp:880
//     create_section.endBeat -> McpTools_Composition.cpp:173
//     create_section.startBeat -> McpTools_Composition.cpp:173
//     duplicate_region.endBeat -> McpTools_Clip.cpp:167
//     duplicate_region.startBeat -> McpTools_Clip.cpp:167
//     import_audio_file.startBeat -> AudioEngineCommands_Clips.cpp:48
//     insert_silence.endBeat -> AudioEngineCommands_Clips.cpp:565
//     insert_silence.startBeat -> AudioEngineCommands_Clips.cpp:564
//     param_verity.startBeat -> AudioEngineCommands_Composition.cpp:1664
//     param_verity_corpus.startBeat -> AudioEngineCommands_Composition.cpp:1925
//     place_patterns.startBeat -> AudioEngineCommands_Composition.cpp:1202
//     ripple_delete.endBeat -> AudioEngineCommands_Clips.cpp:479
//     ripple_delete.startBeat -> AudioEngineCommands_Clips.cpp:478
//     set_cc_point.beat -> McpTools_Cc.cpp:90
//     tone_verity.startBeat -> AudioEngineCommands_Composition.cpp:1805
//     verify_part.endBeat -> AudioEngineCommands_Composition.cpp:1511
//     verify_part.startBeat -> AudioEngineCommands_Composition.cpp:1511
//   {"bar", "bars"} -- 1 live instance(s)
//     place_patterns.startBar -> MidiAnalyzer.cpp:460
//   {"bars", "bars"} -- 11 live instance(s)
//     generate_arrangement.bars -> AudioEngineCommands_Clips.cpp:778
//     generate_arrangement_corpus.bars -> CorpusArranger.cpp:108
//     generate_chopped_break.bars -> BreakPatternGenerator.h:165
//     generate_psytrance_markov.bars -> McpTools_CompositionGenerate.cpp:671/MarkovArranger.cpp:153
//     generate_psytrance_markov.everyBars -> MarkovArranger.cpp:506
//     generate_psytrance_markov.sectionCycleBars -> MarkovArranger.cpp:351
//     generate_psytrance_markov.totalBars -> MarkovArranger.cpp:209
//     generate_rhythm_pattern.bars -> McpTools_CompositionGenerate.cpp:355
//     place_patterns.lengthBars -> MidiAnalyzer.h:48
//     set_song_plan.bars -> AudioEngineCommands_Song.cpp:194
//     set_song_plan.totalBars -> AudioEngineCommands_Song.cpp:151
//   {"sec", "seconds"} -- NO live instance (unmatched vocabulary)
//   {"secs", "seconds"} -- NO live instance (unmatched vocabulary)
//   {"second", "seconds"} -- NO live instance (unmatched vocabulary)
//   {"seconds", "seconds"} -- 14 live instance(s)
//     add_instrument_part.windowSeconds -> AudioEngineCommands_Composition.cpp:573
//     add_tempo_point.timeSeconds -> ReadModelImpl.cpp:824
//     audition_plugin.windowSeconds -> AudioEngineCommands_Composition.cpp:1437
//     auto_gain_to_target.windowSeconds -> AudioEngineCommands_Composition.cpp:977
//     auto_gain_tracks.windowSeconds -> AudioEngineCommands_Composition.cpp:1100
//     diagnose_intro_blast.binSeconds -> MixReport.h:496
//     diagnose_intro_blast.windowSeconds -> MixReport.h:495
//     mix_verdict.introSeconds -> MixReport.h:495
//     param_verity.windowSeconds -> AudioEngineCommands_Composition.cpp:1666
//     param_verity_corpus.windowSeconds -> AudioEngineCommands_Composition.cpp:1924
//     set_tempo_point_time.timeSeconds -> ReadModelImpl.cpp:824
//     tone_verity.binSeconds -> ToneVerity.cpp:118
//     tone_verity.windowSeconds -> AudioEngineCommands_Composition.cpp:1806
//     verify_part.windowSeconds -> AudioEngineCommands_Composition.cpp:1525
//   {"ms", "ms"} -- 4 live instance(s)
//     export_audio.waitTimeoutMs -> McpExportTool.cpp:164
//     rave_set_config.timeoutMs -> RaveTrainingJobManager.cpp:307
//     tone_verity.attackMsMax -> ToneVerity.cpp:170,385
//     tone_verity.attackMsMin -> ToneVerity.cpp:170,383
//   {"millisecond", "ms"} -- NO live instance (unmatched vocabulary)
//   {"milliseconds", "ms"} -- NO live instance (unmatched vocabulary)
//   {"bpm", "bpm"} -- 13 live instance(s)
//     add_tempo_point.bpm -> AudioEngineCommands_Transport.cpp:27
//     mix_diff.bpm -> MixReport.h:363
//     mix_report.bpm -> McpTools_AudioRead.cpp:545
//     mix_verdict.bpm -> McpTools_AudioRead.cpp:456
//     preview_set_project_bpm.bpm -> AudioPreviewPlayer.cpp:68
//     preview_set_tempo_match.fileBpm -> AudioPreviewPlayer.cpp:68
//     search_library.bpmMax -> FileLibraryManager.cpp:1260
//     search_library.bpmMin -> FileLibraryManager.cpp:1259
//     set_clip_source_bpm.bpm -> AudioEngineCommands_Timestretch.cpp:35
//     set_song_plan.bpm -> AudioEngineCommands_Song.cpp:175
//     set_tempo.bpm -> AudioEngineCommands_Transport.cpp:10
//     set_tempo_point_bpm.bpm -> AudioEngineCommands_Transport.cpp:47
//     setup_remix.bpm -> McpTools_Composition.cpp:107
//   {"hz", "Hz"} -- 2 live instance(s)
//     tone_verity.f0Hz -> ToneVerity.cpp:420
//     tone_verity.modRateHz -> ToneVerity.cpp:400
//   {"db", "dB"} -- NO live instance (unmatched vocabulary)
//   {"pct", "percent"} -- 1 live instance(s)
//     tone_verity.modRateTolPct -> ToneVerity.cpp:400
//   {"percent", "percent"} -- 1 live instance(s)
//     generate_arrangement.swingPercent -> ArrangementGenerator.cpp:104
//   {"samples", "samples"} -- NO live instance (unmatched vocabulary)
//   {"semi", "semitones"} -- NO live instance (unmatched vocabulary)
//   {"semis", "semitones"} -- NO live instance (unmatched vocabulary)
//   {"semitone", "semitones"} -- NO live instance (unmatched vocabulary)
//   {"semitones", "semitones"} -- NO live instance (unmatched vocabulary)
//   {"cent", "cents"} -- NO live instance (unmatched vocabulary)
//   {"cents", "cents"} -- 1 live instance(s)
//     tone_verity.f0CentsMax -> ToneVerity.cpp:420
inline const QHash<QString, QString>& tokenUnits()
{
    static const QHash<QString, QString> t{
        {"beat", "beats"},
        {"beats", "beats"},
        {"bar", "bars"},
        {"bars", "bars"},
        {"sec", "seconds"},
        {"secs", "seconds"},
        {"second", "seconds"},
        {"seconds", "seconds"},
        {"ms", "ms"},
        {"millisecond", "ms"},
        {"milliseconds", "ms"},
        {"bpm", "bpm"},
        {"hz", "Hz"},
        {"db", "dB"},
        {"pct", "percent"},
        {"percent", "percent"},
        {"samples", "samples"},
        {"semi", "semitones"},
        {"semis", "semitones"},
        {"semitone", "semitones"},
        {"semitones", "semitones"},
        {"cent", "cents"},
        {"cents", "cents"},
    };
    return t;
}
// ---- 3. dimensionless scalar classification, with live instance lists -----
// A scalar field is NOT a unit: it is a ratio/index/count/enum/threshold that
// has no physical unit. The AMBIGUOUS ones (name could hide a unit) were read
// from the handler; their proofs are listed after the scalar table in GATE E's
// EXPECTED ledger. Instances per rule (deduped tool.field):

//
// Ambiguous-scalar audit (handler proof that the field is dimensionless). These
// are re-asserted as scalar by GATE E's EXPECTED ledger:
//   add_fx.position -> McpTools_FxSlot.cpp:165 (slot INSERT INDEX, clamped to chain length)
//   add_instrument_part.targetRms -> AudioEngineCommands_Composition.cpp:947 (delegates to autoGainToTarget)
//   add_midi_fx.position -> McpTools_MidiFx.cpp:41 (slot insert index)
//   add_send.level -> McpTools_Send.cpp:197 (linear send level, default 1.0)
//   audition_patch.root -> McpTools_FxSlot.cpp:910 (MIDI note 0..127)
//   auto_gain_to_target.targetRms -> AudioEngineCommands_Composition.cpp:991 (fader = targetRms/raw.rms: linear RMS amplitude, not dBFS)
//   auto_gain_tracks.targetRms -> AudioEngineCommands_Composition.cpp:1100 (same linear RMS amplitude)
//   automation_preset.cycles -> AutomationPreset.h:146,155,188 (cycle count; default = window beats)
//   automation_preset.midPoint -> OVERRIDE -> beats (AutomationPreset.h:161)
//   generate_automation_envelope.cycles -> McpTools_Envelope.cpp:108 (cycle COUNT)
//   generate_automation_envelope.density -> McpTools_Envelope.cpp:111 (densityPerSec: events/second, not a musical unit)
//   generate_automation_envelope.phase -> McpTools_Envelope.cpp:110 (normalized phase offset)
//   generate_automation_envelope.smooth -> McpTools_Envelope.cpp:112 (0..1 smoothing)
//   generate_automation_envelope.steps -> McpTools_Envelope.cpp:109 (step COUNT)
//   generate_clip_cc_lane.cycles -> McpTools_Envelope.cpp:194
//   generate_clip_cc_lane.density -> McpTools_Envelope.cpp:197
//   generate_clip_cc_lane.phase -> McpTools_Envelope.cpp:196
//   generate_clip_cc_lane.smooth -> McpTools_Envelope.cpp:198
//   generate_clip_cc_lane.steps -> McpTools_Envelope.cpp:195
//   generate_clip_gain_envelope.cycles -> McpTools_Envelope.cpp:149
//   generate_clip_gain_envelope.density -> McpTools_Envelope.cpp:152
//   generate_clip_gain_envelope.phase -> McpTools_Envelope.cpp:151
//   generate_clip_gain_envelope.smooth -> McpTools_Envelope.cpp:153
//   generate_clip_gain_envelope.steps -> McpTools_Envelope.cpp:150
//   key_check.root -> McpTools_CompositionPattern.cpp:172
//   list_device_params.limit -> McpTools_Device.cpp:123 (max rows)
//   place_patterns.frequency -> MidiAnalyzer.h:51 ("how often this pattern repeats": repeat COUNT, not Hz)
//   preview_set_volume.volume -> McpTools_Settings.cpp:302 (preview gain 0.0..1.0)
//   psy_fm_set_mod_route.depth -> McpTools_PsyFm.cpp:139 (modulation-matrix depth ratio)
//   redo.count -> McpTools_Transport.cpp:81-82 (N actions)
//   search_library.limit -> McpTools_Library.cpp:206 (max results)
//   search_library.offset -> McpTools_Library.cpp:205 (pagination offset into the result set)
//   seek.position -> OVERRIDE -> seconds (McpTools_Transport.cpp:67)
//   set_audio_buffer_size.size -> McpTools_Settings.cpp:179 (device buffer FRAME count)
//   set_bus_fx_param.value -> McpTools_Send.cpp:288 (REAL units per list_bus_fx_params -> param-dependent)
//   set_clip.gain -> McpTools_Clip.cpp:189 (linear clip gain)
//   set_clip_stretch_mode.mode -> McpTools_Clip.cpp:350 (enum 0=Off 1=TempoMatch 2=ManualRatio)
//   set_clip_stretch_ratio.ratio -> McpTools_Clip.cpp:363 (stretch ratio 0.25..4.0)
//   set_fx_param.value -> McpTools_FxSlot.cpp:337 (NORMALIZED 0..1)
//   set_internal_fx_param.value -> McpTools_FxSlot.cpp:720 (param REAL units: Hz/dB/ratio per paramIndex -> unit is param-dependent)
//   set_lfo_param.value -> McpTools_Modulation.cpp:48-49 (param-dependent: rate Hz/cycles-per-beat, depth, phaseOffset degrees)
//   set_master_gain.gain -> McpTools_Track.cpp:197 (master bus gain, linear >= 0)
//   set_midi_fx_param.value -> McpTools_MidiFx.cpp:88 (param's own range -> param-dependent)
//   set_note_gain.gain -> McpTools_Note.cpp:424 (per-note gain multiplier 0.0..2.0)
//   set_note_pitch_offset.pitchOffset -> OVERRIDE -> semitones (McpTools_Note.cpp:448)
//   set_scale.mode -> McpTools_CompositionTiming.cpp:35 (scale mode index 0..20)
//   set_scale.root -> McpTools_CompositionTiming.cpp:34 (pitch class 0..11)
//   set_track.volume -> McpTools_Track.cpp:182 (linear track volume; default fader 0.85 at :61)
//   set_track_send_level.level -> McpTools_Send.cpp:74 (linear send level)
//   undo.count -> McpTools_Transport.cpp:71-72 (N actions)
//   {"bustarget", ""} -- 3: add_bus.busTarget, add_send.busTarget, set_bus_target.busTarget
//   {"denominator", ""} -- 1: set_time_signature.denominator
//   {"ghostfills", ""} -- 1: generate_chopped_break.ghostFills
//   {"height", ""} -- 1: set_track.height
//   {"k", ""} -- 1: cluster_library.k
//   {"numerator", ""} -- 1: set_time_signature.numerator
//   {"occurrence", ""} -- 1: set_note_occurrence.occurrence
//   {"pattern", ""} -- 1: generate_progression.pattern
//   {"recurrence", ""} -- 1: set_note_recurrence.recurrence
//   {"repetitions", ""} -- 1: loop_clip.repetitions
//   {"style", ""} -- 1: generate_arrangement.style
//   {"targetrms", ""} -- 3: add_instrument_part.targetRms, auto_gain_to_target.targetRms, auto_gain_tracks.targetRms
//   {"volume", ""} -- 2: preview_set_volume.volume, set_track.volume
inline const QHash<QString, QString>& scalarNames() // lowercased exact names
{
    static const QHash<QString, QString> t{
        {"k", ""},
        {"item", ""},
        {"items", ""},
        {"properties", ""},
        {"bustarget", ""},
        {"denominator", ""},
        {"ghostfills", ""},
        {"height", ""},
        {"numerator", ""},
        {"occurrence", ""},
        {"pattern", ""},
        {"recurrence", ""},
        {"repetitions", ""},
        {"style", ""},
        {"targetrms", ""},
        {"volume", ""},
    };
    return t;
}

//   {"bank", ""} -- 2: apply_preset.bank, load_virus_preset.bank
//   {"baseline", ""} -- 2: param_verity.baselineRuns, param_verity_corpus.baselineRuns
//   {"batch", ""} -- 1: rave_start_training.batchSize
//   {"bus", ""} -- 6: add_track.parentBus, add_track_with_fx.parentBus, list_bus_fx_params.busID, remove_bus.busID, set_bus_fx_param.busID, set_bus_target.busID
//   {"chance", ""} -- 1: set_note_chance.chance
//   {"channel", ""} -- 3: apply_preset.channel, load_virus_preset.channel, set_track.midiChannel
//   {"clip", ""} -- 36: add_cc_point.clipId, add_note.clipId, add_notes.clipId, align_clip_to_grid.clipId, clear_notes.clipId, duplicate_clip.clipId, fit_clip_to_loop.clipId, generate_chopped_break.clipId, generate_clip_cc_lane.clipId, generate_clip_gain_envelope.clipId, get_cc_points.clipId, get_clip.clipId, get_clip_provenance.clipId, get_waveform_peaks.clipId, list_clip_takes.clipId, list_notes.clipId, loop_clip.clipId, move_clip.clipId, place_patterns.clipId, rave_transform_clip.clipId, remove_clip.clipId, remove_notes.clipId, session_set_clip_scene.clipId, set_clip.clipId, set_clip_seed.clipId, set_clip_source_bpm.clipId, set_clip_stretch_mode.clipId, set_clip_stretch_ratio.clipId, set_note_velocities.clipId, slice_clip_at_playhead.clipId, slice_clip_at_times.clipId, slice_clip_at_transients.clipId, slice_clips_at_playhead.clipIds, slice_clips_at_transients.clipIds, switch_clip_take.clipId, tempo_match_clip.clipId
//   {"color", ""} -- 5: add_arranger_region.color, add_track.color, add_track_with_fx.color, set_arranger_region_color.color, set_track.color
//   {"complexity", ""} -- 1: generate_arrangement.complexity
//   {"count", ""} -- 6: add_chain_entry.repeatCount, add_instrument_part.count, redo.count, set_chain_entry_repeat.repeatCount, set_note_repeat_count.repeatCount, undo.count
//   {"curve", ""} -- 1: set_note_repeat_curve.repeatCurve
//   {"cycles", ""} -- 4: automation_preset.cycles, generate_automation_envelope.cycles, generate_clip_cc_lane.cycles, generate_clip_gain_envelope.cycles
//   {"degree", ""} -- 1: scale_note.degree
//   {"density", ""} -- 8: add_instrument_part.density, audition_plugin.density, generate_automation_envelope.density, generate_clip_cc_lane.density, generate_clip_gain_envelope.density, generate_phrase.density, generate_psytrance.density, generate_psytrance_markov.density
//   {"depth", ""} -- 2: export_audio.bitDepth, psy_fm_set_mod_route.depth
//   {"epochs", ""} -- 1: rave_start_training.epochs
//   {"frequency", ""} -- 1: place_patterns.frequency
//   {"gain", ""} -- 3: set_clip.gain, set_master_gain.gain, set_note_gain.gain
//   {"grid", ""} -- 3: detect_sampler_slices.sliceGrid, generate_chopped_break.grid, generate_rhythm_pattern.grid
//   {"id", ""} -- 15: add_automation_lane.paramID, apply_movement_plan.paramID, move_track_into_folder.folderID, move_track_into_folder.folderId, poll_job.jobId, rave_cancel_job.jobId, rave_cancel_training_job.jobId, rave_job_status.jobId, rave_training_job_status.jobId, remove_cc_point.ccId, remove_send.sendID, set_cc_point.ccId, set_track_send_bypassed.sendID, set_track_send_level.sendID, set_track_send_mode.sendID
//   {"index", ""} -- 30: apply_preset.voiceIndex, fm_synth_import_sysex.voiceIndex, generate_rhythm_pattern.phraseIndex, move_track.newIndex, param_verity.paramIndex, remove_chain_entry.entryIndex, remove_lfo.lfoIndex, remove_send.sendIndex, remove_tempo_point.index, reorder_chain_entry.fromIndex, reorder_chain_entry.toIndex, session_create_clip.sceneIndex, session_launch_scene.sceneIndex, session_set_clip_scene.sceneIndex, set_bus_fx_param.paramIndex, set_chain_entry_repeat.entryIndex, set_fx_param.paramIndex, set_internal_fx_param.paramIndex, set_lfo_param.lfoIndex, set_master_fx_param.paramIndex, set_midi_fx_param_normalized.paramIndex, set_sampler_param.paramIndex, set_tempo_point_bpm.index, set_tempo_point_time.index, set_track_send_bypassed.sendIndex, set_track_send_level.sendIndex, set_track_send_mode.sendIndex, sub_synth_import_sysex.voiceIndex, switch_clip_take.takeIndex, trigger_sampler_slice.sliceIndex
//   {"indexes", ""} -- 1: param_verity_corpus.paramIndexes
//   {"inversion", ""} -- 1: generate_chord.inversion
//   {"key", ""} -- 7: generate_arrangement_corpus.keyRoot, generate_psytrance.keyRoot, generate_psytrance_markov.keyRoot, generate_psytrance_markov.keyShiftDegrees, set_sampler_key_range.keyHigh, set_sampler_key_range.keyLow, set_song_plan.keyRoot
//   {"level", ""} -- 2: add_send.level, set_track_send_level.level
//   {"limit", ""} -- 5: cluster_library.memberLimit, list_device_params.limit, related_samples.limit, search_library.limit, search_plugin_presets.limit
//   {"max", ""} -- 9: add_instrument_part.maxVelocity, audition_plugin.maxVelocity, diagnose_intro_blast.maxTracks, generate_chord.maxVelocity, generate_phrase.maxVelocity, generate_progression.maxVelocity, generate_psytrance_markov.maxPercTracks, generate_psytrance_markov.maxTracks, param_verity_corpus.maxParams
//   {"min", ""} -- 8: add_instrument_part.minVelocity, audition_plugin.minVelocity, generate_chord.minVelocity, generate_phrase.minVelocity, generate_progression.minVelocity, generate_psytrance_markov.minPercTracks, generate_psytrance_markov.minTracks, tone_verity.centroidRiseMin
//   {"mode", ""} -- 14: add_instrument_part.scaleMode, generate_arrangement.scaleMode, generate_arrangement_corpus.scaleMode, generate_chord.scaleMode, generate_phrase.scaleMode, generate_progression.scaleMode, generate_psytrance.scaleMode, generate_psytrance_markov.melodyTransposeMode, generate_psytrance_markov.scaleMode, key_check.scaleMode, set_clip_stretch_mode.mode, set_scale.mode, set_song_plan.scaleMode, setup_remix.scaleMode
//   {"mutation", ""} -- 1: generate_psytrance_markov.melodyContourMutation
//   {"note", ""} -- 25: add_instrument_part.highNote, add_instrument_part.lowNote, audition_plugin.highNote, audition_plugin.lowNote, generate_chord.highNote, generate_chord.lowNote, generate_phrase.highNote, generate_phrase.lowNote, generate_progression.highNote, generate_progression.lowNote, list_notes.noteIds, remove_notes.noteIds, set_note.noteId, set_note_chance.noteId, set_note_gain.noteId, set_note_occurrence.noteId, set_note_pan.noteId, set_note_pitch_offset.noteId, set_note_pressure.noteId, set_note_recurrence.noteId, set_note_repeat_count.noteId, set_note_repeat_curve.noteId, set_note_repeat_rate.noteId, set_note_timbre.noteId, set_note_velocities.noteIds
//   {"num", ""} -- 1: get_waveform_peaks.numBins
//   {"number", ""} -- 3: add_cc_point.controllerNumber, generate_clip_cc_lane.controllerNumber, get_cc_points.controllerNumber
//   {"octave", ""} -- 2: place_patterns.octave, scale_note.octave
//   {"offset", ""} -- 1: search_library.offset
//   {"pan", ""} -- 2: set_note_pan.pan, set_track.pan
//   {"phase", ""} -- 3: generate_automation_envelope.phase, generate_clip_cc_lane.phase, generate_clip_gain_envelope.phase
//   {"pitch", ""} -- 7: add_note.pitch, add_notes.pitch, generate_rhythm_pattern.dslPitch, generate_rhythm_pattern.pitchA, generate_rhythm_pattern.pitchB, place_patterns.pitch, set_note.pitch
//   {"pitches", ""} -- 3: list_notes.pitches, remove_notes.pitches, set_note_velocities.pitches
//   {"position", ""} -- 2: add_fx.position, add_midi_fx.position
//   {"preset", ""} -- 1: load_je8086_preset.preset
//   {"pressure", ""} -- 1: set_note_pressure.pressure
//   {"prob", ""} -- 3: generate_arrangement_corpus.melodyCorpusPhraseProb, generate_psytrance_markov.melodyCorpusPhraseProb, generate_psytrance_markov.percCorpusPhraseProb
//   {"program", ""} -- 6: add_instrument_part.programIndex, apply_preset.program, audition_plugin.programIndex, load_nord_bank.program, load_plugin_preset.programIndex, load_virus_preset.program
//   {"progression", ""} -- 6: generate_arrangement_corpus.progressionA, generate_arrangement_corpus.progressionB, generate_psytrance.progressionA, generate_psytrance.progressionB, generate_psytrance_markov.progressionA, generate_psytrance_markov.progressionB
//   {"pulse", ""} -- 2: generate_rhythm_pattern.pulseA, generate_rhythm_pattern.pulseB
//   {"ratio", ""} -- 4: mix_report.dropBuildRatio, mix_verdict.dropBuildRatio, set_clip_stretch_ratio.ratio, tone_verity.sustainRatioMin
//   {"root", ""} -- 14: add_instrument_part.scaleRoot, audition_patch.root, generate_arrangement.scaleRoot, generate_chord.rootPitch, generate_chord.scaleRoot, generate_phrase.scaleRoot, generate_progression.scaleRoot, key_check.root, rave_import_result.samplerRootNote, rave_transform_clip.samplerRootNote, sampler_set_sample.rootNote, scale_note.rootMidi, set_scale.root, setup_remix.scaleRoot
//   {"rotation", ""} -- 2: generate_rhythm_pattern.rotationA, generate_rhythm_pattern.rotationB
//   {"seed", ""} -- 23: add_instrument_part.seed, apply_movement_plan.seed, audition_plugin.seed, automation_preset.seed, generate_arrangement.seed, generate_arrangement_corpus.seed, generate_automation_envelope.seed, generate_chopped_break.seed, generate_chord.seed, generate_clip_cc_lane.seed, generate_clip_gain_envelope.seed, generate_phrase.seed, generate_progression.seed, generate_psytrance.seed, generate_psytrance_markov.seed, rave_start_transform.seed, rave_transform_clip.seed, rave_transform_file.seed, select_patch.seed, set_cell.seed, set_cells.seed, set_clip_seed.seed, set_song_plan.seed
//   {"sensitivity", ""} -- 1: detect_sampler_slices.sliceSensitivity
//   {"size", ""} -- 1: set_audio_buffer_size.size
//   {"slot", ""} -- 51: apply_matrix_preset.slotIndex, apply_preset.slotIndex, apply_sub_synth_mod_preset.slotIndex, audition_plugin.slotIndex, capture_fx_snapshot.slotIndex, clear_fx_param_overrides.slotIndex, detect_sampler_slices.slotIndex, fm_synth_get_state.slotIndex, fm_synth_import_sysex.slotIndex, fm_synth_load_preset.slotIndex, generate_chopped_break.slotIndex, get_fx_capture_status.slotIndex, get_internal_fx_param.slotIndex, list_fx_params.slotIndex, list_midi_fx_params.slotIndex, list_plugin_presets.slotIndex, load_je8086_preset.slotIndex, load_nord_bank.slotIndex, load_plugin_preset.slotIndex, load_plugin_preset_file.slotIndex, load_virus_preset.slotIndex, param_verity.slotIndex, param_verity_corpus.slotIndex, psy_fm_clear_mod_matrix.slotIndex, psy_fm_get_analysis.slotIndex, psy_fm_load_preset.slotIndex, psy_fm_mod_matrix_debug.slotIndex, psy_fm_set_mod_route.slotIndex, rave_import_result.samplerSlotIndex, rave_transform_clip.samplerSlotIndex, remove_fx.slotIndex, remove_midi_fx.slotIndex, restart_fx.slotIndex, sampler_get_state.slotIndex, sampler_set_sample.slotIndex, send_fx_midi.slotIndex, set_fx_bypass.slotIndex, set_fx_param.slotIndex, set_internal_fx_param.slotIndex, set_master_fx_bypassed.slotIndex, set_master_fx_param.slotIndex, set_midi_fx_bypass.slotIndex, set_midi_fx_param.slotIndex, set_midi_fx_param_normalized.slotIndex, set_sampler_key_range.slotIndex, set_sampler_mode.slotIndex, set_sampler_param.slotIndex, sub_synth_import_sysex.slotIndex, swap_fx_snapshot.slotIndex, toggle_plugin_editor.slotIndex, trigger_sampler_slice.slotIndex
//   {"smooth", ""} -- 3: generate_automation_envelope.smooth, generate_clip_cc_lane.smooth, generate_clip_gain_envelope.smooth
//   {"steps", ""} -- 5: generate_automation_envelope.steps, generate_clip_cc_lane.steps, generate_clip_gain_envelope.steps, param_verity.steps, param_verity_corpus.steps
//   {"temperature", ""} -- 3: rave_start_transform.temperature, rave_transform_clip.temperature, rave_transform_file.temperature
//   {"timbre", ""} -- 1: set_note_timbre.timbre
//   {"track", ""} -- 176: add_audio_clip.trackId, add_automation_lane.trackID, add_automation_lane.trackId, add_automation_point.trackID, add_automation_point.trackId, add_fx.trackID, add_fx.trackId, add_lfo.trackId, add_midi_clip.trackId, add_midi_fx.trackID, add_midi_fx.trackId, add_send.trackID, add_send.trackId, apply_matrix_preset.trackID, apply_matrix_preset.trackId, apply_movement_plan.trackID, apply_movement_plan.trackId, apply_preset.trackID, apply_preset.trackId, apply_sub_synth_mod_preset.trackID, apply_sub_synth_mod_preset.trackId, audition_patch.trackID, audition_patch.trackId, audition_plugin.trackIndex, auto_gain_to_target.trackId, auto_gain_tracks.trackId, automation_preset.trackID, automation_preset.trackId, capture_fx_snapshot.trackIndex, clear_fx_param_overrides.trackID, clear_fx_param_overrides.trackId, clear_layer_handoff.trackId, debug_audio.trackId, detect_sampler_slices.trackID, detect_sampler_slices.trackId, duplicate_clip.trackId, duplicate_track.trackID, duplicate_track.trackId, export_audio.trackIds, fm_synth_get_state.trackID, fm_synth_get_state.trackId, fm_synth_import_sysex.trackID, fm_synth_import_sysex.trackId, fm_synth_load_preset.trackID, fm_synth_load_preset.trackId, generate_automation_envelope.trackID, generate_automation_envelope.trackId, generate_chopped_break.trackId, generate_chord.trackId, generate_phrase.trackId, generate_progression.trackId, generate_rhythm_pattern.trackId, get_fx_capture_status.trackID, get_fx_capture_status.trackId, get_internal_fx_param.trackID, get_internal_fx_param.trackId, get_layer_handoffs.trackId, get_track_sends.trackID, get_track_sends.trackId, import_audio_file.trackIndex, list_automation_lanes.trackId, list_clips.trackId, list_fx.trackId, list_fx_params.trackID, list_fx_params.trackId, list_lfos.trackId, list_midi_fx_params.trackID, list_midi_fx_params.trackId, list_plugin_presets.trackID, list_plugin_presets.trackId, load_fx_chain.trackID, load_fx_chain.trackId, load_je8086_preset.trackID, load_je8086_preset.trackId, load_nord_bank.trackID, load_nord_bank.trackId, load_plugin_preset.trackID, load_plugin_preset.trackId, load_plugin_preset_file.trackID, load_plugin_preset_file.trackId, load_virus_preset.trackID, load_virus_preset.trackId, move_clip.trackId, move_track.trackID, move_track.trackId, move_track_into_folder.trackID, move_track_into_folder.trackId, move_track_out_of_folder.trackID, move_track_out_of_folder.trackId, param_verity.trackID, param_verity.trackId, param_verity_corpus.trackID, param_verity_corpus.trackId, place_patterns.trackIndex, psy_fm_clear_mod_matrix.trackID, psy_fm_clear_mod_matrix.trackId, psy_fm_get_analysis.trackID, psy_fm_get_analysis.trackId, psy_fm_load_preset.trackID, psy_fm_load_preset.trackId, psy_fm_mod_matrix_debug.trackID, psy_fm_mod_matrix_debug.trackId, psy_fm_set_mod_route.trackID, psy_fm_set_mod_route.trackId, rave_import_result.samplerTrackIndex, rave_import_result.trackIndex, rave_transform_clip.samplerTrackIndex, rave_transform_clip.trackIndex, remove_automation_lane.trackID, remove_automation_lane.trackId, remove_fx.trackID, remove_fx.trackId, remove_lfo.trackId, remove_midi_fx.trackID, remove_midi_fx.trackId, remove_send.trackID, remove_send.trackId, remove_track.trackID, remove_track.trackId, restart_fx.trackID, restart_fx.trackIndex, sampler_get_state.trackID, sampler_get_state.trackId, sampler_set_sample.trackID, sampler_set_sample.trackId, save_fx_chain.trackID, save_fx_chain.trackId, send_fx_midi.trackID, send_fx_midi.trackId, session_create_clip.trackIndex, set_automation_enabled.trackID, set_automation_enabled.trackId, set_automation_points.trackID, set_automation_points.trackId, set_cell.trackId, set_cells.trackId, set_fader_authoritative.trackID, set_fader_authoritative.trackId, set_fx_bypass.trackID, set_fx_bypass.trackId, set_fx_param.trackID, set_fx_param.trackId, set_internal_fx_param.trackID, set_internal_fx_param.trackId, set_layer_handoff.trackId, set_lfo_param.trackId, set_midi_fx_bypass.trackID, set_midi_fx_bypass.trackId, set_midi_fx_param.trackID, set_midi_fx_param.trackId, set_midi_fx_param_normalized.trackID, set_midi_fx_param_normalized.trackId, set_sampler_key_range.trackID, set_sampler_key_range.trackId, set_sampler_mode.trackID, set_sampler_mode.trackId, set_sampler_param.trackID, set_sampler_param.trackId, set_track.trackID, set_track.trackId, set_track.trackType, set_track_send_bypassed.trackID, set_track_send_bypassed.trackId, set_track_send_level.trackID, set_track_send_level.trackId, set_track_send_mode.trackID, set_track_send_mode.trackId, sub_synth_import_sysex.trackID, sub_synth_import_sysex.trackId, swap_fx_snapshot.trackIndex, toggle_plugin_editor.trackID, toggle_plugin_editor.trackId, tone_verity.trackId, trigger_sampler_slice.trackID, trigger_sampler_slice.trackId, verify_part.trackIndex
//   {"type", ""} -- 3: generate_chord.chordType, generate_progression.chordTypeOverride, key_check.scaleType
//   {"value", ""} -- 22: add_automation_point.value, add_cc_point.value, apply_movement_plan.endValue, apply_movement_plan.startValue, automation_preset.endValue, automation_preset.startValue, generate_automation_envelope.endValue, generate_automation_envelope.startValue, generate_clip_cc_lane.endValue, generate_clip_cc_lane.startValue, generate_clip_gain_envelope.endValue, generate_clip_gain_envelope.startValue, set_automation_points.value, set_bus_fx_param.value, set_cc_point.value, set_fx_param.value, set_internal_fx_param.value, set_lfo_param.value, set_master_fx_param.value, set_midi_fx_param.value, set_midi_fx_param_normalized.value, set_sampler_param.value
//   {"velocity", ""} -- 17: add_note.velocity, add_notes.velocity, generate_arrangement.velocityMax, generate_arrangement.velocityMin, generate_chopped_break.velocityMax, generate_chopped_break.velocityMin, generate_rhythm_pattern.dslVelocity, generate_rhythm_pattern.velocityA, generate_rhythm_pattern.velocityB, place_patterns.velocity, place_patterns.velocityScale, set_note.velocity, set_note_velocities.velocity, set_note_velocities.velocityMax, set_note_velocities.velocityMin, set_note_velocities.velocityOffset, trigger_sampler_slice.velocity
//   {"voicing", ""} -- 1: generate_chord.voicing
inline const QHash<QString, QString>& scalarTokens()
{
    static const QHash<QString, QString> t{
        {"gain", ""}, {"pan", ""}, {"chance", ""}, {"depth", ""}, {"velocity", ""},
        {"timbre", ""}, {"pressure", ""}, {"ratio", ""}, {"mix", ""}, {"amount", ""},
        {"level", ""}, {"count", ""}, {"index", ""}, {"id", ""}, {"ids", ""},
        {"size", ""}, {"seed", ""}, {"mode", ""}, {"type", ""}, {"root", ""},
        {"pitch", ""}, {"note", ""}, {"slot", ""}, {"track", ""}, {"clip", ""},
        {"lane", ""}, {"value", ""}, {"position", ""}, {"offset", ""}, {"program", ""},
        {"bank", ""}, {"preset", ""}, {"color", ""}, {"degree", ""}, {"degrees", ""},
        {"octave", ""}, {"inversion", ""}, {"voicing", ""}, {"frequency", ""},
        {"density", ""}, {"step", ""}, {"steps", ""}, {"cycle", ""}, {"cycles", ""},
        {"phase", ""}, {"smooth", ""}, {"grid", ""}, {"limit", ""}, {"epoch", ""},
        {"epochs", ""}, {"batch", ""}, {"baseline", ""}, {"run", ""}, {"runs", ""},
        {"temperature", ""}, {"complexity", ""}, {"sensitivity", ""}, {"num", ""},
        {"bin", ""}, {"bins", ""}, {"channel", ""}, {"number", ""}, {"min", ""},
        {"max", ""}, {"prominence", ""}, {"tol", ""}, {"axis", ""}, {"key", ""},
        {"bus", ""}, {"mutation", ""}, {"prob", ""}, {"probability", ""}, {"pulse", ""},
        {"curve", ""}, {"rotation", ""}, {"point", ""}, {"mid", ""}, {"pitches", ""},
        {"indexes", ""}, {"progression", ""},
    };
    return t;
}
// Split a camelCase / snake_case / digit-bearing field name into lowercase
// tokens: "modRateTolPct" -> [mod, rate, tol, pct], "f0Hz" -> [f, 0, hz].
inline QStringList tokenize(const QString& field)
{
    QStringList out;
    const int n = field.size();
    QString cur;
    auto flush = [&]() { if (!cur.isEmpty()) { out << cur.toLower(); cur.clear(); } };
    for (int i = 0; i < n; ++i)
    {
        const QChar ch = field.at(i);
        if (!ch.isLetterOrNumber()) { flush(); continue; }
        if (!cur.isEmpty())
        {
            const QChar prev = cur.at(cur.size() - 1);
            const bool boundary =
                (prev.isLower() && ch.isUpper()) ||
                (prev.isLetter() && ch.isDigit()) ||
                (prev.isDigit() && ch.isLetter());
            if (boundary) flush();
        }
        cur += ch;
    }
    flush();
    return out;
}

// Resolve a numeric field's unit: unit string, "" (classified scalar), or
// "?unclassified" (a new field the tables do not know — the coverage gate fails).
inline QString resolveFieldUnit(const QString& tool, const QString& field)
{
    const QString key = tool.toLower() + "." + field.toLower();
    if (overrideTable().contains(key)) return overrideTable().value(key);

    const QString lf = field.toLower();
    if (nameUnits().contains(lf)) return nameUnits().value(lf);

    const QStringList toks = tokenize(field);
    for (const QString& t : toks)
        if (tokenUnits().contains(t)) return tokenUnits().value(t);

    if (scalarNames().contains(lf)) return QString();
    for (const QString& t : toks)
        if (scalarTokens().contains(t)) return QString();

    return QStringLiteral("?unclassified");
}

// Human-readable sentence appended to a numeric property's description.
inline QString fieldDescriptionSuffix(const QString& unit)
{
    if (unit == QLatin1String("beats"))     return QStringLiteral(" In beats (musical time).");
    if (unit == QLatin1String("bars"))      return QStringLiteral(" In bars (4/4).");
    if (unit == QLatin1String("seconds"))   return QStringLiteral(" In seconds.");
    if (unit == QLatin1String("ms"))        return QStringLiteral(" In milliseconds.");
    if (unit == QLatin1String("bpm"))       return QStringLiteral(" In BPM.");
    if (unit == QLatin1String("Hz"))        return QStringLiteral(" In Hz.");
    if (unit == QLatin1String("dB"))        return QStringLiteral(" In dB.");
    if (unit == QLatin1String("percent"))   return QStringLiteral(" In percent.");
    if (unit == QLatin1String("samples"))   return QStringLiteral(" In samples.");
    if (unit == QLatin1String("semitones")) return QStringLiteral(" In semitones.");
    if (unit == QLatin1String("cents"))     return QStringLiteral(" In cents.");
    return QString();
}

// Annotate ONE numeric property object in place: append the unit sentence to an
// existing description (never replace) and set the machine-readable x-unit.
inline void applyUnit(QJsonObject& prop, const QString& unit)
{
    if (unit == QLatin1String("?unclassified"))
    {
        prop["x-unit"] = QStringLiteral("?unclassified");
        return;
    }
    if (unit.isEmpty())
    {
        prop["x-unit"] = QStringLiteral("scalar");
        return;
    }
    prop["description"] = prop.value("description").toString() + fieldDescriptionSuffix(unit);
    prop["x-unit"] = unit;
}

inline QJsonObject annotateProps(const QString& tool, const QJsonObject& propsIn)
{
    QJsonObject props = propsIn;
    for (auto it = props.begin(); it != props.end(); ++it)
    {
        QJsonObject prop = it.value().toObject();
        if (prop.isEmpty()) continue;
        const QString type = prop.value("type").toString();
        if (type == QLatin1String("number") || type == QLatin1String("integer"))
        {
            applyUnit(prop, resolveFieldUnit(tool, it.key()));
        }
        else if (type == QLatin1String("array"))
        {
            QJsonObject items = prop.value("items").toObject();
            if (!items.isEmpty())
            {
                const QString ityp = items.value("type").toString();
                if (ityp == QLatin1String("number") || ityp == QLatin1String("integer"))
                {
                    applyUnit(items, resolveFieldUnit(tool, it.key()));
                }
                else if (ityp == QLatin1String("object"))
                {
                    const QJsonObject sub = items.value("properties").toObject();
                    // Only recurse when the item declares its own properties; an
                    // object item WITHOUT a "properties" key is free-form and
                    // adding an empty one would make validateSchema reject every
                    // instance key (additionalProperties defaults to false).
                    if (!sub.isEmpty())
                        items["properties"] = annotateProps(tool, sub);
                }
                prop["items"] = items;
            }
        }
        else if (type == QLatin1String("object"))
        {
            const QJsonObject sub = prop.value("properties").toObject();
            if (!sub.isEmpty())
                prop["properties"] = annotateProps(tool, sub);
        }
        it.value() = prop;
    }
    return props;
}

// Deep-copy `schema`; annotate every numeric (number/integer) property and
// numeric array item. Non-numeric properties, type/minimum/maximum/enum/
// required are untouched.
inline QJsonObject enrichSchemaUnits(const QString& toolName, const QJsonObject& schema)
{
    QJsonObject out = schema;
    if (out.value("properties").isObject())
        out["properties"] = annotateProps(toolName, out.value("properties").toObject());
    return out;
}

// Visit every numeric (number/integer) field name reachable in a schema's
// properties (top level + nested object properties + numeric array items).
template <typename Fn>
inline void forEachNumericField(const QString& tool, const QJsonObject& props, Fn&& fn)
{
    for (auto it = props.begin(); it != props.end(); ++it)
    {
        const QJsonObject prop = it.value().toObject();
        if (prop.isEmpty()) continue;
        const QString type = prop.value("type").toString();
        if (type == QLatin1String("number") || type == QLatin1String("integer"))
        {
            fn(it.key());
        }
        else if (type == QLatin1String("array"))
        {
            const QJsonObject items = prop.value("items").toObject();
            const QString ityp = items.value("type").toString();
            if (ityp == QLatin1String("number") || ityp == QLatin1String("integer"))
                fn(it.key());
            else if (ityp == QLatin1String("object"))
                forEachNumericField(tool, items.value("properties").toObject(), fn);
        }
        else if (type == QLatin1String("object"))
        {
            forEachNumericField(tool, prop.value("properties").toObject(), fn);
        }
    }
}

// Coverage-gate helper: the "<tool>.<field>" names that resolve to
// "?unclassified" in this schema (empty when the tables cover everything).
inline QStringList unclassifiedNumericFields(const QString& tool, const QJsonObject& schema)
{
    QStringList out;
    const QJsonObject props = schema.value("properties").toObject();
    forEachNumericField(tool, props, [&](const QString& field) {
        if (resolveFieldUnit(tool, field) == QLatin1String("?unclassified"))
            out << (tool + "." + field);
    });
    return out;
}

// One example object per tool: exactly the `required` props (or all props when
// `required` is empty), values by declared type. Always an object.
inline QJsonObject buildToolExample(const QJsonObject& schema)
{
    const QJsonObject props = schema.value("properties").toObject();
    const QJsonArray required = schema.value("required").toArray();

    QStringList keys;
    if (!required.isEmpty())
    {
        for (const QJsonValue& v : required) keys << v.toString();
    }
    else
    {
        for (auto it = props.begin(); it != props.end(); ++it) keys << it.key();
    }

    QJsonObject ex;
    for (const QString& k : keys)
    {
        const QJsonObject p = props.value(k).toObject();
        const QString type = p.value("type").toString();
        if (type == QLatin1String("integer") || type == QLatin1String("number"))
            ex[k] = 0;
        else if (type == QLatin1String("string"))
        {
            const QJsonArray e = p.value("enum").toArray();
            ex[k] = e.isEmpty() ? QJsonValue(QString()) : e.first();
        }
        else if (type == QLatin1String("boolean"))
            ex[k] = false;
        else if (type == QLatin1String("array"))
            ex[k] = QJsonArray{};
        else
            ex[k] = QJsonObject{};
    }
    return ex;
}

} // namespace toolunits
} // namespace mcp
