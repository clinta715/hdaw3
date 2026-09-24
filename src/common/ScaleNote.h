#pragma once
// scale_note — the SINGLE resolver + payload builder behind the MCP `scale_note`
// tool and (by the AGENTS.md parity rule) any future RPC twin.
//
// "Where both surfaces shape the same artifact, put the logic in src/common/ —
// identical payload by construction, not by discipline." The shaping used to
// live inline in the MCP lambda; it is moved here verbatim so a twin route
// cannot drift, including the three exact error strings:
//   "rootMidi must be in 0..127"
//   "unknown scale: " + <name>
//   "degree out of range: computed pitch falls outside 0..127"
//
// The scale-NAME parser is NOT re-implemented: HDAW::resolveScaleModeIndex()
// (src/common/KeyConflict.h) is the one resolver — it accepts the canonical
// name ("Minor (Aeolian)"), the short form ("minor"), and the parenthetical
// mode alias ("aeolian"), case-insensitively.

#include <QJsonObject>
#include <QString>

#include "../engine/PhraseGenerator.h"
#include "KeyConflict.h"   // HDAW::resolveScaleModeIndex

#include <string>

namespace HDAW {

struct ScaleNoteResult {
    bool ok = false;
    int midiPitch = 0;
    std::string error;
};

// Map a scale degree (diatonic step, octave-wrapped) to an absolute MIDI pitch
// for `rootMidi` (0..127) and `scaleName`. `octave` shifts the result by
// +-12 * octave. Errors (not ok) reproduce the MCP tool's exact strings.
inline ScaleNoteResult resolveScaleNote(int rootMidi, const QString& scaleName,
                                        int degree, int octave)
{
    ScaleNoteResult r;
    if (rootMidi < 0 || rootMidi > 127) {
        r.error = "rootMidi must be in 0..127";
        return r;
    }

    const int scaleIdx = resolveScaleModeIndex(scaleName.toStdString());
    if (scaleIdx < 0) {
        r.error = "unknown scale: " + scaleName.toStdString();
        return r;
    }

    const int midiPitch = PhraseGenerator::scaleDegreeToPitch(rootMidi, scaleIdx, degree, octave);
    if (midiPitch < 0) {
        r.error = "degree out of range: computed pitch falls outside 0..127";
        return r;
    }

    r.midiPitch = midiPitch;
    r.ok = true;
    return r;
}

// {"midiPitch", r.midiPitch} — the compact success payload every surface emits.
inline QJsonObject scaleNoteJson(const ScaleNoteResult& r)
{
    return QJsonObject{{"midiPitch", r.midiPitch}};
}

} // namespace HDAW
