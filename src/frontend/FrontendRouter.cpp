#include "FrontendRouter.h"

#include "../engine/AudioEngine.h"
#include "../common/ProjectCommands.h"
#include "../common/TransportCommands.h"
#include "../common/AudioGraphCommands.h"
#include "../common/ReadModel.h"
#include "../common/PluginService.h"
#include "../common/PluginParamService.h"
#include "../common/MidiService.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace frontend {

// ---- Small param-extraction helpers ----------------------------------------
// Every helper returns true on success and writes the out-param. On failure
// they return false and set *err to a JSON-RPC InvalidParams payload.

namespace {

template <typename T>
bool requireInt(const QJsonObject& o, const char* key, T& out, DispatchResult* err) {
    if (!o.contains(key) || !o.value(key).isDouble()) {
        if (err) *err = makeError(-32602, QString("missing or non-numeric param: ") + key);
        return false;
    }
    out = static_cast<T>(o.value(key).toDouble());
    return true;
}

bool requireDouble(const QJsonObject& o, const char* key, double& out, DispatchResult* err) {
    if (!o.contains(key) || !o.value(key).isDouble()) {
        if (err) *err = makeError(-32602, QString("missing or non-numeric param: ") + key);
        return false;
    }
    out = o.value(key).toDouble();
    return true;
}

bool requireFloat(const QJsonObject& o, const char* key, float& out, DispatchResult* err) {
    double d = 0.0;
    if (!requireDouble(o, key, d, err)) return false;
    out = static_cast<float>(d);
    return true;
}

bool requireBool(const QJsonObject& o, const char* key, bool& out, DispatchResult* err) {
    if (!o.contains(key) || !o.value(key).isBool()) {
        if (err) *err = makeError(-32602, QString("missing or non-boolean param: ") + key);
        return false;
    }
    out = o.value(key).toBool();
    return true;
}

bool requireString(const QJsonObject& o, const char* key, std::string& out, DispatchResult* err) {
    if (!o.contains(key) || !o.value(key).isString()) {
        if (err) *err = makeError(-32602, QString("missing or non-string param: ") + key);
        return false;
    }
    out = o.value(key).toString().toStdString();
    return true;
}

// Optional with default. Returns the default if the key is absent; only errors
// if the key is present but the wrong type.
template <typename T>
T optInt(const QJsonObject& o, const char* key, T fallback, DispatchResult* err) {
    if (!o.contains(key)) return fallback;
    if (!o.value(key).isDouble()) {
        if (err) *err = makeError(-32602, QString("non-numeric param: ") + key);
        return fallback;
    }
    return static_cast<T>(o.value(key).toDouble());
}

double optDouble(const QJsonObject& o, const char* key, double fallback, DispatchResult* err) {
    if (!o.contains(key)) return fallback;
    if (!o.value(key).isDouble()) {
        if (err) *err = makeError(-32602, QString("non-numeric param: ") + key);
        return fallback;
    }
    return o.value(key).toDouble();
}

float optFloat(const QJsonObject& o, const char* key, float fallback, DispatchResult* err) {
    return static_cast<float>(optDouble(o, key, static_cast<double>(fallback), err));
}

bool optBool(const QJsonObject& o, const char* key, bool fallback, DispatchResult* err) {
    if (!o.contains(key)) return fallback;
    if (!o.value(key).isBool()) {
        if (err) *err = makeError(-32602, QString("non-boolean param: ") + key);
        return fallback;
    }
    return o.value(key).toBool();
}

std::string optString(const QJsonObject& o, const char* key, std::string fallback) {
    if (!o.contains(key) || !o.value(key).isString()) return fallback;
    return o.value(key).toString().toStdString();
}

// Extract a params object from a QJsonValue that may be null/undefined.
QJsonObject paramsObject(const QJsonValue& params) {
    return params.isObject() ? params.toObject() : QJsonObject{};
}

// Extract a vector<double> from a JSON array of numbers.
std::vector<double> toDoubleVector(const QJsonValue& v, DispatchResult* err) {
    std::vector<double> out;
    if (!v.isArray()) {
        if (err) *err = makeError(-32602, "expected an array of numbers");
        return out;
    }
    for (const auto& e : v.toArray()) {
        if (!e.isDouble()) {
            if (err) *err = makeError(-32602, "array element is not a number");
            return {};
        }
        out.push_back(e.toDouble());
    }
    return out;
}

// ---- Per-namespace dispatch -------------------------------------------------

DispatchResult dispatchProject(ProjectCommands& c, const QString& m, const QJsonValue& params) {
    const auto o = paramsObject(params);

    // --- Tracks ---
    if (m == "addTrack") {
        std::string name; if (!requireString(o, "name", name, nullptr)) name = "Track";
        int color = optInt(o, "color", -1, nullptr);
        int parentBus = optInt(o, "parentBus", -1, nullptr);
        return { false, c.addTrack(name, color, parentBus) };
    }
    if (m == "removeTrack")     { int i; if (!requireInt(o, "trackIndex", i, nullptr)) return makeError(-32602, "trackIndex required"); c.removeTrack(i); return { false, QJsonValue::Null }; }
    if (m == "moveTrack")       { int i, n; if (!requireInt(o, "trackIndex", i, nullptr) || !requireInt(o, "newIndex", n, nullptr)) return makeError(-32602, "trackIndex and newIndex required"); c.moveTrack(i, n); return { false, QJsonValue::Null }; }
    if (m == "duplicateTrack")  { int i; if (!requireInt(o, "trackIndex", i, nullptr)) return makeError(-32602, "trackIndex required"); return { false, c.duplicateTrack(i) }; }
    if (m == "setTrackName")    { int i; std::string s; if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "name", s, nullptr)) return makeError(-32602, "trackIndex and name required"); c.setTrackName(i, s); return { false, QJsonValue::Null }; }
    if (m == "setTrackColor")   { int i, color; if (!requireInt(o, "trackIndex", i, nullptr) || !requireInt(o, "color", color, nullptr)) return makeError(-32602, "trackIndex and color required"); c.setTrackColor(i, color); return { false, QJsonValue::Null }; }
    if (m == "setTrackVolume")  { int i; float v;   if (!requireInt(o, "trackIndex", i, nullptr) || !requireFloat(o, "volume", v, nullptr)) return makeError(-32602, "trackIndex and volume required"); c.setTrackVolume(i, v); return { false, QJsonValue::Null }; }
    if (m == "setTrackPan")     { int i; float v;   if (!requireInt(o, "trackIndex", i, nullptr) || !requireFloat(o, "pan", v, nullptr))     return makeError(-32602, "trackIndex and pan required"); c.setTrackPan(i, v); return { false, QJsonValue::Null }; }
    if (m == "setTrackMuted")   { int i; bool b;    if (!requireInt(o, "trackIndex", i, nullptr) || !requireBool(o, "muted", b, nullptr))   return makeError(-32602, "trackIndex and muted required"); c.setTrackMuted(i, b); return { false, QJsonValue::Null }; }
    if (m == "setTrackSoloed")  { int i; bool b;    if (!requireInt(o, "trackIndex", i, nullptr) || !requireBool(o, "soloed", b, nullptr))  return makeError(-32602, "trackIndex and soloed required"); c.setTrackSoloed(i, b); return { false, QJsonValue::Null }; }
    if (m == "setTrackArmed")   { int i; bool b;    if (!requireInt(o, "trackIndex", i, nullptr) || !requireBool(o, "armed", b, nullptr))   return makeError(-32602, "trackIndex and armed required"); c.setTrackArmed(i, b); return { false, QJsonValue::Null }; }
    if (m == "setTrackInputMonitor") { int i; bool b; if (!requireInt(o, "trackIndex", i, nullptr) || !requireBool(o, "monitor", b, nullptr)) return makeError(-32602, "trackIndex and monitor required"); c.setTrackInputMonitor(i, b); return { false, QJsonValue::Null }; }
    if (m == "setTrackHeight")  { int i, h; if (!requireInt(o, "trackIndex", i, nullptr) || !requireInt(o, "height", h, nullptr)) return makeError(-32602, "trackIndex and height required"); c.setTrackHeight(i, h); return { false, QJsonValue::Null }; }
    if (m == "setTrackMidiChannel") { int i, ch; if (!requireInt(o, "trackIndex", i, nullptr) || !requireInt(o, "channel", ch, nullptr)) return makeError(-32602, "trackIndex and channel required"); c.setTrackMidiChannel(i, ch); return { false, QJsonValue::Null }; }

    // --- Clips ---
    if (m == "addAudioClip") {
        int i; double start, dur; std::string src, name;
        if (!requireInt(o, "trackIndex", i, nullptr) || !requireDouble(o, "start", start, nullptr) || !requireDouble(o, "duration", dur, nullptr) || !requireString(o, "sourceFile", src, nullptr) || !requireString(o, "name", name, nullptr))
            return makeError(-32602, "trackIndex, start, duration, sourceFile, name required");
        return { false, c.addAudioClip(i, start, dur, src, name) };
    }
    if (m == "addMidiClip") {
        int i; double start, dur; std::string name;
        if (!requireInt(o, "trackIndex", i, nullptr) || !requireDouble(o, "start", start, nullptr) || !requireDouble(o, "duration", dur, nullptr) || !requireString(o, "name", name, nullptr))
            return makeError(-32602, "trackIndex, start, duration, name required");
        return { false, c.addMidiClip(i, start, dur, name) };
    }
    if (m == "removeClip")      { int i; if (!requireInt(o, "clipId", i, nullptr)) return makeError(-32602, "clipId required"); c.removeClip(i); return { false, QJsonValue::Null }; }
    if (m == "moveClip")        { int i, t; double s; if (!requireInt(o, "clipId", i, nullptr) || !requireInt(o, "newTrackIndex", t, nullptr) || !requireDouble(o, "newStart", s, nullptr)) return makeError(-32602, "clipId, newTrackIndex, newStart required"); c.moveClip(i, t, s); return { false, QJsonValue::Null }; }
    if (m == "duplicateClip")   { int i; if (!requireInt(o, "clipId", i, nullptr)) return makeError(-32602, "clipId required"); return { false, c.duplicateClip(i) }; }
    if (m == "setClipStart")    { int i; double v; if (!requireInt(o, "clipId", i, nullptr) || !requireDouble(o, "start", v, nullptr)) return makeError(-32602, "clipId and start required"); c.setClipStart(i, v); return { false, QJsonValue::Null }; }
    if (m == "setClipDuration") { int i; double v; if (!requireInt(o, "clipId", i, nullptr) || !requireDouble(o, "duration", v, nullptr)) return makeError(-32602, "clipId and duration required"); c.setClipDuration(i, v); return { false, QJsonValue::Null }; }
    if (m == "setClipGain")     { int i; float v;  if (!requireInt(o, "clipId", i, nullptr) || !requireFloat(o, "gain", v, nullptr)) return makeError(-32602, "clipId and gain required"); c.setClipGain(i, v); return { false, QJsonValue::Null }; }
    if (m == "setClipFadeIn")   { int i; double v; if (!requireInt(o, "clipId", i, nullptr) || !requireDouble(o, "fadeIn", v, nullptr)) return makeError(-32602, "clipId and fadeIn required"); c.setClipFadeIn(i, v); return { false, QJsonValue::Null }; }
    if (m == "setClipFadeOut")  { int i; double v; if (!requireInt(o, "clipId", i, nullptr) || !requireDouble(o, "fadeOut", v, nullptr)) return makeError(-32602, "clipId and fadeOut required"); c.setClipFadeOut(i, v); return { false, QJsonValue::Null }; }
    if (m == "setClipOffset")   { int i; double v; if (!requireInt(o, "clipId", i, nullptr) || !requireDouble(o, "offset", v, nullptr)) return makeError(-32602, "clipId and offset required"); c.setClipOffset(i, v); return { false, QJsonValue::Null }; }
    if (m == "setClipLooping")  { int i; bool b;   if (!requireInt(o, "clipId", i, nullptr) || !requireBool(o, "looping", b, nullptr)) return makeError(-32602, "clipId and looping required"); c.setClipLooping(i, b); return { false, QJsonValue::Null }; }

    // --- Timestretch ---
    if (m == "setClipSourceBpm")    { int i; double v; if (!requireInt(o, "clipId", i, nullptr) || !requireDouble(o, "bpm", v, nullptr)) return makeError(-32602, "clipId and bpm required"); c.setClipSourceBpm(i, v); return { false, QJsonValue::Null }; }
    if (m == "setClipStretchMode")  { int i, mode;   if (!requireInt(o, "clipId", i, nullptr) || !requireInt(o, "mode", mode, nullptr)) return makeError(-32602, "clipId and mode required"); c.setClipStretchMode(i, mode); return { false, QJsonValue::Null }; }
    if (m == "setClipStretchRatio") { int i; double v; if (!requireInt(o, "clipId", i, nullptr) || !requireDouble(o, "ratio", v, nullptr)) return makeError(-32602, "clipId and ratio required"); c.setClipStretchRatio(i, v); return { false, QJsonValue::Null }; }
    if (m == "tempoMatchClip")      { int i; if (!requireInt(o, "clipId", i, nullptr)) return makeError(-32602, "clipId required"); c.tempoMatchClip(i); return { false, QJsonValue::Null }; }
    if (m == "fitClipToLoop")       { int i; if (!requireInt(o, "clipId", i, nullptr)) return makeError(-32602, "clipId required"); c.fitClipToLoop(i); return { false, QJsonValue::Null }; }

    // --- MIDI notes ---
    if (m == "addNote") {
        int clip, pitch, vel; double start, dur;
        if (!requireInt(o, "clipId", clip, nullptr) || !requireInt(o, "pitch", pitch, nullptr) || !requireInt(o, "velocity", vel, nullptr) || !requireDouble(o, "startBeat", start, nullptr) || !requireDouble(o, "durationBeats", dur, nullptr))
            return makeError(-32602, "clipId, pitch, velocity, startBeat, durationBeats required");
        return { false, c.addNote(clip, pitch, vel, start, dur) };
    }
    if (m == "removeNote")      { int i; if (!requireInt(o, "noteId", i, nullptr)) return makeError(-32602, "noteId required"); c.removeNote(i); return { false, QJsonValue::Null }; }
    if (m == "setNotePitch")    { int i, v; if (!requireInt(o, "noteId", i, nullptr) || !requireInt(o, "pitch", v, nullptr)) return makeError(-32602, "noteId and pitch required"); c.setNotePitch(i, v); return { false, QJsonValue::Null }; }
    if (m == "setNoteVelocity") { int i, v; if (!requireInt(o, "noteId", i, nullptr) || !requireInt(o, "velocity", v, nullptr)) return makeError(-32602, "noteId and velocity required"); c.setNoteVelocity(i, v); return { false, QJsonValue::Null }; }
    if (m == "setNoteStart")    { int i; double v; if (!requireInt(o, "noteId", i, nullptr) || !requireDouble(o, "startBeat", v, nullptr)) return makeError(-32602, "noteId and startBeat required"); c.setNoteStart(i, v); return { false, QJsonValue::Null }; }
    if (m == "setNoteDuration") { int i; double v; if (!requireInt(o, "noteId", i, nullptr) || !requireDouble(o, "durationBeats", v, nullptr)) return makeError(-32602, "noteId and durationBeats required"); c.setNoteDuration(i, v); return { false, QJsonValue::Null }; }
    if (m == "clearNotes")      { int i; if (!requireInt(o, "clipId", i, nullptr)) return makeError(-32602, "clipId required"); c.clearNotes(i); return { false, QJsonValue::Null }; }
    if (m == "addCcPoint")      { int clip, cc; double beat; int val; if (!requireInt(o, "clipId", clip, nullptr) || !requireInt(o, "controllerNumber", cc, nullptr) || !requireDouble(o, "beat", beat, nullptr) || !requireInt(o, "value", val, nullptr)) return makeError(-32602, "clipId, controllerNumber, beat, value required"); c.addCcPoint(clip, cc, beat, val); return { false, QJsonValue::Null }; }

    // --- FX ---
    if (m == "addFxSlot") {
        int i; std::string type; int pos; std::string pluginId;
        if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "type", type, nullptr))
            return makeError(-32602, "trackIndex and type required");
        pos = optInt(o, "position", -1, nullptr);
        pluginId = optString(o, "pluginId", "");
        c.addFxSlot(i, type, pos, pluginId);  // string overload
        return { false, QJsonValue::Null };
    }
    if (m == "removeFxSlot")        { int i, s; if (!requireInt(o, "trackIndex", i, nullptr) || !requireInt(o, "slotIndex", s, nullptr)) return makeError(-32602, "trackIndex and slotIndex required"); c.removeFxSlot(i, s); return { false, QJsonValue::Null }; }
    if (m == "setFxSlotBypassed")   { int i, s; bool b; if (!requireInt(o, "trackIndex", i, nullptr) || !requireInt(o, "slotIndex", s, nullptr) || !requireBool(o, "bypassed", b, nullptr)) return makeError(-32602, "trackIndex, slotIndex, bypassed required"); c.setFxSlotBypassed(i, s, b); return { false, QJsonValue::Null }; }
    if (m == "setFxSlotParam")      { int i, s, p; float v; if (!requireInt(o, "trackIndex", i, nullptr) || !requireInt(o, "slotIndex", s, nullptr) || !requireInt(o, "paramIndex", p, nullptr) || !requireFloat(o, "value", v, nullptr)) return makeError(-32602, "trackIndex, slotIndex, paramIndex, value required"); c.setFxSlotParam(i, s, p, v); return { false, QJsonValue::Null }; }
    if (m == "reorderFxSlots")      { int i, f, t; if (!requireInt(o, "trackIndex", i, nullptr) || !requireInt(o, "fromSlot", f, nullptr) || !requireInt(o, "toSlot", t, nullptr)) return makeError(-32602, "trackIndex, fromSlot, toSlot required"); c.reorderFxSlots(i, f, t); return { false, QJsonValue::Null }; }
    if (m == "setFxSlotPlugin") {
        int i, s; std::string fxType, pluginID, fmt, path;
        if (!requireInt(o, "trackIndex", i, nullptr) || !requireInt(o, "slotIndex", s, nullptr) || !requireString(o, "fxType", fxType, nullptr) || !requireString(o, "pluginID", pluginID, nullptr) || !requireString(o, "pluginFormat", fmt, nullptr) || !requireString(o, "pluginPath", path, nullptr))
            return makeError(-32602, "trackIndex, slotIndex, fxType, pluginID, pluginFormat, pluginPath required");
        c.setFxSlotPlugin(i, s, fxType, pluginID, fmt, path); return { false, QJsonValue::Null };
    }

    // --- Automation ---
    if (m == "addAutomationLane")       { int i; std::string lane; if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "laneName", lane, nullptr)) return makeError(-32602, "trackIndex and laneName required"); c.addAutomationLane(i, lane); return { false, QJsonValue::Null }; }
    if (m == "removeAutomationLane")    { int i; std::string lane; if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "laneName", lane, nullptr)) return makeError(-32602, "trackIndex and laneName required"); c.removeAutomationLane(i, lane); return { false, QJsonValue::Null }; }
    if (m == "addAutomationPoint")      { int i; std::string lane; double t; float v; if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "lane", lane, nullptr) || !requireDouble(o, "time", t, nullptr) || !requireFloat(o, "value", v, nullptr)) return makeError(-32602, "trackIndex, lane, time, value required"); c.addAutomationPoint(i, lane, t, v); return { false, QJsonValue::Null }; }
    if (m == "removeAutomationPoint")   { int i; std::string lane; double t; if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "lane", lane, nullptr) || !requireDouble(o, "time", t, nullptr)) return makeError(-32602, "trackIndex, lane, time required"); c.removeAutomationPoint(i, lane, t); return { false, QJsonValue::Null }; }
    if (m == "setAutomationEnabled")    { int i; std::string lane; bool b; if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "lane", lane, nullptr) || !requireBool(o, "enabled", b, nullptr)) return makeError(-32602, "trackIndex, lane, enabled required"); c.setAutomationEnabled(i, lane, b); return { false, QJsonValue::Null }; }
    if (m == "setAutomationPointValue") { int i; std::string lane; double t; float v; if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "lane", lane, nullptr) || !requireDouble(o, "time", t, nullptr) || !requireFloat(o, "value", v, nullptr)) return makeError(-32602, "trackIndex, lane, time, value required"); c.setAutomationPointValue(i, lane, t, v); return { false, QJsonValue::Null }; }

    // --- Transport properties ---
    if (m == "setTempo")            { double v; if (!requireDouble(o, "bpm", v, nullptr)) return makeError(-32602, "bpm required"); c.setTempo(v); return { false, QJsonValue::Null }; }
    if (m == "setLoopStart")        { double v; if (!requireDouble(o, "beat", v, nullptr)) return makeError(-32602, "beat required"); c.setLoopStart(v); return { false, QJsonValue::Null }; }
    if (m == "setLoopEnd")          { double v; if (!requireDouble(o, "beat", v, nullptr)) return makeError(-32602, "beat required"); c.setLoopEnd(v); return { false, QJsonValue::Null }; }
    if (m == "setLooping")          { bool b; if (!requireBool(o, "looping", b, nullptr)) return makeError(-32602, "looping required"); c.setLooping(b); return { false, QJsonValue::Null }; }
    if (m == "setMetronomeEnabled") { bool b; if (!requireBool(o, "enabled", b, nullptr)) return makeError(-32602, "enabled required"); c.setMetronomeEnabled(b); return { false, QJsonValue::Null }; }
    if (m == "setTimeSignature")    { int n, d; if (!requireInt(o, "numerator", n, nullptr) || !requireInt(o, "denominator", d, nullptr)) return makeError(-32602, "numerator and denominator required"); c.setTimeSignature(n, d); return { false, QJsonValue::Null }; }

    // --- Markers ---
    if (m == "addMarker")      { std::string name; double t; if (!requireString(o, "name", name, nullptr) || !requireDouble(o, "time", t, nullptr)) return makeError(-32602, "name and time required"); int color = optInt<int>(o, "color", 0xFF59e0c4, nullptr); return { false, c.addMarker(name, t, color) }; }
    if (m == "removeMarker")   { int i; if (!requireInt(o, "index", i, nullptr)) return makeError(-32602, "index required"); c.removeMarker(i); return { false, QJsonValue::Null }; }
    if (m == "setMarkerName")  { int i; std::string s; if (!requireInt(o, "index", i, nullptr) || !requireString(o, "name", s, nullptr)) return makeError(-32602, "index and name required"); c.setMarkerName(i, s); return { false, QJsonValue::Null }; }
    if (m == "setMarkerTime")  { int i; double t; if (!requireInt(o, "index", i, nullptr) || !requireDouble(o, "time", t, nullptr)) return makeError(-32602, "index and time required"); c.setMarkerTime(i, t); return { false, QJsonValue::Null }; }

    // --- Gain envelope ---
    if (m == "addGainEnvelopePoint")    { int i; double t, g; if (!requireInt(o, "clipId", i, nullptr) || !requireDouble(o, "time", t, nullptr) || !requireDouble(o, "gain", g, nullptr)) return makeError(-32602, "clipId, time, gain required"); c.addGainEnvelopePoint(i, t, g); return { false, QJsonValue::Null }; }
    if (m == "moveGainEnvelopePoint")   { int i, idx; double t, g; if (!requireInt(o, "clipId", i, nullptr) || !requireInt(o, "pointIndex", idx, nullptr) || !requireDouble(o, "time", t, nullptr) || !requireDouble(o, "gain", g, nullptr)) return makeError(-32602, "clipId, pointIndex, time, gain required"); c.moveGainEnvelopePoint(i, idx, t, g); return { false, QJsonValue::Null }; }
    if (m == "removeGainEnvelopePoint") { int i, idx; if (!requireInt(o, "clipId", i, nullptr) || !requireInt(o, "pointIndex", idx, nullptr)) return makeError(-32602, "clipId and pointIndex required"); c.removeGainEnvelopePoint(i, idx); return { false, QJsonValue::Null }; }
    if (m == "clearGainEnvelope")       { int i; if (!requireInt(o, "clipId", i, nullptr)) return makeError(-32602, "clipId required"); c.clearGainEnvelope(i); return { false, QJsonValue::Null }; }
    if (m == "notifyClipGainEnvelopeChanged") { int i; if (!requireInt(o, "clipId", i, nullptr)) return makeError(-32602, "clipId required"); c.notifyClipGainEnvelopeChanged(i); return { false, QJsonValue::Null }; }

    // --- Modulation (LFO) ---
    if (m == "addLfo")         { int i; if (!requireInt(o, "trackIndex", i, nullptr)) return makeError(-32602, "trackIndex required"); c.addLfo(i); return { false, QJsonValue::Null }; }
    if (m == "removeLfo")      { int i, idx; if (!requireInt(o, "trackIndex", i, nullptr) || !requireInt(o, "lfoIndex", idx, nullptr)) return makeError(-32602, "trackIndex and lfoIndex required"); c.removeLfo(i, idx); return { false, QJsonValue::Null }; }
    if (m == "setLfoParam")    { int i, idx; std::string name; double v; if (!requireInt(o, "trackIndex", i, nullptr) || !requireInt(o, "lfoIndex", idx, nullptr) || !requireString(o, "paramName", name, nullptr) || !requireDouble(o, "value", v, nullptr)) return makeError(-32602, "trackIndex, lfoIndex, paramName, value required"); c.setLfoParam(i, idx, name, v); return { false, QJsonValue::Null }; }

    // --- Slicing ---
    if (m == "sliceClipAtTimes")     { int i; if (!requireInt(o, "clipId", i, nullptr)) return makeError(-32602, "clipId required"); DispatchResult e; auto times = toDoubleVector(o.value("times"), &e); if (e.isError) return e; c.sliceClipAtTimes(i, times); return { false, QJsonValue::Null }; }
    if (m == "sliceClipAtTransients"){ int i; if (!requireInt(o, "clipId", i, nullptr)) return makeError(-32602, "clipId required"); c.sliceClipAtTransients(i); return { false, QJsonValue::Null }; }
    if (m == "sliceClipAtPlayhead")  { int i; if (!requireInt(o, "clipId", i, nullptr)) return makeError(-32602, "clipId required"); c.sliceClipAtPlayhead(i); return { false, QJsonValue::Null }; }

    // --- Region cut/copy/paste ---
    if (m == "copyAudioClipRegion") { int i; double s, e; if (!requireInt(o, "clipId", i, nullptr) || !requireDouble(o, "regionStart", s, nullptr) || !requireDouble(o, "regionEnd", e, nullptr)) return makeError(-32602, "clipId, regionStart, regionEnd required"); return { false, c.copyAudioClipRegion(i, s, e) }; }
    if (m == "cutAudioClipRegion")  { int i; double s, e; if (!requireInt(o, "clipId", i, nullptr) || !requireDouble(o, "regionStart", s, nullptr) || !requireDouble(o, "regionEnd", e, nullptr)) return makeError(-32602, "clipId, regionStart, regionEnd required"); return { false, c.cutAudioClipRegion(i, s, e) }; }
    if (m == "pasteAudioClipRegion"){ int i; double t; if (!requireInt(o, "clipId", i, nullptr) || !requireDouble(o, "pasteTime", t, nullptr)) return makeError(-32602, "clipId and pasteTime required"); return { false, c.pasteAudioClipRegion(i, t) }; }

    // --- Undo/redo & transactions ---
    if (m == "undo")  { c.undo();  return { false, QJsonValue::Null }; }
    if (m == "redo")  { c.redo();  return { false, QJsonValue::Null }; }
    if (m == "canUndo")  { return { false, c.canUndo() }; }
    if (m == "canRedo")  { return { false, c.canRedo() }; }
    if (m == "beginTransaction") { std::string name = optString(o, "name", "edit"); c.beginTransaction(name); return { false, QJsonValue::Null }; }
    if (m == "endTransaction")   { c.endTransaction(); return { false, QJsonValue::Null }; }

    // --- Project lifecycle ---
    if (m == "newProject")  { c.newProject(); return { false, QJsonValue::Null }; }
    if (m == "saveProject") { std::string p; if (!requireString(o, "filePath", p, nullptr)) return makeError(-32602, "filePath required"); return { false, c.saveProject(p) }; }
    if (m == "loadProject") { std::string p; if (!requireString(o, "filePath", p, nullptr)) return makeError(-32602, "filePath required"); return { false, c.loadProject(p) }; }

    // --- Scale ---
    if (m == "setScaleRoot") { int r; if (!requireInt(o, "root", r, nullptr)) return makeError(-32602, "root required"); c.setScaleRoot(r); return { false, QJsonValue::Null }; }
    if (m == "setScaleMode") { int mo; if (!requireInt(o, "mode", mo, nullptr)) return makeError(-32602, "mode required"); c.setScaleMode(mo); return { false, QJsonValue::Null }; }

    // --- Missing-file relinking ---
    if (m == "findMissingClipSourceFile") { int i; std::string d; if (!requireInt(o, "clipId", i, nullptr) || !requireString(o, "searchDir", d, nullptr)) return makeError(-32602, "clipId and searchDir required"); return { false, QString::fromStdString(c.findMissingClipSourceFile(i, d)) }; }
    if (m == "relinkAllMissingFiles") {
        std::string d; if (!requireString(o, "searchDir", d, nullptr)) return makeError(-32602, "searchDir required");
        auto r = c.relinkAllMissingFiles(d);
        return { false, QJsonObject{ { "found", r.found }, { "totalMissing", r.totalMissing } } };
    }

    return makeError(-32601, "unknown project method: " + m);
}

DispatchResult dispatchTransport(TransportCommands& c, const QString& m, const QJsonValue& params) {
    const auto o = paramsObject(params);
    if (m == "play")    { c.play();    return { false, QJsonValue::Null }; }
    if (m == "stop")    { c.stop();    return { false, QJsonValue::Null }; }
    if (m == "pause")   { c.pause();   return { false, QJsonValue::Null }; }
    if (m == "rewind")  { c.rewind();  return { false, QJsonValue::Null }; }
    if (m == "toggleLoop") { c.toggleLoop(); return { false, QJsonValue::Null }; }
    if (m == "seekToSample")  { int64_t s; if (!requireInt(o, "sample", s, nullptr)) return makeError(-32602, "sample required"); c.seekToSample(s); return { false, QJsonValue::Null }; }
    if (m == "seekToSeconds") { double s; if (!requireDouble(o, "seconds", s, nullptr)) return makeError(-32602, "seconds required"); c.seekToSeconds(s); return { false, QJsonValue::Null }; }
    if (m == "startRecording") { c.startRecording(); return { false, QJsonValue::Null }; }
    if (m == "stopRecording")  { c.stopRecording();  return { false, QJsonValue::Null }; }
    if (m == "isRecording")    { return { false, c.isRecording() }; }
    return makeError(-32601, "unknown transport method: " + m);
}

DispatchResult dispatchAudioGraph(AudioGraphCommands& c, const QString& m, const QJsonValue& params) {
    const auto o = paramsObject(params);
    if (m == "rebuildRoutingGraph")  { c.rebuildRoutingGraph();  return { false, QJsonValue::Null }; }
    if (m == "rebuildTrackFX")       { int i; if (!requireInt(o, "trackIndex", i, nullptr)) return makeError(-32602, "trackIndex required"); c.rebuildTrackFX(i); return { false, QJsonValue::Null }; }
    if (m == "rebuildAutomationCache"){ int i; if (!requireInt(o, "trackIndex", i, nullptr)) return makeError(-32602, "trackIndex required"); c.rebuildAutomationCache(i); return { false, QJsonValue::Null }; }
    if (m == "rebuildModulation")    { int i; if (!requireInt(o, "trackIndex", i, nullptr)) return makeError(-32602, "trackIndex required"); c.rebuildModulation(i); return { false, QJsonValue::Null }; }
    if (m == "toggleFXEditor")       { int i, s; if (!requireInt(o, "trackIndex", i, nullptr) || !requireInt(o, "slotIndex", s, nullptr)) return makeError(-32602, "trackIndex and slotIndex required"); c.toggleFXEditor(i, s); return { false, QJsonValue::Null }; }
    if (m == "switchClipTake")       { int i; if (!requireInt(o, "clipId", i, nullptr)) return makeError(-32602, "clipId required"); c.switchClipTake(i); return { false, QJsonValue::Null }; }
    return makeError(-32601, "unknown audioGraph method: " + m);
}

DispatchResult dispatchRead(ReadModel& r, const QString& m, const QJsonValue& params) {
    const auto o = paramsObject(params);
    if (m == "snapshot")         { return { false, toJson(r.snapshot()) }; }
    if (m == "getTrackCount")    { return { false, r.getTrackCount() }; }
    if (m == "getTrack")         { int i; if (!requireInt(o, "trackIndex", i, nullptr)) return makeError(-32602, "trackIndex required"); return { false, toJson(r.getTrack(i)) }; }
    if (m == "getClip")          { int i; if (!requireInt(o, "clipId", i, nullptr)) return makeError(-32602, "clipId required"); return { false, toJson(r.getClip(i)) }; }
    if (m == "getNotes") {
        int i; if (!requireInt(o, "clipId", i, nullptr)) return makeError(-32602, "clipId required");
        QJsonArray arr; for (const auto& n : r.getNotes(i)) arr.append(toJson(n));
        return { false, arr };
    }
    if (m == "getClipGainEnvelope") {
        int i; if (!requireInt(o, "clipId", i, nullptr)) return makeError(-32602, "clipId required");
        QJsonArray arr; for (const auto& p : r.getClipGainEnvelope(i)) arr.append(toJson(p));
        return { false, arr };
    }
    if (m == "getTransport")     { return { false, toJson(r.getTransport()) }; }
    if (m == "getScaleRoot")     { return { false, r.getScaleRoot() }; }
    if (m == "getScaleMode")     { return { false, r.getScaleMode() }; }
    if (m == "getFxSlots") {
        int i; if (!requireInt(o, "trackIndex", i, nullptr)) return makeError(-32602, "trackIndex required");
        QJsonArray arr; for (const auto& f : r.getFxSlots(i)) arr.append(toJson(f));
        return { false, arr };
    }
    if (m == "getAutomationLanes") {
        int i; if (!requireInt(o, "trackIndex", i, nullptr)) return makeError(-32602, "trackIndex required");
        QJsonArray arr; for (const auto& l : r.getAutomationLanes(i)) arr.append(toJson(l));
        return { false, arr };
    }
    if (m == "getAutomationPoints") {
        int i; std::string lane; if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "laneName", lane, nullptr)) return makeError(-32602, "trackIndex and laneName required");
        QJsonArray arr; for (const auto& p : r.getAutomationPoints(i, lane)) arr.append(toJson(p));
        return { false, arr };
    }
    if (m == "getMarkers") {
        QJsonArray arr; for (const auto& mk : r.getMarkers()) arr.append(toJson(mk));
        return { false, arr };
    }
    if (m == "getTempoPoints") {
        QJsonArray arr; for (const auto& t : r.getTempoPoints()) arr.append(toJson(t));
        return { false, arr };
    }
    if (m == "getAutomatableParams") {
        int i; if (!requireInt(o, "trackIndex", i, nullptr)) return makeError(-32602, "trackIndex required");
        QJsonArray arr; for (const auto& a : r.getAutomatableParams(i)) arr.append(toJson(a));
        return { false, arr };
    }
    if (m == "getModulationLfos") {
        int i; if (!requireInt(o, "trackIndex", i, nullptr)) return makeError(-32602, "trackIndex required");
        QJsonArray arr; for (const auto& l : r.getModulationLfos(i)) arr.append(toJson(l));
        return { false, arr };
    }
    if (m == "getTrackMeter")   { int i; if (!requireInt(o, "trackIndex", i, nullptr)) return makeError(-32602, "trackIndex required"); return { false, toJson(r.getTrackMeter(i)) }; }
    if (m == "getMasterMeter")  { return { false, toJson(r.getMasterMeter()) }; }
    if (m == "isDirty")         { return { false, r.isDirty() }; }
    return makeError(-32601, "unknown read method: " + m);
}

DispatchResult dispatchPlugin(PluginService& s, const QString& m, const QJsonValue& params) {
    const auto o = paramsObject(params);
    auto pluginInfoToJson = [](const PluginInfo& p) {
        return QJsonObject{
            { "name",            QString::fromStdString(p.name) },
            { "format",          QString::fromStdString(p.format) },
            { "manufacturer",    QString::fromStdString(p.manufacturer) },
            { "fileOrIdentifier", QString::fromStdString(p.fileOrIdentifier) },
            { "isInstrument",    p.isInstrument },
        };
    };
    if (m == "scanAll")  { s.scanAll(); return { false, QJsonValue::Null }; }
    if (m == "isLoading") { return { false, s.isLoading() }; }
    if (m == "getPlugins") {
        QJsonArray arr; for (const auto& p : s.getPlugins()) arr.append(pluginInfoToJson(p));
        return { false, arr };
    }
    if (m == "getInstrumentPlugins") {
        QJsonArray arr; for (const auto& p : s.getInstrumentPlugins()) arr.append(pluginInfoToJson(p));
        return { false, arr };
    }
    if (m == "getEffectPlugins") {
        QJsonArray arr; for (const auto& p : s.getEffectPlugins()) arr.append(pluginInfoToJson(p));
        return { false, arr };
    }
    if (m == "isBlacklisted")      { std::string id; if (!requireString(o, "pluginID", id, nullptr)) return makeError(-32602, "pluginID required"); return { false, s.isBlacklisted(id) }; }
    if (m == "blacklistPlugin")    { std::string id; if (!requireString(o, "pluginID", id, nullptr)) return makeError(-32602, "pluginID required"); s.blacklistPlugin(id); return { false, QJsonValue::Null }; }
    if (m == "unblacklistPlugin")  { std::string id; if (!requireString(o, "pluginID", id, nullptr)) return makeError(-32602, "pluginID required"); s.unblacklistPlugin(id); return { false, QJsonValue::Null }; }
    if (m == "getBlacklistReason") { std::string id; if (!requireString(o, "pluginID", id, nullptr)) return makeError(-32602, "pluginID required"); return { false, QString::fromStdString(s.getBlacklistReason(id)) }; }
    return makeError(-32601, "unknown plugin method: " + m);
}

DispatchResult dispatchPluginParam(PluginParamService& s, const QString& m, const QJsonValue& params) {
    const auto o = paramsObject(params);
    if (m == "getParams") {
        int i; std::string id; if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "pluginID", id, nullptr)) return makeError(-32602, "trackIndex and pluginID required");
        QJsonArray arr;
        for (const auto& p : s.getParams(i, id)) {
            arr.append(QJsonObject{
                { "index",       p.index },
                { "name",        QString::fromStdString(p.name) },
                { "value",       static_cast<double>(p.value) },
                { "text",        QString::fromStdString(p.text) },
                { "label",       QString::fromStdString(p.label) },
                { "automatable", p.automatable },
            });
        }
        return { false, arr };
    }
    if (m == "getParamText") {
        int i, pi; std::string id; float v;
        if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "pluginID", id, nullptr) || !requireInt(o, "paramIndex", pi, nullptr) || !requireFloat(o, "normalizedValue", v, nullptr))
            return makeError(-32602, "trackIndex, pluginID, paramIndex, normalizedValue required");
        return { false, QString::fromStdString(s.getParamText(i, id, pi, v)) };
    }
    if (m == "setParam") {
        int i, pi; std::string id; float v;
        if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "pluginID", id, nullptr) || !requireInt(o, "paramIndex", pi, nullptr) || !requireFloat(o, "normalizedValue", v, nullptr))
            return makeError(-32602, "trackIndex, pluginID, paramIndex, normalizedValue required");
        s.setParam(i, id, pi, v); return { false, QJsonValue::Null };
    }
    if (m == "getProgramCount")  { int i; std::string id; if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "pluginID", id, nullptr)) return makeError(-32602, "trackIndex and pluginID required"); return { false, s.getProgramCount(i, id) }; }
    if (m == "getCurrentProgram"){ int i; std::string id; if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "pluginID", id, nullptr)) return makeError(-32602, "trackIndex and pluginID required"); return { false, s.getCurrentProgram(i, id) }; }
    if (m == "getProgramName")   { int i, pi; std::string id; if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "pluginID", id, nullptr) || !requireInt(o, "programIndex", pi, nullptr)) return makeError(-32602, "trackIndex, pluginID, programIndex required"); return { false, QString::fromStdString(s.getProgramName(i, id, pi)) }; }
    if (m == "setCurrentProgram"){ int i, pi; std::string id; if (!requireInt(o, "trackIndex", i, nullptr) || !requireString(o, "pluginID", id, nullptr) || !requireInt(o, "programIndex", pi, nullptr)) return makeError(-32602, "trackIndex, pluginID, programIndex required"); s.setCurrentProgram(i, id, pi); return { false, QJsonValue::Null }; }
    return makeError(-32601, "unknown pluginParam method: " + m);
}

DispatchResult dispatchMidi(MidiService& s, const QString& m, const QJsonValue& params) {
    const auto o = paramsObject(params);
    if (m == "getAvailableDevices") {
        QJsonArray arr; for (const auto& d : s.getAvailableDevices()) arr.append(QString::fromStdString(d));
        return { false, arr };
    }
    if (m == "openDevice")  { std::string id; if (!requireString(o, "identifier", id, nullptr)) return makeError(-32602, "identifier required"); return { false, s.openDevice(id) }; }
    if (m == "closeDevice") { s.closeDevice(); return { false, QJsonValue::Null }; }
    return makeError(-32601, "unknown midi method: " + m);
}

} // namespace (anonymous)

// ---- Public entry point ----------------------------------------------------

DispatchResult dispatch(AudioEngine& engine, const QString& method, const QJsonValue& params) {
    const int dot = method.indexOf('.');
    if (dot < 0) return makeError(-32601, "method must be 'namespace.method': " + method);
    const QString ns = method.left(dot);
    const QString m  = method.mid(dot + 1);

    if      (ns == method::Project)     return dispatchProject(engine.getProjectCommands(), m, params);
    else if (ns == method::Transport)   return dispatchTransport(engine.getTransportCommands(), m, params);
    else if (ns == method::AudioGraph)  return dispatchAudioGraph(engine.getAudioGraphCommands(), m, params);
    else if (ns == method::Read)        return dispatchRead(engine.getReadModel(), m, params);
    else if (ns == method::Plugin)      return dispatchPlugin(engine.getPluginService(), m, params);
    else if (ns == method::PluginParam) return dispatchPluginParam(engine.getPluginParamService(), m, params);
    else if (ns == method::Midi)        return dispatchMidi(engine.getMidiService(), m, params);

    return makeError(-32601, "unknown method namespace: " + ns);
}

} // namespace frontend
