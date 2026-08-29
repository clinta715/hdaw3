#include "McpTools.h"
#include "McpTools_Private.h"
#include "McpServer.h"
#include "McpToolDef.h"
#include "../model/ProjectModel.h"
#include "../engine/AudioEngine.h"
#include "../engine/AudioEngineCommands_Helpers.h"
#include "../engine/MainAudioProcessor.h"
#include "../engine/PluginManager.h"
#include "../engine/Track.h"
#include "../engine/PhraseGenerator.h"
#include "../engine/ArrangementGenerator.h"
#include "engine/RhythmPatternGenerator.h"
#include "../engine/PatternLibrary.h"
#include "../engine/MidiAnalyzer.h"
#include "../engine/ProjectSerializer.h"
#include "../engine/ProjectBackup.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QSet>
#include <algorithm>
#include <set>
#include <tuple>

namespace mcp {

void registerNoteTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"add_note", "Add a MIDI note to a clip; returns noteId.",
        objSchema({{"clipId",    QJsonObject{{"type","integer"}}},
                  {"pitch",     QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
                  {"start",     QJsonObject{{"type","number"}}},
                  {"duration",  QJsonObject{{"type","number"}}},
                  {"velocity",  QJsonObject{{"type","integer"},{"minimum",1},{"maximum",127}}}},
                 {"clipId","pitch","start","duration","velocity"}),
        "note",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = -1; auto c = findClip(e, a.value("clipId").toInt(), &ti);
            if (!c.isValid()) return McpToolResult::text("clip not found", true);
            if (c.getProperty(IDs::clipType).toString() != juce::String("midi"))
                return McpToolResult::text("clip is not MIDI", true);
            auto& m = e->getProjectModel(); auto& um = m.getUndoManager();
            auto nl = c.getChildWithName(IDs::MIDI_NOTE_LIST);
            if (!nl.isValid()) { nl = juce::ValueTree(IDs::MIDI_NOTE_LIST); c.addChild(nl, -1, nullptr); }
            juce::ValueTree n(IDs::MIDI_NOTE);
            int nid = m.allocateNoteID();
            n.setProperty(IDs::noteID, nid, nullptr);
            n.setProperty(IDs::noteNumber, a.value("pitch").toInt(), &um);
            n.setProperty(IDs::startBeat, a.value("start").toDouble(), &um);
            n.setProperty(IDs::durationBeats, a.value("duration").toDouble(), &um);
            n.setProperty(IDs::velocity, static_cast<float>(a.value("velocity").toInt()) / 127.0f, &um);
            nl.addChild(n, -1, &um);
            return McpToolResult::text(QString("noteId=%1").arg(nid));
        }});

    {
        QJsonObject noteItemProps{
            {"pitch",     QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
            {"start",     QJsonObject{{"type","number"}}},
            {"duration",  QJsonObject{{"type","number"}}},
            {"velocity",  QJsonObject{{"type","integer"},{"minimum",1},{"maximum",127}}}
        };
        QJsonObject noteItem{{"type","object"},{"properties", noteItemProps},{"required", QJsonArray{"pitch","start","duration"}}};
        QJsonObject addNotesSchema = objSchema(
            {{"clipId",   QJsonObject{{"type","integer"}}},
             {"notes",    QJsonObject{{"type","array"},{"items", noteItem}}},
             {"relative", QJsonObject{{"type","boolean"}}}},
            QJsonArray{"clipId","notes"});
        s.registerTool({"add_notes", "Add multiple MIDI notes to a clip in one batch; returns {added, noteIds}. Default (relative=true): start/duration are CLIP-LOCAL beats (subtract the clip's own start beat from timeline positions). ABSOLUTE mode (relative=false): each note's start is a TIMELINE-ABSOLUTE beat position, converted to clip-local by subtracting the clip's start beat (clip start seconds * bpm / 60); the whole batch is rejected if any absolute start is before the clip's start. A clip plays at most 8192 notes (excess is logged and skipped - split long parts into multiple clips). Exact duplicates WITHIN the batch (same pitch, start, and duration) are skipped after the first occurrence and counted in duplicatesSkipped (velocity is not a dedupe key).",
            addNotesSchema,
            "note",
            [e](const QJsonObject& a) -> McpToolResult {
                int ti = -1; auto c = findClip(e, a.value("clipId").toInt(), &ti);
                if (!c.isValid()) return McpToolResult::text("clip not found", true);
                if (c.getProperty(IDs::clipType).toString() != juce::String("midi"))
                    return McpToolResult::text("clip is not MIDI", true);
                auto& m = e->getProjectModel(); auto& um = m.getUndoManager();
                auto nl = c.getChildWithName(IDs::MIDI_NOTE_LIST);
                if (!nl.isValid()) { nl = juce::ValueTree(IDs::MIDI_NOTE_LIST); c.addChild(nl, -1, nullptr); }
                auto notesArr = a.value("notes").toArray();
                if (notesArr.isEmpty()) return McpToolResult::text("notes array is empty", true);

                // ABSOLUTE mode (relative=false): note starts are timeline-absolute
                // beats. Convert to clip-local by subtracting the clip's own start,
                // which the ValueTree stores in SECONDS (HDAW boundary convention:
                // MCP speaks beats, processors/tree speak seconds). Validate the
                // WHOLE batch before mutating anything so a single out-of-range
                // absolute start rejects the entire call (no partial add).
                const bool relative = a.value("relative").toBool(true);
                double clipStartBeats = 0.0;
                if (!relative) {
                    const double bpm = e->getProjectModel().getTree().getProperty(IDs::tempo, 120.0);
                    clipStartBeats = HDAW::secondsToBeats(static_cast<double>(c.getProperty(IDs::startTime, 0.0)), bpm);
                    for (const auto& nv : notesArr) {
                        const double absStart = nv.toObject().value("start").toDouble();
                        if (absStart < clipStartBeats)
                            return McpToolResult::text(
                                QString("absolute start %1 < clip start %2 (clipId %3)")
                                    .arg(absStart).arg(clipStartBeats).arg(a.value("clipId").toInt()), true);
                    }
                }

                um.beginNewTransaction();
                QJsonArray ids;
                int duplicatesSkipped = 0;
                // Batch-internal exact-dup guard (P3-1): same (pitch, startBeat,
                // durationBeats) triple keeps only the FIRST occurrence. Keyed on
                // the EFFECTIVE clip-local startBeat (post absolute conversion),
                // matching what is actually written to the tree. Velocity is not
                // a dedupe key. Duplicates against EXISTING clip notes are NOT
                // deduped — this guard is batch-internal only.
                std::set<std::tuple<int, double, double>> seen;
                for (const auto& nv : notesArr) {
                    auto no = nv.toObject();
                    const int pitch = no.value("pitch").toInt();
                    const double startBeat = relative ? no.value("start").toDouble()
                                                      : no.value("start").toDouble() - clipStartBeats;
                    const double durationBeats = no.value("duration").toDouble();
                    if (!seen.insert(std::make_tuple(pitch, startBeat, durationBeats)).second) {
                        ++duplicatesSkipped;
                        continue;
                    }
                    juce::ValueTree n(IDs::MIDI_NOTE);
                    int nid = m.allocateNoteID();
                    n.setProperty(IDs::noteID, nid, nullptr);
                    n.setProperty(IDs::noteNumber, pitch, &um);
                    n.setProperty(IDs::startBeat, startBeat, &um);
                    n.setProperty(IDs::durationBeats, durationBeats, &um);
                    n.setProperty(IDs::velocity, static_cast<float>(no.value("velocity").toInt(100)) / 127.0f, &um);
                    nl.addChild(n, -1, &um);
                    ids.append(nid);
                }
                QJsonObject result;
                result["added"] = ids.size();
                result["duplicatesSkipped"] = duplicatesSkipped;
                result["noteIds"] = ids;
                return McpToolResult::text(QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)));
            }});
    }

    s.registerTool({"set_note", "Update a note's properties (partial).",
        objSchema({{"noteId",   QJsonObject{{"type","integer"}}},
                  {"pitch",    QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}},
                  {"start",    QJsonObject{{"type","number"}}},
                  {"duration", QJsonObject{{"type","number"}}},
                  {"velocity", QJsonObject{{"type","integer"},{"minimum",1},{"maximum",127}}}}, {"noteId"}),
        "note",
        [e](const QJsonObject& a) -> McpToolResult {
            int dummy = 0; auto n = findNote(e, a.value("noteId").toInt(), &dummy);
            if (!n.isValid()) return McpToolResult::text("note not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            if (a.contains("pitch"))    n.setProperty(IDs::noteNumber, a.value("pitch").toInt(), &um);
            if (a.contains("start"))    n.setProperty(IDs::startBeat, a.value("start").toDouble(), &um);
            if (a.contains("duration")) n.setProperty(IDs::durationBeats, a.value("duration").toDouble(), &um);
            if (a.contains("velocity")) n.setProperty(IDs::velocity, static_cast<float>(a.value("velocity").toInt()) / 127.0f, &um);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_note_velocities", "Set velocities on notes in a clip (bulk). Supports absolute, relative, and random modes.",
        objSchema({{"clipId",         QJsonObject{{"type","integer"}}},
                  {"noteIds",        QJsonObject{{"type","array"},
                      {"items", QJsonObject{{"type","integer"}}}}},
                  {"pitches",        QJsonObject{{"type","array"},
                      {"items", QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}}}},
                  {"startGte",       QJsonObject{{"type","number"}}},
                  {"startLt",        QJsonObject{{"type","number"}}},
                  {"velocity",       QJsonObject{{"type","integer"},{"minimum",1},{"maximum",127}}},
                  {"velocityOffset", QJsonObject{{"type","integer"}}},
                  {"velocityMin",    QJsonObject{{"type","integer"},{"minimum",1},{"maximum",127}}},
                  {"velocityMax",    QJsonObject{{"type","integer"},{"minimum",1},{"maximum",127}}}},
                 {"clipId"}),
        "note",
        [e](const QJsonObject& a) -> McpToolResult {
            auto c = findClip(e, a.value("clipId").toInt(), nullptr);
            if (!c.isValid()) return McpToolResult::text("clip not found", true);
            auto nl = c.getChildWithName(IDs::MIDI_NOTE_LIST);
            if (!nl.isValid()) return McpToolResult::text("ok");
            bool hasAbs = a.contains("velocity");
            bool hasRel = a.contains("velocityOffset");
            bool hasRng = a.contains("velocityMin") && a.contains("velocityMax");
            if (!hasAbs && !hasRel && !hasRng)
                return McpToolResult::text("provide velocity, velocityOffset, or velocityMin+velocityMax", true);
            QSet<int> pitches; for (const auto& p : a.value("pitches").toArray()) pitches.insert(p.toInt());
            QSet<int> ids;     for (const auto& i : a.value("noteIds").toArray()) ids.insert(i.toInt());
            bool hasGte = a.contains("startGte"); bool hasLt = a.contains("startLt");
            double gte = a.value("startGte").toDouble(); double lt = a.value("startLt").toDouble();
            int vAbs = a.value("velocity").toInt();
            int vOff = a.value("velocityOffset").toInt();
            int vMin = a.value("velocityMin").toInt();
            int vMax = a.value("velocityMax").toInt();
            auto& um = e->getProjectModel().getUndoManager();
            um.beginNewTransaction();
            int modified = 0;
            for (int k = 0; k < nl.getNumChildren(); ++k) {
                auto n = nl.getChild(k);
                int nid = static_cast<int>(n.getProperty(IDs::noteID));
                int p   = static_cast<int>(n.getProperty(IDs::noteNumber));
                double s= static_cast<double>(n.getProperty(IDs::startBeat));
                bool match = ids.isEmpty() && pitches.isEmpty();
                if (!ids.isEmpty() && ids.contains(nid)) match = true;
                if (!pitches.isEmpty() && pitches.contains(p)) match = true;
                if (hasGte && s < gte) match = false;
                if (hasLt  && s >= lt) match = false;
                if (!match) continue;
                int newVel = 0;
                if (hasAbs) {
                    newVel = vAbs;
                } else if (hasRel) {
                    newVel = static_cast<int>(static_cast<double>(n.getProperty(IDs::velocity)) * 127.0 + 0.5) + vOff;
                } else {
                    newVel = vMin + (std::rand() % (vMax - vMin + 1));
                }
                newVel = (std::max)(1, (std::min)(127, newVel));
                n.setProperty(IDs::velocity, static_cast<float>(newVel) / 127.0f, &um);
                ++modified;
            }
            return McpToolResult::text(QString("modified %1 notes").arg(modified));
        }});

    s.registerTool({"remove_notes", "Remove notes by filter or by noteIds (destructive).",
        objSchema({{"clipId",   QJsonObject{{"type","integer"}}},
                  {"pitches",  QJsonObject{{"type","array"},
                      {"items", QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}}}},
                  {"startGte", QJsonObject{{"type","number"}}},
                  {"startLt",  QJsonObject{{"type","number"}}},
                  {"noteIds",  QJsonObject{{"type","array"},
                      {"items", QJsonObject{{"type","integer"}}}}},
                  {"dryRun",   QJsonObject{{"type","boolean"}}}}, {"clipId"}),
        "note",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = -1; auto c = findClip(e, a.value("clipId").toInt(), &ti);
            if (!c.isValid()) return McpToolResult::text("clip not found", true);
            auto nl = c.getChildWithName(IDs::MIDI_NOTE_LIST);
            if (!nl.isValid()) return McpToolResult::text("ok");
            QSet<int> pitches; for (const auto& p : a.value("pitches").toArray()) pitches.insert(p.toInt());
            QSet<int> ids;     for (const auto& i : a.value("noteIds").toArray()) ids.insert(i.toInt());
            bool hasGte = a.contains("startGte"); bool hasLt = a.contains("startLt");
            double gte = a.value("startGte").toDouble(); double lt = a.value("startLt").toDouble();
            int matched = 0;
            for (int k = nl.getNumChildren() - 1; k >= 0; --k) {
                auto n = nl.getChild(k);
                int nid = static_cast<int>(n.getProperty(IDs::noteID));
                int p   = static_cast<int>(n.getProperty(IDs::noteNumber));
                double s= static_cast<double>(n.getProperty(IDs::startBeat));
                bool match = ids.isEmpty() && pitches.isEmpty();
                if (!ids.isEmpty() && ids.contains(nid)) match = true;
                if (!pitches.isEmpty() && pitches.contains(p)) match = true;
                if (hasGte && s < gte) match = false;
                if (hasLt  && s >= lt) match = false;
                if (match) {
                    ++matched;
                    if (!a.value("dryRun").toBool(false))
                        nl.removeChild(k, &e->getProjectModel().getUndoManager());
                }
            }
            if (a.value("dryRun").toBool(false))
                return McpToolResult::text(QString("would remove %1 notes").arg(matched));
            return McpToolResult::text(QString("removed %1 notes").arg(matched));
        }});

    s.registerTool({"clear_notes", "Remove all notes from a MIDI clip (destructive).",
        objSchema({{"clipId", QJsonObject{{"type","integer"}}},
                  {"dryRun", QJsonObject{{"type","boolean"}}}}, {"clipId"}),
        "note",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = -1; auto c = findClip(e, a.value("clipId").toInt(), &ti);
            if (!c.isValid()) return McpToolResult::text("clip not found", true);
            auto nl = c.getChildWithName(IDs::MIDI_NOTE_LIST);
            int n = nl.isValid() ? nl.getNumChildren() : 0;
            if (a.value("dryRun").toBool(false))
                return McpToolResult::text(QString("would clear %1 notes").arg(n));
            if (nl.isValid()) c.removeChild(nl, &e->getProjectModel().getUndoManager());
            return McpToolResult::text(QString("cleared %1 notes").arg(n));
        }});

    s.registerTool({"list_notes", "List MIDI notes in a clip with optional range filters. Filters mirror remove_notes: pitches[], startGte/startLt (clip-local beats), noteIds[]. Returns {count, notes:[{noteId,pitch,start,duration,velocity,chance,repeatCount,repeatRate,repeatCurve,occurrence,recurrence,gain,pan,pitchOffset,timbre,pressure}]} in clip-local beats.",
        objSchema({{"clipId",   QJsonObject{{"type","integer"}}},
                  {"pitches",  QJsonObject{{"type","array"},
                      {"items", QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}}}},
                  {"startGte", QJsonObject{{"type","number"}}},
                  {"startLt",  QJsonObject{{"type","number"}}},
                  {"noteIds",  QJsonObject{{"type","array"},
                      {"items", QJsonObject{{"type","integer"}}}}}}, {"clipId"}),
        "note",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = -1; auto c = findClip(e, a.value("clipId").toInt(), &ti);
            if (!c.isValid()) return McpToolResult::text("clip not found", true);
            if (c.getProperty(IDs::clipType).toString() != juce::String("midi"))
                return McpToolResult::text("clip is not MIDI", true);
            auto nl = c.getChildWithName(IDs::MIDI_NOTE_LIST);
            if (!nl.isValid())
                return McpToolResult::text(QString::fromUtf8(
                    QJsonDocument(QJsonObject{{"count", 0}, {"notes", QJsonArray()}})
                        .toJson(QJsonDocument::Compact)));
            QSet<int> pitches; for (const auto& p : a.value("pitches").toArray()) pitches.insert(p.toInt());
            QSet<int> ids;     for (const auto& i : a.value("noteIds").toArray()) ids.insert(i.toInt());
            const bool filterIds = !ids.isEmpty();
            const bool filterPitches = !pitches.isEmpty();
            const bool hasGte = a.contains("startGte");
            const bool hasLt  = a.contains("startLt");
            const double gte = a.value("startGte").toDouble();
            const double lt  = a.value("startLt").toDouble();

            QJsonArray notes;
            for (int k = 0; k < nl.getNumChildren(); ++k)
            {
                auto n = nl.getChild(k);
                const int nid = static_cast<int>(n.getProperty(IDs::noteID));
                const int p   = static_cast<int>(n.getProperty(IDs::noteNumber));
                const double s= static_cast<double>(n.getProperty(IDs::startBeat));
                if (filterIds && !ids.contains(nid)) continue;
                if (filterPitches && !pitches.contains(p)) continue;
                if (hasGte && s < gte) continue;
                if (hasLt  && s >= lt) continue;
                notes.append(QJsonObject{
                    {"noteId", nid},
                    {"pitch", p},
                    {"start", s},
                    {"duration", static_cast<double>(n.getProperty(IDs::durationBeats))},
                    {"velocity", static_cast<int>(static_cast<double>(n.getProperty(IDs::velocity)) * 127.0 + 0.5)},
                    {"chance", static_cast<double>(n.getProperty(IDs::chance, 1.0f))},
                    {"repeatCount", static_cast<int>(n.getProperty(IDs::repeatCount, 0))},
                    {"repeatRate", static_cast<double>(n.getProperty(IDs::repeatRate, 0.25f))},
                    {"repeatCurve", static_cast<double>(n.getProperty(IDs::repeatCurve, 0.0f))},
                    {"occurrence", static_cast<int>(n.getProperty(IDs::occurrence, 0))},
                    {"recurrence", static_cast<int>(n.getProperty(IDs::recurrence, 0))},
                    {"gain", static_cast<double>(n.getProperty(IDs::noteGain, 1.0f))},
                    {"pan", static_cast<double>(n.getProperty(IDs::notePan, 0.0f))},
                    {"pitchOffset", static_cast<double>(n.getProperty(IDs::notePitch, 0.0f))},
                    {"timbre", static_cast<double>(n.getProperty(IDs::noteTimbre, 0.5f))},
                    {"pressure", static_cast<double>(n.getProperty(IDs::notePressure, 0.0f))}
                });
            }
            QJsonObject result;
            result["count"] = notes.size();
            result["notes"] = notes;
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(result).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"set_note_chance", "Set a note's chance (probability) operator.",
        objSchema({{"noteId", QJsonObject{{"type","integer"}}},
                  {"chance", QJsonObject{{"type","number"},{"minimum",0.0},{"maximum",1.0}}}}, {"noteId","chance"}),
        "note",
        [e](const QJsonObject& a) -> McpToolResult {
            int dummy = 0; auto n = findNote(e, a.value("noteId").toInt(), &dummy);
            if (!n.isValid()) return McpToolResult::text("note not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            n.setProperty(IDs::chance, a.value("chance").toDouble(), &um);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_note_repeat_count", "Set a note's repeat count (repeats/ratchets) operator.",
        objSchema({{"noteId", QJsonObject{{"type","integer"}}},
                  {"repeatCount", QJsonObject{{"type","integer"},{"minimum",0}}}}, {"noteId","repeatCount"}),
        "note",
        [e](const QJsonObject& a) -> McpToolResult {
            int dummy = 0; auto n = findNote(e, a.value("noteId").toInt(), &dummy);
            if (!n.isValid()) return McpToolResult::text("note not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            n.setProperty(IDs::repeatCount, a.value("repeatCount").toInt(), &um);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_note_repeat_rate", "Set a note's repeat rate (beat fraction) operator.",
        objSchema({{"noteId", QJsonObject{{"type","integer"}}},
                  {"repeatRate", QJsonObject{{"type","number"},{"minimum",0.0}}}}, {"noteId","repeatRate"}),
        "note",
        [e](const QJsonObject& a) -> McpToolResult {
            int dummy = 0; auto n = findNote(e, a.value("noteId").toInt(), &dummy);
            if (!n.isValid()) return McpToolResult::text("note not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            n.setProperty(IDs::repeatRate, a.value("repeatRate").toDouble(), &um);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_note_repeat_curve", "Set a note's repeat curve (bunching, -1.0 to 1.0) operator.",
        objSchema({{"noteId", QJsonObject{{"type","integer"}}},
                  {"repeatCurve", QJsonObject{{"type","number"},{"minimum",-1.0},{"maximum",1.0}}}}, {"noteId","repeatCurve"}),
        "note",
        [e](const QJsonObject& a) -> McpToolResult {
            int dummy = 0; auto n = findNote(e, a.value("noteId").toInt(), &dummy);
            if (!n.isValid()) return McpToolResult::text("note not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            n.setProperty(IDs::repeatCurve, a.value("repeatCurve").toDouble(), &um);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_note_occurrence", "Set a note's occurrence (cycle-aware bitmask) operator.",
        objSchema({{"noteId", QJsonObject{{"type","integer"}}},
                  {"occurrence", QJsonObject{{"type","integer"},{"minimum",0}}}}, {"noteId","occurrence"}),
        "note",
        [e](const QJsonObject& a) -> McpToolResult {
            int dummy = 0; auto n = findNote(e, a.value("noteId").toInt(), &dummy);
            if (!n.isValid()) return McpToolResult::text("note not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            n.setProperty(IDs::occurrence, a.value("occurrence").toInt(), &um);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_note_recurrence", "Set a note's recurrence (previous-event dependency) operator.",
        objSchema({{"noteId", QJsonObject{{"type","integer"}}},
                  {"recurrence", QJsonObject{{"type","integer"},{"minimum",0},{"maximum",2}}}}, {"noteId","recurrence"}),
        "note",
        [e](const QJsonObject& a) -> McpToolResult {
            int dummy = 0; auto n = findNote(e, a.value("noteId").toInt(), &dummy);
            if (!n.isValid()) return McpToolResult::text("note not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            n.setProperty(IDs::recurrence, a.value("recurrence").toInt(), &um);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_note_gain", "Set a note's per-note gain multiplier (0.0 to 2.0).",
        objSchema({{"noteId", QJsonObject{{"type","integer"}}},
                  {"gain", QJsonObject{{"type","number"},{"minimum",0.0},{"maximum",2.0}}}}, {"noteId","gain"}),
        "note",
        [e](const QJsonObject& a) -> McpToolResult {
            int dummy = 0; auto n = findNote(e, a.value("noteId").toInt(), &dummy);
            if (!n.isValid()) return McpToolResult::text("note not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            n.setProperty(IDs::noteGain, a.value("gain").toDouble(), &um);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_note_pan", "Set a note's per-note pan (-1.0 left to 1.0 right).",
        objSchema({{"noteId", QJsonObject{{"type","integer"}}},
                  {"pan", QJsonObject{{"type","number"},{"minimum",-1.0},{"maximum",1.0}}}}, {"noteId","pan"}),
        "note",
        [e](const QJsonObject& a) -> McpToolResult {
            int dummy = 0; auto n = findNote(e, a.value("noteId").toInt(), &dummy);
            if (!n.isValid()) return McpToolResult::text("note not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            n.setProperty(IDs::notePan, a.value("pan").toDouble(), &um);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_note_pitch_offset", "Set a note's per-note pitch offset in semitones.",
        objSchema({{"noteId", QJsonObject{{"type","integer"}}},
                  {"pitchOffset", QJsonObject{{"type","number"}}}}, {"noteId","pitchOffset"}),
        "note",
        [e](const QJsonObject& a) -> McpToolResult {
            int dummy = 0; auto n = findNote(e, a.value("noteId").toInt(), &dummy);
            if (!n.isValid()) return McpToolResult::text("note not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            n.setProperty(IDs::notePitch, a.value("pitchOffset").toDouble(), &um);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_note_timbre", "Set a note's per-note timbre (0.0 dark to 1.0 bright).",
        objSchema({{"noteId", QJsonObject{{"type","integer"}}},
                  {"timbre", QJsonObject{{"type","number"},{"minimum",0.0},{"maximum",1.0}}}}, {"noteId","timbre"}),
        "note",
        [e](const QJsonObject& a) -> McpToolResult {
            int dummy = 0; auto n = findNote(e, a.value("noteId").toInt(), &dummy);
            if (!n.isValid()) return McpToolResult::text("note not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            n.setProperty(IDs::noteTimbre, a.value("timbre").toDouble(), &um);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_note_pressure", "Set a note's per-note aftertouch pressure (0.0 to 1.0).",
        objSchema({{"noteId", QJsonObject{{"type","integer"}}},
                  {"pressure", QJsonObject{{"type","number"},{"minimum",0.0},{"maximum",1.0}}}}, {"noteId","pressure"}),
        "note",
        [e](const QJsonObject& a) -> McpToolResult {
            int dummy = 0; auto n = findNote(e, a.value("noteId").toInt(), &dummy);
            if (!n.isValid()) return McpToolResult::text("note not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            n.setProperty(IDs::notePressure, a.value("pressure").toDouble(), &um);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_clip_seed", "Set the deterministic seed for a clip's operators.",
        objSchema({{"clipId", QJsonObject{{"type","integer"}}},
                  {"seed", QJsonObject{{"type","integer"}}}}, {"clipId","seed"}),
        "note",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = -1; auto c = findClip(e, a.value("clipId").toInt(), &ti);
            if (!c.isValid()) return McpToolResult::text("clip not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            c.setProperty(IDs::seed, static_cast<int64_t>(a.value("seed").toDouble()), &um);
            return McpToolResult::text("ok");
        }});
}

} // namespace mcp
