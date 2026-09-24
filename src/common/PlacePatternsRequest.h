#pragma once
// place_patterns — the SINGLE argument parser/validator + payload builder behind
// the MCP `place_patterns` tool and (by the AGENTS.md parity rule) any future
// RPC twin.
//
// The parser and every error string were moved verbatim out of the MCP lambda
// (which previously validated inline — Gate 9: every note/placement field is
// range-checked). `clipId` is deliberately NOT part of the parsed struct: the
// MCP lambda takes it separately and passes it straight to
// AudioEngineCommands::placePatterns(). Engine-side notes:
//   * PatternPlacer::PatternNote / Placement and the kMaxOctaveShift /
//     kMinVelocityScale / kMaxVelocityScale bounds are reused, not re-declared.
//   * AudioEngineCommands::PlaceResult is the shared engine result type.
//
// Exact error strings reproduced here:
//   "pattern note pitch must be in 0..127"
//   "pattern note velocity must be in 1..127"
//   "pattern note durationBeats must be > 0"
//   "pattern note startBeat must be >= 0"
//   "patterns must be non-empty"
//   "placement start must be >= 0"
//   "placement octave must be in -6..6"
//   "placement velocityScale must be in 0.05..2.0"
//   "placements must be non-empty"

#include <QJsonArray>
#include <QJsonObject>

#include "../engine/AudioEngineCommands.h"   // AudioEngineCommands::PlaceResult (+ PatternPlacer.h)
#include "../engine/PatternPlacer.h"

#include <string>
#include <utility>
#include <vector>

namespace HDAW {

struct PlacePatternsRequest {
    bool ok = false;
    std::string error;
    std::vector<std::vector<PatternPlacer::PatternNote>> patterns;
    std::vector<PatternPlacer::Placement> placements;
    bool clearExisting = false;
};

// Parse + validate the MCP `place_patterns` arguments. On failure `ok` is false
// and `error` holds the exact surface string.
inline PlacePatternsRequest parsePlacePatternsRequest(const QJsonObject& args)
{
    PlacePatternsRequest req;

    // Parse + validate the pattern payload (the analyze_midi_file patterns[]
    // shape; Gate 9 — every note field is range-checked).
    const auto patternsArr = args.value("patterns").toArray();
    for (const auto& pv : patternsArr)
    {
        std::vector<PatternPlacer::PatternNote> notes;
        for (const auto& nv : pv.toObject().value("notes").toArray())
        {
            auto no = nv.toObject();
            const int pitch = no.value("pitch").toInt();
            const double start = no.value("startBeat").toDouble();
            const double dur = no.value("durationBeats").toDouble();
            const int vel = no.value("velocity").toInt();
            if (pitch < 0 || pitch > 127) {
                req.error = "pattern note pitch must be in 0..127";
                return req;
            }
            if (vel < 1 || vel > 127) {
                req.error = "pattern note velocity must be in 1..127";
                return req;
            }
            if (!(dur > 0.0)) {
                req.error = "pattern note durationBeats must be > 0";
                return req;
            }
            if (start < 0.0) {
                req.error = "pattern note startBeat must be >= 0";
                return req;
            }
            notes.push_back(PatternPlacer::PatternNote{pitch, start, dur, vel});
        }
        req.patterns.push_back(std::move(notes));
    }
    if (req.patterns.empty()) {
        req.error = "patterns must be non-empty";
        return req;
    }

    // Parse + validate placements (octave/velocityScale ranges; Gate 9).
    for (const auto& plv : args.value("placements").toArray())
    {
        auto pl = plv.toObject();
        const double start = pl.value("start").toDouble();
        if (start < 0.0) {
            req.error = "placement start must be >= 0";
            return req;
        }
        PatternPlacer::Placement p;
        p.start = start;
        p.octaveShift = pl.contains("octave") ? pl.value("octave").toInt() : 0;
        if (p.octaveShift < -PatternPlacer::kMaxOctaveShift || p.octaveShift > PatternPlacer::kMaxOctaveShift) {
            req.error = "placement octave must be in -6..6";
            return req;
        }
        p.velocityScale = pl.contains("velocityScale") ? pl.value("velocityScale").toDouble() : 1.0;
        if (p.velocityScale < PatternPlacer::kMinVelocityScale || p.velocityScale > PatternPlacer::kMaxVelocityScale) {
            req.error = "placement velocityScale must be in 0.05..2.0";
            return req;
        }
        p.reverse = pl.contains("reverse") ? pl.value("reverse").toBool() : false;
        req.placements.push_back(p);
    }
    if (req.placements.empty()) {
        req.error = "placements must be non-empty";
        return req;
    }

    req.clearExisting = args.contains("clear") ? args.value("clear").toBool() : false;
    req.ok = true;
    return req;
}

// {added, skipped, clipId, placementsApplied} — the compact success payload
// every surface emits (`placementsApplied` is the parsed placement count).
inline QJsonObject placePatternsJson(const AudioEngineCommands::PlaceResult& out,
                                     int placementsApplied)
{
    return QJsonObject{
        {"added",              out.added},
        {"skipped",            out.skipped},
        {"clipId",             out.clipId},
        {"placementsApplied",  placementsApplied}};
}

} // namespace HDAW
