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

namespace mcp {

void registerClipTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"add_midi_clip", "Add an empty MIDI clip to a track.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                  {"start",   QJsonObject{{"type","number"}}},
                  {"length",  QJsonObject{{"type","number"}}},
                  {"name",    QJsonObject{{"type","string"}}}}, {"trackId","start","length"}),
        "clip",
        [e](const QJsonObject& a) -> McpToolResult {
            auto& m = e->getProjectModel(); auto& um = m.getUndoManager();
            int ti = a.value("trackId").toInt();
            auto tl = m.getTrackListTree();
            if (ti < 0 || ti >= tl.getNumChildren()) return McpToolResult::text("track not found", true);
            double bpm = e->getReadModel().getTransport().bpm;
            double startSec = HDAW::beatsToSeconds(a.value("start").toDouble(), bpm);
            double durSec = HDAW::beatsToSeconds(a.value("length").toDouble(), bpm);
            auto c = m.createMidiClipEmpty(
                juce::String(a.value("name").toString("MIDI Clip").toUtf8().constData()),
                startSec, durSec);
            c.setProperty(IDs::color, static_cast<int>(ProjectModel::trackColorForIndex(ti)), nullptr);
            int cid = static_cast<int>(c.getProperty(IDs::clipID));
            tl.getChild(ti).getChildWithName(IDs::CLIP_LIST).addChild(c, -1, &um);
            QJsonObject result{{"clipId", cid}};
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"add_audio_clip", "Add an audio clip referencing a source file.",
        objSchema({{"trackId",     QJsonObject{{"type","integer"}}},
                  {"start",       QJsonObject{{"type","number"}}},
                  {"length",      QJsonObject{{"type","number"}}},
                  {"sourceFile",  QJsonObject{{"type","string"}}},
                  {"name",        QJsonObject{{"type","string"}}}}, {"trackId","start","length","sourceFile"}),
        "clip",
        [e](const QJsonObject& a) -> McpToolResult {
            auto& m = e->getProjectModel(); auto& um = m.getUndoManager();
            int ti = a.value("trackId").toInt();
            auto tl = m.getTrackListTree();
            if (ti < 0 || ti >= tl.getNumChildren()) return McpToolResult::text("track not found", true);
            juce::File src(QString::fromUtf8(a.value("sourceFile").toString().toUtf8()).toStdString());
            if (!src.existsAsFile()) return McpToolResult::text("source file not found", true);
            double bpm = e->getReadModel().getTransport().bpm;
            double startSec = HDAW::beatsToSeconds(a.value("start").toDouble(), bpm);
            double durSec = HDAW::beatsToSeconds(a.value("length").toDouble(), bpm);
            auto c = m.createAudioClip(
                juce::String(a.value("name").toString("Audio Clip").toUtf8().constData()),
                startSec, durSec,
                src.getFullPathName());
            c.setProperty(IDs::color, static_cast<int>(ProjectModel::trackColorForIndex(ti)), nullptr);
            int cid = static_cast<int>(c.getProperty(IDs::clipID));
            tl.getChild(ti).getChildWithName(IDs::CLIP_LIST).addChild(c, -1, &um);
            QJsonObject result{{"clipId", cid}};
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"remove_clip", "Remove a clip (destructive).",
        objSchema({{"clipId", QJsonObject{{"type","integer"}}},
                  {"dryRun", QJsonObject{{"type","boolean"}}}}, {"clipId"}),
        "clip",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = -1; auto c = findClip(e, a.value("clipId").toInt(), &ti);
            if (!c.isValid()) return McpToolResult::text("clip not found", true);
            QString name = jstr(c.getProperty(IDs::name).toString());
            if (a.value("dryRun").toBool(false))
                return McpToolResult::text(QString("would remove clip %1 (%2)").arg(static_cast<int>(c.getProperty(IDs::clipID))).arg(name));
            e->getProjectModel().getTrackListTree().getChild(ti)
                .getChildWithName(IDs::CLIP_LIST).removeChild(c, &e->getProjectModel().getUndoManager());
            return McpToolResult::text(QString("removed clip %1").arg(static_cast<int>(c.getProperty(IDs::clipID))));
        }});

    s.registerTool({"move_clip", "Move a clip to a new start (and optionally a new track).",
        objSchema({{"clipId",  QJsonObject{{"type","integer"}}},
                  {"start",   QJsonObject{{"type","number"}}},
                  {"trackId", QJsonObject{{"type","integer"}}}}, {"clipId"}),
        "clip",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = -1; auto c = findClip(e, a.value("clipId").toInt(), &ti);
            if (!c.isValid()) return McpToolResult::text("clip not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            if (a.contains("start")) {
                double bpm = e->getReadModel().getTransport().bpm;
                double startSec = HDAW::beatsToSeconds(a.value("start").toDouble(), bpm);
                c.setProperty(IDs::startTime, startSec, &um);
            }
            if (a.contains("trackId")) {
                int nti = a.value("trackId").toInt();
                auto tl = e->getProjectModel().getTrackListTree();
                if (nti < 0 || nti >= tl.getNumChildren()) return McpToolResult::text("target track not found", true);
                e->getProjectModel().getTrackListTree().getChild(ti)
                    .getChildWithName(IDs::CLIP_LIST).removeChild(c, nullptr);
                tl.getChild(nti).getChildWithName(IDs::CLIP_LIST).addChild(c, -1, &um);
            }
            return McpToolResult::text("ok");
        }});

    s.registerTool({"ripple_delete",
        "Ripple-delete a time range: remove all clip content within "
        "[startBeat, endBeat) and shift later clips left to close the gap.",
        objSchema({{"startBeat", QJsonObject{{"type","number"}}},
                   {"endBeat",   QJsonObject{{"type","number"}}}}, {"startBeat","endBeat"}),
        "clip",
        [e](const QJsonObject& a) -> McpToolResult {
            if (!a.contains("startBeat") || !a.contains("endBeat"))
                return McpToolResult::text("startBeat and endBeat required", true);
            double sb = a.value("startBeat").toDouble();
            double eb = a.value("endBeat").toDouble();
            if (eb <= sb)
                return McpToolResult::text("endBeat must be greater than startBeat", true);
            e->getProjectCommands().rippleDelete(sb, eb);
            return McpToolResult::text(QString("rippled [%1, %2)").arg(sb).arg(eb));
        }});

    s.registerTool({"insert_silence",
        "Insert silence: split any clip crossing startBeat and shift all later "
        "content right by (endBeat - startBeat), opening an empty gap.",
        objSchema({{"startBeat", QJsonObject{{"type","number"}}},
                   {"endBeat",   QJsonObject{{"type","number"}}}}, {"startBeat","endBeat"}),
        "clip",
        [e](const QJsonObject& a) -> McpToolResult {
            if (!a.contains("startBeat") || !a.contains("endBeat"))
                return McpToolResult::text("startBeat and endBeat required", true);
            double sb = a.value("startBeat").toDouble();
            double eb = a.value("endBeat").toDouble();
            if (eb <= sb)
                return McpToolResult::text("endBeat must be greater than startBeat", true);
            e->getProjectCommands().insertSilence(sb, eb);
            return McpToolResult::text(QString("inserted silence [%1, %2)").arg(sb).arg(eb));
        }});

    s.registerTool({"duplicate_region",
        "Duplicate region: copy all clip content within [startBeat, endBeat) "
        "and paste it at endBeat, shifting later content right.",
        objSchema({{"startBeat", QJsonObject{{"type","number"}}},
                   {"endBeat",   QJsonObject{{"type","number"}}}}, {"startBeat","endBeat"}),
        "clip",
        [e](const QJsonObject& a) -> McpToolResult {
            if (!a.contains("startBeat") || !a.contains("endBeat"))
                return McpToolResult::text("startBeat and endBeat required", true);
            double sb = a.value("startBeat").toDouble();
            double eb = a.value("endBeat").toDouble();
            if (eb <= sb)
                return McpToolResult::text("endBeat must be greater than startBeat", true);
            e->getProjectCommands().duplicateRegion(sb, eb);
            return McpToolResult::text(QString("duplicated [%1, %2) to %3").arg(sb).arg(eb).arg(eb));
        }});

    s.registerTool({"set_clip", "Update clip properties (partial).",
        objSchema({{"clipId",    QJsonObject{{"type","integer"}}},
                  {"name",      QJsonObject{{"type","string"}}},
                  {"start",     QJsonObject{{"type","number"}}},
                  {"duration",  QJsonObject{{"type","number"}}},
                  {"gain",      QJsonObject{{"type","number"}}},
                  {"fadeIn",    QJsonObject{{"type","number"}}},
                  {"fadeOut",   QJsonObject{{"type","number"}}},
                  {"looping",   QJsonObject{{"type","boolean"}}}}, {"clipId"}),
        "clip",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = -1; auto c = findClip(e, a.value("clipId").toInt(), &ti);
            if (!c.isValid()) return McpToolResult::text("clip not found", true);
            auto& um = e->getProjectModel().getUndoManager();
            double bpm = e->getReadModel().getTransport().bpm;
            if (a.contains("name"))     c.setProperty(IDs::name, juce::String(a.value("name").toString().toUtf8().constData()), &um);
            if (a.contains("start"))    c.setProperty(IDs::startTime, HDAW::beatsToSeconds(a.value("start").toDouble(), bpm), &um);
            if (a.contains("duration")) c.setProperty(IDs::duration, HDAW::beatsToSeconds(a.value("duration").toDouble(), bpm), &um);
            if (a.contains("gain"))     c.setProperty(IDs::gain, a.value("gain").toDouble(), &um);
            if (a.contains("fadeIn"))   c.setProperty(IDs::fadeIn, a.value("fadeIn").toDouble(), &um);
            if (a.contains("fadeOut"))  c.setProperty(IDs::fadeOut, a.value("fadeOut").toDouble(), &um);
            if (a.contains("looping"))  c.setProperty(IDs::looping, a.value("looping").toBool(), &um);
            if (a.contains("muted"))    c.setProperty(IDs::muted, a.value("muted").toBool(), &um);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"duplicate_clip", "Duplicate a clip (destructive: creates a new clip).",
        objSchema({{"clipId",   QJsonObject{{"type","integer"}}},
                  {"start",    QJsonObject{{"type","number"}}},
                  {"trackId",  QJsonObject{{"type","integer"}}},
                  {"dryRun",   QJsonObject{{"type","boolean"}}}}, {"clipId"}),
        "clip",
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = -1; auto src = findClip(e, a.value("clipId").toInt(), &ti);
            if (!src.isValid()) return McpToolResult::text("clip not found", true);
            int nti = a.contains("trackId") ? a.value("trackId").toInt() : ti;
            double bpm = e->getReadModel().getTransport().bpm;
            double ns = a.contains("start") ? HDAW::beatsToSeconds(a.value("start").toDouble(), bpm)
                                            : static_cast<double>(src.getProperty(IDs::startTime));
            if (a.value("dryRun").toBool(false))
                return McpToolResult::text(QString("would duplicate clip %1 to track %2 @ %3")
                    .arg(static_cast<int>(src.getProperty(IDs::clipID))).arg(nti).arg(ns));
            auto& m = e->getProjectModel(); auto& um = m.getUndoManager();
            auto tl = m.getTrackListTree();
            if (nti < 0 || nti >= tl.getNumChildren()) return McpToolResult::text("track not found", true);
            auto xml = src.toXmlString();
            juce::ValueTree copy(juce::ValueTree::fromXml(xml));
            int newId = m.allocateClipID();
            copy.setProperty(IDs::clipID, newId, nullptr);
            copy.setProperty(IDs::startTime, ns, nullptr);
            tl.getChild(nti).getChildWithName(IDs::CLIP_LIST).addChild(copy, -1, &um);
            return McpToolResult::text(QString("clipId=%1").arg(newId));
        }});

    s.registerTool({"loop_clip", "Loop a clip's notes within the clip: repeats the note pattern N times, extending the clip duration. Each repetition offsets notes by the original clip duration.",
        objSchema({{"clipId",      QJsonObject{{"type","integer"}}},
                  {"repetitions", QJsonObject{{"type","integer"},{"minimum",1}}}},
                 {"clipId","repetitions"}),
        "clip",
        [e](const QJsonObject& a) -> McpToolResult {
            auto c = findClip(e, a.value("clipId").toInt(), nullptr);
            if (!c.isValid()) return McpToolResult::text("clip not found", true);
            if (c.getProperty(IDs::clipType).toString() != juce::String("midi"))
                return McpToolResult::text("clip is not MIDI", true);
            int reps = a.value("repetitions").toInt();
            if (reps < 1) return McpToolResult::text("repetitions must be >= 1", true);
            auto nl = c.getChildWithName(IDs::MIDI_NOTE_LIST);
            if (!nl.isValid() || nl.getNumChildren() == 0)
                return McpToolResult::text("clip has no notes");
            auto& m = e->getProjectModel(); auto& um = m.getUndoManager();
            double bpm = e->getReadModel().getTransport().bpm;
            double durSec = static_cast<double>(c.getProperty(IDs::duration));
            double durBeats = HDAW::secondsToBeats(durSec, bpm);
            int origCount = nl.getNumChildren();
            um.beginNewTransaction();
            for (int rep = 1; rep < reps; ++rep) {
                double offset = rep * durBeats;
                for (int k = 0; k < origCount; ++k) {
                    auto src = nl.getChild(k);
                    juce::ValueTree n(IDs::MIDI_NOTE);
                    n.setProperty(IDs::noteID, m.allocateNoteID(), nullptr);
                    n.setProperty(IDs::noteNumber, src.getProperty(IDs::noteNumber), &um);
                    n.setProperty(IDs::startBeat, static_cast<double>(src.getProperty(IDs::startBeat)) + offset, &um);
                    n.setProperty(IDs::durationBeats, src.getProperty(IDs::durationBeats), &um);
                    n.setProperty(IDs::velocity, src.getProperty(IDs::velocity), &um);
                    nl.addChild(n, -1, &um);
                }
            }
            c.setProperty(IDs::duration, durSec * reps, &um);
            int totalNotes = nl.getNumChildren();
            return McpToolResult::text(QString("looped clip %1, now %2 notes, duration %3s")
                .arg(static_cast<int>(c.getProperty(IDs::clipID))).arg(totalNotes).arg(durSec * reps));
        }});

    // ── Slicing tools ──

    s.registerTool({"slice_clip_at_playhead",
        "Slice an audio or MIDI clip at the current playhead position, splitting it into two clips.",
        objSchema({{"clipId", QJsonObject{{"type","integer"}}}}, {"clipId"}),
        "clip",
        [e](const QJsonObject& a) -> McpToolResult {
            int clipId = a.value("clipId").toInt();
            e->getProjectCommands().sliceClipAtPlayhead(clipId);
            return McpToolResult::text(QString("sliced clip %1 at playhead").arg(clipId));
        }});

    s.registerTool({"slice_clips_at_playhead",
        "Slice multiple clips at the current playhead position.",
        objSchema({{"clipIds", QJsonObject{{"type","array"},
                       {"items", QJsonObject{{"type","integer"}}}}}}, {"clipIds"}),
        "clip",
        [e](const QJsonObject& a) -> McpToolResult {
            std::vector<int> ids;
            for (const auto& v : a.value("clipIds").toArray())
                ids.push_back(v.toInt());
            e->getProjectCommands().sliceClipsAtPlayhead(ids);
            return McpToolResult::text(QString("sliced %1 clips at playhead").arg(ids.size()));
        }});

    s.registerTool({"slice_clip_at_times",
        "Slice a clip at multiple timeline-absolute beat positions. Each time splits the clip into two; multiple times produce multiple slices.",
        objSchema({{"clipId", QJsonObject{{"type","integer"}}},
                   {"times",  QJsonObject{{"type","array"},
                       {"items", QJsonObject{{"type","number"}}}}}}, {"clipId","times"}),
        "clip",
        [e](const QJsonObject& a) -> McpToolResult {
            int clipId = a.value("clipId").toInt();
            double bpm = e->getReadModel().getTransport().bpm;
            std::vector<double> timesSec;
            for (const auto& v : a.value("times").toArray())
                timesSec.push_back(HDAW::beatsToSeconds(v.toDouble(), bpm));
            e->getProjectCommands().sliceClipAtTimes(clipId, timesSec);
            return McpToolResult::text(QString("sliced clip %1 at %2 positions").arg(clipId).arg(timesSec.size()));
        }});

    s.registerTool({"slice_clip_at_transients",
        "Slice an audio clip at detected transient positions (auto-slice).",
        objSchema({{"clipId", QJsonObject{{"type","integer"}}}}, {"clipId"}),
        "clip",
        [e](const QJsonObject& a) -> McpToolResult {
            int clipId = a.value("clipId").toInt();
            e->getProjectCommands().sliceClipAtTransients(clipId);
            return McpToolResult::text(QString("sliced clip %1 at transients").arg(clipId));
        }});

    s.registerTool({"slice_clips_at_transients",
        "Slice multiple audio clips at detected transient positions.",
        objSchema({{"clipIds", QJsonObject{{"type","array"},
                       {"items", QJsonObject{{"type","integer"}}}}}}, {"clipIds"}),
        "clip",
        [e](const QJsonObject& a) -> McpToolResult {
            std::vector<int> ids;
            for (const auto& v : a.value("clipIds").toArray())
                ids.push_back(v.toInt());
            e->getProjectCommands().sliceClipsAtTransients(ids);
            return McpToolResult::text(QString("sliced %1 clips at transients").arg(ids.size()));
        }});

    // ── Timestretch tools ──

    s.registerTool({"set_clip_source_bpm",
        "Set the source BPM metadata for an audio clip (used by tempo-match and stretch).",
        objSchema({{"clipId", QJsonObject{{"type","integer"}}},
                   {"bpm",    QJsonObject{{"type","number"}}}}, {"clipId","bpm"}),
        "clip",
        [e](const QJsonObject& a) -> McpToolResult {
            int clipId = a.value("clipId").toInt();
            double bpm = a.value("bpm").toDouble();
            e->getProjectCommands().setClipSourceBpm(clipId, bpm);
            return McpToolResult::text(QString("set source BPM to %1 for clip %2").arg(bpm).arg(clipId));
        }});

    s.registerTool({"set_clip_stretch_mode",
        "Set the stretch mode for an audio clip: 0=Off, 1=TempoMatch, 2=ManualRatio.",
        objSchema({{"clipId", QJsonObject{{"type","integer"}}},
                   {"mode",   QJsonObject{{"type","integer"}}}}, {"clipId","mode"}),
        "clip",
        [e](const QJsonObject& a) -> McpToolResult {
            int clipId = a.value("clipId").toInt();
            int mode = a.value("mode").toInt();
            e->getProjectCommands().setClipStretchMode(clipId, mode);
            const char* modeName = (mode == 0) ? "Off" : (mode == 1) ? "TempoMatch" : "ManualRatio";
            return McpToolResult::text(QString("set stretch mode to %1 for clip %2").arg(modeName).arg(clipId));
        }});

    s.registerTool({"set_clip_stretch_ratio",
        "Set the stretch ratio for an audio clip (0.25 to 4.0). Only applies when stretch mode is ManualRatio.",
        objSchema({{"clipId", QJsonObject{{"type","integer"}}},
                   {"ratio",  QJsonObject{{"type","number"}}}}, {"clipId","ratio"}),
        "clip",
        [e](const QJsonObject& a) -> McpToolResult {
            int clipId = a.value("clipId").toInt();
            double ratio = a.value("ratio").toDouble();
            e->getProjectCommands().setClipStretchRatio(clipId, ratio);
            return McpToolResult::text(QString("set stretch ratio to %1 for clip %2").arg(ratio).arg(clipId));
        }});

    s.registerTool({"tempo_match_clip",
        "Auto-stretch an audio clip to match the project tempo. Requires sourceBpm to be set.",
        objSchema({{"clipId", QJsonObject{{"type","integer"}}}}, {"clipId"}),
        "clip",
        [e](const QJsonObject& a) -> McpToolResult {
            int clipId = a.value("clipId").toInt();
            e->getProjectCommands().tempoMatchClip(clipId);
            return McpToolResult::text(QString("tempo-matched clip %1").arg(clipId));
        }});

    s.registerTool({"fit_clip_to_loop",
        "Stretch an audio clip to fill the current loop region exactly.",
        objSchema({{"clipId", QJsonObject{{"type","integer"}}}}, {"clipId"}),
        "clip",
        [e](const QJsonObject& a) -> McpToolResult {
            int clipId = a.value("clipId").toInt();
            e->getProjectCommands().fitClipToLoop(clipId);
            return McpToolResult::text(QString("fit clip %1 to loop").arg(clipId));
        }});
}

} // namespace mcp
