#pragma once

// Frontend RPC layer: JSON-RPC 2.0 over WebSocket for the HTML/Electron UI.
//
// This module is the *only* code in src/frontend that knows the JSON shape of
// the engine's POD snapshot structs. The router (FrontendRouter.cpp) calls
// into the six abstract interfaces in src/common/ and hands the results to
// these converters to serialize for the wire.
//
// Design rules (mirror the AGENTS.md "GUI-Engine Decoupling" contract):
//   - Never leak juce::ValueTree / juce::String / juce::Identifier over the wire.
//   - Every method on the six interfaces maps 1:1 to a "namespace.method" name.
//   - Mutations return either a primitive (the new id, a bool) or null.
//   - Reads return a JSON object/array built from the *Snapshot structs.

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QString>

#include "../common/ReadModel.h"

namespace frontend {

// JSON-RPC method namespace prefixes. Keep these in sync with the TypeScript
// client (frontend/src/rpc/client.ts) and the router switch in FrontendRouter.cpp.
namespace method {
    inline constexpr const char* Project    = "project";
    inline constexpr const char* Transport  = "transport";
    inline constexpr const char* AudioGraph = "audioGraph";
    inline constexpr const char* Read       = "read";
    inline constexpr const char* Plugin     = "plugin";
    inline constexpr const char* PluginParam = "pluginParam";
    inline constexpr const char* Audio     = "audio";
    inline constexpr const char* Midi       = "midi";
    inline constexpr const char* Export     = "export";
    inline constexpr const char* Preview    = "preview";
    inline constexpr const char* Composition = "composition";
    inline constexpr const char* Session    = "session";
    inline constexpr const char* Library    = "library";
    inline constexpr const char* Sampler    = "sampler";
} // namespace method

// Server-initiated push notifications (no client id, no response expected).
namespace notify {
    inline constexpr const char* TreeChanged    = "notify.treeChanged";
    inline constexpr const char* Meters         = "notify.meters";
    inline constexpr const char* FmAnalysis     = "notify.fmAnalysis";
    inline constexpr const char* Transport      = "notify.transport";
    inline constexpr const char* ScanProgress   = "notify.scanProgress";
    inline constexpr const char* LibraryScanProgress = "notify.libraryScanProgress";
    inline constexpr const char* LibraryScanComplete = "notify.libraryScanComplete";
    inline constexpr const char* ExportProgress = "notify.exportProgress";
    inline constexpr const char* LoadProgress   = "notify.loadProgress";
    inline constexpr const char* SessionState   = "notify.sessionStateChanged";
    inline constexpr const char* PluginCrashed  = "notify.pluginCrashed";
    inline constexpr const char* PluginRecovered = "notify.pluginRecovered";
} // namespace notify

// Normalized dispatch outcome. Mirrors mcp::McpServer::DispatchResult so the
// WebSocket transport code reads the same way the HTTP/stdio code does.
struct DispatchResult {
    bool isError = false;
    QJsonValue payload;        // bare result on success; {code,message} on error
};

// Build a JSON-RPC error payload.
inline DispatchResult makeError(int code, const QString& message) {
    return DispatchResult{ true, QJsonObject{
        { "code", code }, { "message", message } } };
}

// ---- Snapshot → JSON converters --------------------------------------------

inline QJsonObject toJson(const TrackSnapshot& t) {
    return QJsonObject{
        { "index",          t.index },
        { "name",           QString::fromStdString(t.name) },
        { "color",          t.color },
        { "volume",         t.volume },
        { "pan",            t.pan },
        { "muted",          t.muted },
        { "soloed",         t.soloed },
        { "armed",          t.armed },
        { "inputMonitor",   t.inputMonitor },
        { "height",         t.height },
        { "midiChannel",    t.midiChannel },
        { "trackType",      t.trackType },
        { "isCollapsed",    t.isCollapsed },
        { "isHidden",       t.isHidden },
        { "effectiveMuted", t.effectiveMuted },
        { "effectiveSoloed",t.effectiveSoloed },
        { "parentId",       t.parentId },
        { "clipCount",      t.clipCount },
    };
}

inline QJsonObject toJson(const ClipSnapshot::GainEnvelopePoint& p) {
    return QJsonObject{ { "time", p.time }, { "gain", p.gain } };
}

inline QJsonObject toJson(const ClipSnapshot& c) {
    QJsonObject o{
        { "clipId",         c.clipId },
        { "trackIndex",     c.trackIndex },
        { "name",           QString::fromStdString(c.name) },
        { "sourceFile",     QString::fromStdString(c.sourceFile) },
        { "startBeat",      c.startBeat },
        { "durationBeats",  c.durationBeats },
        { "offset",         c.offset },
        { "gain",           c.gain },
        { "fadeIn",         c.fadeIn },
        { "fadeOut",        c.fadeOut },
        { "looping",        c.looping },
        { "isMidi",         c.isMidi },
        { "sourceBpm",      c.sourceBpm },
        { "stretchMode",    c.stretchMode },
        { "stretchRatio",   c.stretchRatio },
        { "sourceDuration", c.sourceDuration },
        { "isGhost",       c.isGhost },
        { "ghostSourceId", c.ghostSourceId },
        { "sceneIndex",    c.sceneIndex },
    };
    QJsonArray env;
    for (const auto& p : c.gainEnvelope) env.append(toJson(p));
    o.insert("gainEnvelope", env);
    return o;
}

inline QJsonObject toJson(const NoteSnapshot& n) {
    return QJsonObject{
        { "noteId",        n.noteId },
        { "pitch",         n.pitch },
        { "velocity",      n.velocity },
        { "startBeat",     n.startBeat },
        { "durationBeats", n.durationBeats },
        { "chance",        static_cast<double>(n.chance) },
        { "repeatCount",   n.repeatCount },
        { "repeatRate",    static_cast<double>(n.repeatRate) },
        { "repeatCurve",   static_cast<double>(n.repeatCurve) },
        { "occurrence",    n.occurrence },
        { "recurrence",    n.recurrence },
        { "noteGain",      static_cast<double>(n.noteGain) },
        { "notePan",       static_cast<double>(n.notePan) },
        { "notePitch",     static_cast<double>(n.notePitch) },
        { "noteTimbre",    static_cast<double>(n.noteTimbre) },
        { "notePressure",  static_cast<double>(n.notePressure) },
    };
}

inline QJsonObject toJson(const CcPointSnapshot& c) {
    return QJsonObject{
        { "ccId",             c.ccId },
        { "controllerNumber", c.controllerNumber },
        { "beat",             c.beat },
        { "value",            c.value },
    };
}

inline QJsonObject toJson(const TransportSnapshot& t) {
    return QJsonObject{
        { "bpm",              t.bpm },
        { "isPlaying",        t.isPlaying },
        { "isLooping",        t.isLooping },
        { "isRecording",      t.isRecording },
        { "punchEnabled",     t.punchEnabled },
        { "loopStart",        t.loopStart },
        { "loopEnd",          t.loopEnd },
        { "currentTimeSeconds", t.currentTimeSeconds },
        { "sampleRate",       t.sampleRate },
        { "timeSigNumerator", t.timeSigNumerator },
        { "timeSigDenominator", t.timeSigDenominator },
    };
}

inline QJsonObject toJson(const FxSlotSnapshot& f) {
    return QJsonObject{
        { "slotIndex",  f.slotIndex },
        { "fxType",     QString::fromStdString(f.fxType) },
        { "pluginId",   QString::fromStdString(f.pluginId) },
        { "pluginName", QString::fromStdString(f.pluginName) },
        { "bypassed",   f.bypassed },
        { "paramCount", f.paramCount },
    };
}

inline QJsonObject toJson(const MidiFxSlotSnapshot& f) {
    QJsonObject obj{
        { "slotIndex", f.slotIndex },
        { "fxType",    QString::fromStdString(f.fxType) },
        { "bypassed",  f.bypassed },
    };
    QJsonObject params;
    for (auto& [k, v] : f.params)
    {
        if (std::holds_alternative<double>(v))
            params[QString::fromStdString(k)] = std::get<double>(v);
        else if (std::holds_alternative<std::string>(v))
            params[QString::fromStdString(k)] = QString::fromStdString(std::get<std::string>(v));
    }
    obj["params"] = params;
    return obj;
}

inline QJsonObject toJson(const AutomationLaneSnapshot& l) {
    return QJsonObject{
        { "laneIndex", l.laneIndex },
        { "name",      QString::fromStdString(l.name) },
        { "paramID",   l.paramID },
        { "enabled",   l.enabled },
        { "mode",      QString::fromStdString(l.mode) },
    };
}

inline QJsonObject toJson(const AutomationPointSnapshot& p) {
    return QJsonObject{ { "time", p.time }, { "value", static_cast<double>(p.value) } };
}

inline QJsonObject toJson(const MarkerSnapshot& m) {
    return QJsonObject{
        { "index", m.index },
        { "time",  m.time },
        { "name",  QString::fromStdString(m.name) },
        { "color", m.color },
    };
}

inline QJsonObject toJson(const ArrangerRegionSnapshot& r) {
    return QJsonObject{
        { "regionID",  QString::fromStdString(r.regionID) },
        { "name",      QString::fromStdString(r.name) },
        { "startTime", r.startTime },
        { "duration",  r.duration },
        { "color",     r.color },
    };
}

inline QJsonObject toJson(const ChainEntrySnapshot& e) {
    return QJsonObject{
        { "regionID",    QString::fromStdString(e.regionID) },
        { "repeatCount", e.repeatCount },
    };
}

inline QJsonObject toJson(const ArrangerChainSnapshot& c) {
    QJsonArray entries;
    for (const auto& e : c.entries)
        entries.append(toJson(e));
    return QJsonObject{
        { "chainID",  QString::fromStdString(c.chainID) },
        { "name",     QString::fromStdString(c.name) },
        { "isActive", c.isActive },
        { "entries",  entries },
    };
}

inline QJsonObject toJson(const TempoPointSnapshot& t) {
    return QJsonObject{ { "timeSeconds", t.timeSeconds }, { "bpm", t.bpm } };
}

inline QJsonObject toJson(const AutomatableParamSnapshot& a) {
    return QJsonObject{
        { "slotIndex",  a.slotIndex },
        { "paramIndex", a.paramIndex },
        { "name",       QString::fromStdString(a.name) },
        { "automatable", a.automatable },
    };
}

inline QJsonObject toJson(const MeterSnapshot& m, const char* leftKey = "l", const char* rightKey = "r") {
    return QJsonObject{
        { leftKey,  static_cast<double>(m.leftLevel) },
        { rightKey, static_cast<double>(m.rightLevel) },
        { "rmsL",  static_cast<double>(m.rmsLeftLevel) },
        { "rmsR",  static_cast<double>(m.rmsRightLevel) },
        { "lufs",  static_cast<double>(m.lufsMomentary) },
    };
}

inline QJsonObject toJson(const FmAnalysisSnapshot& a) {
    QJsonArray levels;
    for (int i = 0; i < 6; ++i)
        levels.append(static_cast<double>(a.opEgLevel[i]));
    return QJsonObject{
        { "opEgLevel", levels },
        { "activeVoices", a.activeVoices },
        { "algorithm", a.algorithm },
    };
}

inline QJsonObject toJson(const InternalFxParamSnapshot& p) {
    return QJsonObject{
        { "paramIndex",    p.paramIndex },
        { "name",          QString::fromStdString(p.name) },
        { "value",         static_cast<double>(p.value) },
        { "minValue",      static_cast<double>(p.minValue) },
        { "maxValue",      static_cast<double>(p.maxValue) },
        { "defaultValue",  static_cast<double>(p.defaultValue) },
    };
}

inline QJsonObject toJson(const LfoSnapshot& l) {
    return QJsonObject{
        { "index",        l.index },
        { "name",         QString::fromStdString(l.name) },
        { "waveform",     l.waveform },
        { "rate",         l.rate },
        { "rateSync",     l.rateSync },
        { "depth",        l.depth },
        { "bipolar",      l.bipolar },
        { "phaseOffset",  l.phaseOffset },
        { "targetParamID", l.targetParamID },
        { "enabled",      l.enabled },
    };
}

inline QJsonObject toJson(const SendSnapshot& s) {
    return QJsonObject{
        { "sendIndex",  s.sendIndex },
        { "level",      static_cast<double>(s.level) },
        { "isPreFader", s.isPreFader },
        { "bypassed",   s.bypassed },
    };
}

inline QJsonObject toJson(const ProjectSnapshot& s) {
    QJsonObject o{
        { "name", QString::fromStdString(s.name) },
        { "transport", toJson(s.transport) },
        { "scaleRoot", s.scaleRoot },
        { "scaleMode", s.scaleMode },
        { "launchedScene", s.launchedScene },
        { "sceneCount",    s.sceneCount },
        { "createdWithApp", QString::fromStdString(s.createdWithApp) },
        { "savedWithApp",   QString::fromStdString(s.savedWithApp) },
        { "formatVersion",  s.formatVersion },
    };
    QJsonArray tracks;
    for (const auto& t : s.tracks) tracks.append(toJson(t));
    o.insert("tracks", tracks);
    QJsonArray clips;
    for (const auto& c : s.clips) clips.append(toJson(c));
    o.insert("clips", clips);
    return o;
}

} // namespace frontend
