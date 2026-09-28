#pragma once
// Beat-window archaeology — ONE implementation for the query_notes / query_clips
// MCP tools and their read.queryNotes / read.queryClips RPC twins (slice S2 of
// docs/plans/2026-09-28-agent-mechanization.md, per the AGENTS.md parity
// contract: one src/common shaper, so the two surfaces cannot drift).
//
// WHY: "which notes SOUND at beat 657.5, on which track, with which IDs" used to
// be four regex passes over the .hdaw XML. The agent asks in PROJECT (absolute)
// beats and gets edit-ready IDs back.
//
// SEMANTICS — INTERVAL OVERLAP in the project beat space, never "onset in
// range": a note/clip participates when `absStartBeat < endBeat &&
// absEndBeat > startBeat`. A note that starts before the window but sustains
// into it is returned; a clip that straddles the window boundary is returned.
//
// TWO UNIT SPACES, deliberately: a note's IDs::startBeat/durationBeats are
// CLIP-LOCAL beats (McpTools_Note.cpp list_notes CONTRACT) while a clip's
// IDs::startTime/duration are SECONDS. Both are converted to project beats here
// (querySecondsToBeats mirrors HDAW::secondsToBeats), and every row echoes
// `localBeat` next to its absolute `absBeat` so the split is visible rather than
// implicit.
//
// NOT EXPANDED (stated so the limit is visible): note-level repeat operators
// (repeatCount / repeatRate / occurrence / recurrence) are raw properties, not
// occurrences — MidiClipProcessor.h:253+ folds them into chance/level at
// render time, and this reader reports the note once.
//
// LOOPING (evidence, MidiClipProcessor.h): a MIDI clip does NOT replay its
// content. processBlock returns silence whenever the transport sits outside
// [startSec, startSec + durSec) (MidiClipProcessor.h:152), the `loopCount` it
// derives (:170) only seeds the chance/occurrence operators (:212-214), and the
// MIDI branch of RoutingManager never calls setLooping (RoutingManager.cpp:
// 723-732; the AUDIO branch does, :671). MainAudioProcessor.cpp:746-752 lets a
// MIDI clip bound the project end with no isLooping exemption (audio is exempt
// at :741). So IDs::looping is inert for MIDI and every note sounds once,
// clipped to [clipStartB, clipEndB): occurrenceIndex is always 0.
#include "../model/ProjectModel.h"

#include <QJsonObject>
#include <QString>

namespace HDAW {

// Seconds → beats, mirroring HDAW::secondsToBeats in
// src/engine/AudioEngineCommands_Helpers.h (inlined because src/common must not
// include an engine header). bpm <= 0 leaves the value untouched, as the engine
// helper does.
inline double querySecondsToBeats(double sec, double bpm)
{
    return bpm > 0.0 ? sec * bpm / 60.0 : sec;
}

// The ONE invalid-window refusal text. Both builders below return it (via
// `error`) and both surfaces emit it verbatim, so the twin test compares bytes.
inline constexpr const char* kInvalidBeatWindowError =
    "beat window invalid: endBeat must be greater than startBeat";

// Read the beat-window arguments off a params object. ONE place, so a missing or
// non-numeric startBeat/endBeat answers with the SAME text on the MCP tool and
// the RPC route (the tool schema deliberately does NOT declare them `required`:
// the MCP validator would pre-empt the handler with "invalid params: …" and the
// two failure messages would differ). "" when absent/false: see kInvalid...
inline bool readBeatWindowArgs(const QJsonObject& o, double& startBeat, double& endBeat, QString& error)
{
    if (!o.contains("startBeat") || !o.value("startBeat").isDouble())
    {
        error = QStringLiteral("missing or non-numeric param: startBeat");
        return false;
    }
    if (!o.contains("endBeat") || !o.value("endBeat").isDouble())
    {
        error = QStringLiteral("missing or non-numeric param: endBeat");
        return false;
    }
    startBeat = o.value("startBeat").toDouble();
    endBeat = o.value("endBeat").toDouble();
    return true;
}

// Notes sounding during [startBeat, endBeat) in PROJECT beats. `trackIndex` is a
// TRACK_LIST position, or -1 for every track. Payload:
//   {"count":N, "unit":"beats", "rows":[{noteId, clipId, trackIndex, trackID,
//     clipName, absBeat, endBeat, localBeat, durationBeats, pitch, velocity,
//     occurrenceIndex, truncated}]}
// On an inverted/empty window returns {} and sets *ok=false/*error (the shared
// refusal text). *ok is set true on success when the out-param is given.
QJsonObject buildNoteQueryPayload(const juce::ValueTree& trackList, double bpm,
                                  double startBeat, double endBeat, int trackIndex,
                                  bool* ok = nullptr, QString* error = nullptr);

// Clips whose span intersects [startBeat, endBeat) in PROJECT beats. A looping
// clip is reported ONCE, by its own [clipStartB, clipEndB) interval — only notes
// would carry occurrences, and MIDI notes do not loop (see the header note).
// Payload rows: {clipId, trackIndex, trackID, name, type, startBeat, endBeat,
// durationBeats, muted, gain}.
QJsonObject buildClipQueryPayload(const juce::ValueTree& trackList, double bpm,
                                  double startBeat, double endBeat,
                                  bool* ok = nullptr, QString* error = nullptr);

} // namespace HDAW
