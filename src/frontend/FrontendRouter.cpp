#include "FrontendRouter.h"
#include "FrontendServer.h"

#include "../engine/AudioEngine.h"
#include "../engine/ExportManager.h"
#include "../engine/ProjectPool.h"
#include "../engine/MainAudioProcessor.h"
#include "../engine/PluginManager.h"
#include "../common/ProjectCommands.h"
#include "../common/TransportCommands.h"
#include "../common/AudioGraphCommands.h"
#include "../common/ReadModel.h"
#include "../common/PluginService.h"
#include "../common/PluginParamService.h"
#include "../common/MidiService.h"
#include "../common/SettingsKeys.h"
#include "../engine/PhraseGenerator.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QString>
#include <QStringList>

#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <thread>

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
    if (m == "moveClipWithOverlap") { int i, t; double s; if (!requireInt(o, "clipId", i, nullptr) || !requireInt(o, "newTrackIndex", t, nullptr) || !requireDouble(o, "newStart", s, nullptr)) return makeError(-32602, "clipId, newTrackIndex, newStart required"); c.moveClipWithOverlap(i, t, s); return { false, QJsonValue::Null }; }
    if (m == "duplicateClip")   { int i; if (!requireInt(o, "clipId", i, nullptr)) return makeError(-32602, "clipId required"); return { false, c.duplicateClip(i) }; }
    if (m == "createGhostClip") { int i, t; double s; if (!requireInt(o, "sourceClipId", i, nullptr) || !requireDouble(o, "newStart", s, nullptr) || !requireInt(o, "newTrackIndex", t, nullptr)) return makeError(-32602, "sourceClipId, newStart, newTrackIndex required"); return { false, c.createGhostClip(i, s, t) }; }
    if (m == "paintClips") {
        auto srcArr = o.value("sourceClipIds");
        if (!srcArr.isArray()) return makeError(-32602, "sourceClipIds array required");
        std::vector<int> srcIds;
        for (const auto& e : srcArr.toArray()) {
            if (!e.isDouble()) return makeError(-32602, "sourceClipIds element is not a number");
            srcIds.push_back(static_cast<int>(e.toDouble()));
        }
        double origin; int target, count;
        double spacing;
        if (!requireDouble(o, "originBeat", origin, nullptr) || !requireDouble(o, "spacing", spacing, nullptr) || !requireInt(o, "targetTrackIndex", target, nullptr) || !requireInt(o, "count", count, nullptr))
            return makeError(-32602, "originBeat, spacing, targetTrackIndex, count required");
        auto ids = c.paintClips(srcIds, origin, spacing, target, count);
        QJsonArray arr;
        for (int id : ids) arr.append(id);
        return { false, arr };
    }
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
        if (!requireInt(o, "trackIndex", i, nullptr))
            return makeError(-32602, "trackIndex required");
        // Accept either `type` (canonical) or `fxType` (frontend spelling) for
        // the FX-type string. Accept either `position` (canonical) or
        // `slotIndex` (frontend spelling) for the insertion index; default
        // -1 = append. Both spellings are tolerated silently.
        if (o.contains("type") && o.value("type").isString())
            type = o.value("type").toString().toStdString();
        else if (o.contains("fxType") && o.value("fxType").isString())
            type = o.value("fxType").toString().toStdString();
        else
            return makeError(-32602, "type (or fxType) required");
        if (o.contains("position") && o.value("position").isDouble())
            pos = static_cast<int>(o.value("position").toDouble());
        else if (o.contains("slotIndex") && o.value("slotIndex").isDouble())
            pos = static_cast<int>(o.value("slotIndex").toDouble());
        else
            pos = -1;
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
    if (m == "setClipGainEnvelope") {
        int i; if (!requireInt(o, "clipId", i, nullptr)) return makeError(-32602, "clipId required");
        DispatchResult e;
        auto pointPairs = [](const QJsonValue& v, DispatchResult* err) -> std::vector<std::pair<double, double>> {
            std::vector<std::pair<double, double>> out;
            if (!v.isArray()) { if (err) *err = makeError(-32602, "points must be an array"); return out; }
            for (const auto& el : v.toArray()) {
                if (!el.isObject()) { if (err) *err = makeError(-32602, "each point must be an object {time, gain}"); return {}; }
                auto obj = el.toObject();
                if (!obj.contains("time") || !obj.value("time").isDouble() ||
                    !obj.contains("gain") || !obj.value("gain").isDouble()) {
                    if (err) *err = makeError(-32602, "each point must have numeric time and gain");
                    return {};
                }
                out.emplace_back(obj.value("time").toDouble(), obj.value("gain").toDouble());
            }
            return out;
        }(o.value("points"), &e);
        if (e.isError) return e;
        c.setClipGainEnvelope(i, pointPairs);
        return { false, QJsonValue::Null };
    }
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
    // transport.record: alias matching the UI semantics (TransportBar's ●
    // button toggles; "R" shortcut triggers). Toggles start/stop based on
    // current state.
    if (m == "record") { if (c.isRecording()) c.stopRecording(); else c.startRecording(); return { false, QJsonValue::Null }; }
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
    if (m == "getCcPoints") {
        int i, cc; if (!requireInt(o, "clipId", i, nullptr) || !requireInt(o, "controllerNumber", cc, nullptr)) return makeError(-32602, "clipId and controllerNumber required");
        QJsonArray arr; for (const auto& p : r.getCcPoints(i, cc)) arr.append(toJson(p));
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
    if (m == "getInternalFxParams") {
        int ti, si; if (!requireInt(o, "trackIndex", ti, nullptr) || !requireInt(o, "slotIndex", si, nullptr)) return makeError(-32602, "trackIndex and slotIndex required");
        QJsonArray arr; for (const auto& p : r.getInternalFxParams(ti, si)) arr.append(toJson(p));
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

DispatchResult dispatchPlugin(PluginService& s, const QString& m, const QJsonValue& params,
                              FrontendServer* server) {
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
    if (m == "scanAll") {
        if (s.isLoading())
            return makeError(-32603, "scan already in progress");

        // Run the scan on a background thread so the RPC doesn't block the
        // engine main thread. Broadcast notify.scanProgress for each plugin
        // file, then broadcast a completion notification when done.
        // Spin a local QEventLoop so queued cross-thread invocations
        // (the broadcastNotificationFromAnyThread hops) are processed.
        QEventLoop loop;
        bool scanDone = false;
        std::thread scanThread([&]() {
            s.scanAll([&](const std::string& fileName, int completed, int total) {
                if (server == nullptr) return;
                QJsonObject payload{
                    { "fileName", QString::fromStdString(fileName) },
                    { "completed", completed },
                    { "total", total },
                };
                server->broadcastNotificationFromAnyThread(notify::ScanProgress, payload);
            });
            scanDone = true;
            QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
        });
        scanThread.detach();

        loop.exec();

        // Broadcast completion
        if (server != nullptr) {
            server->broadcastNotificationFromAnyThread(notify::ScanProgress,
                QJsonObject{ { "fileName", "" }, { "completed", -1 }, { "total", -1 },
                             { "done", true } });
        }
        return { false, QJsonValue::Null };
    }
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
                // Field name is `paramIndex` (not `index`) for consistency
                // with the write side (pluginParam.setParam's paramIndex),
                // the AutomatableParamSnapshot shape, and the frontend's
                // ParamInfo/TS interface.
                { "paramIndex",  p.index },
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

DispatchResult dispatchAudio(AudioEngine& engine, const QString& m, const QJsonValue& params) {
    auto& dm = engine.getDeviceManager();
    const auto o = paramsObject(params);

    if (m == "getDeviceTypes") {
        QJsonArray arr;
        for (auto* type : dm.getAvailableDeviceTypes())
            arr.append(QString::fromUtf8(type->getTypeName().toRawUTF8()));
        return { false, arr };
    }

    if (m == "getOutputDevices") {
        QJsonArray arr;
        auto* devType = dm.getCurrentDeviceTypeObject();
        if (devType != nullptr)
            for (const auto& name : devType->getDeviceNames(false))
                arr.append(QString::fromUtf8(name.toRawUTF8()));
        return { false, arr };
    }

    if (m == "getInputDevices") {
        QJsonArray arr;
        auto* devType = dm.getCurrentDeviceTypeObject();
        if (devType != nullptr)
            for (const auto& name : devType->getDeviceNames(true))
                arr.append(QString::fromUtf8(name.toRawUTF8()));
        return { false, arr };
    }

    if (m == "getCurrentSetup") {
        auto setup = dm.getAudioDeviceSetup();
        auto* dev = dm.getCurrentAudioDevice();
        double sr = setup.sampleRate;
        int bs = setup.bufferSize;
        double latencyMs = 0.0;
        if (dev != nullptr) {
            sr = dev->getCurrentSampleRate();
            bs = dev->getCurrentBufferSizeSamples();
            if (sr > 0.0) latencyMs = static_cast<double>(bs) / sr * 1000.0;
        }
        return { false, QJsonObject{
            { "driver",      QString::fromUtf8(dm.getCurrentAudioDeviceType().toRawUTF8()) },
            { "output",      QString::fromUtf8(setup.outputDeviceName.toRawUTF8()) },
            { "input",       QString::fromUtf8(setup.inputDeviceName.toRawUTF8()) },
            { "sampleRate",  sr },
            { "bufferSize",  bs },
            { "latencyMs",   latencyMs },
        }};
    }

    if (m == "getSampleRates") {
        QJsonArray arr;
        auto* dev = dm.getCurrentAudioDevice();
        if (dev != nullptr)
            for (double rate : dev->getAvailableSampleRates())
                arr.append(rate);
        return { false, arr };
    }

    if (m == "getBufferSizes") {
        QJsonArray arr;
        auto* dev = dm.getCurrentAudioDevice();
        if (dev != nullptr)
            for (int buf : dev->getAvailableBufferSizes())
                arr.append(buf);
        return { false, arr };
    }

    if (m == "setDeviceType") {
        std::string type;
        if (!requireString(o, "type", type, nullptr))
            return makeError(-32602, "type required");
        dm.setCurrentAudioDeviceType(juce::String(type), true);
        QSettings s;
        s.setValue(SettingsKeys::kKeyAudioDriver, QString::fromStdString(type));
        return { false, QJsonValue::Null };
    }

    if (m == "setOutputDevice") {
        std::string name;
        if (!requireString(o, "name", name, nullptr))
            return makeError(-32602, "name required");
        auto setup = dm.getAudioDeviceSetup();
        setup.outputDeviceName = juce::String(name);
        dm.setAudioDeviceSetup(setup, true);
        QSettings s;
        s.setValue(SettingsKeys::kKeyAudioOutputDevice, QString::fromStdString(name));
        return { false, QJsonValue::Null };
    }

    if (m == "setInputDevice") {
        std::string name;
        if (!requireString(o, "name", name, nullptr))
            return makeError(-32602, "name required");
        auto setup = dm.getAudioDeviceSetup();
        setup.inputDeviceName = juce::String(name);
        dm.setAudioDeviceSetup(setup, true);
        QSettings s;
        s.setValue(SettingsKeys::kKeyAudioInputDevice, QString::fromStdString(name));
        return { false, QJsonValue::Null };
    }

    if (m == "setSampleRate") {
        double rate;
        if (!requireDouble(o, "rate", rate, nullptr))
            return makeError(-32602, "rate required");
        auto setup = dm.getAudioDeviceSetup();
        setup.sampleRate = rate;
        dm.setAudioDeviceSetup(setup, true);
        QSettings s;
        s.setValue(SettingsKeys::kKeyAudioSampleRate, static_cast<qint64>(rate));
        return { false, QJsonValue::Null };
    }

    if (m == "setBufferSize") {
        int size;
        if (!requireInt(o, "size", size, nullptr))
            return makeError(-32602, "size required");
        auto setup = dm.getAudioDeviceSetup();
        setup.bufferSize = size;
        dm.setAudioDeviceSetup(setup, true);
        QSettings s;
        s.setValue(SettingsKeys::kKeyAudioBufferSize, size);
        return { false, QJsonValue::Null };
    }

    return makeError(-32601, "unknown audio method: " + m);
}

// Render the project to an audio file. The ExportManager runs the render on
// its own worker thread; this handler spins a local QEventLoop (processing
// queued cross-thread invocations) until onComplete fires, broadcasting
// notify.exportProgress along the way. Mirrors the MCP export_audio tool
// (src/mcp/McpExportTool.cpp) but uses the frontend's own
// notify.exportProgress channel (not notifications/progress).
DispatchResult dispatchExport(AudioEngine& engine, const QString& m,
                              const QJsonValue& params, FrontendServer* server) {
    const auto o = paramsObject(params);

    if (m == "audio") {
        std::string pathStr;
        if (!requireString(o, "outputPath", pathStr, nullptr))
            return makeError(-32602, "outputPath required");
        QString path = QString::fromStdString(pathStr);
        if (path.isEmpty())
            return makeError(-32602, "outputPath required");

        QString formatStr = optString(o, "format", "wav").c_str();
        formatStr = formatStr.toLower();
        HDAW::ExportManager::Format fmt = HDAW::ExportManager::WAV;
        if      (formatStr == "aiff") fmt = HDAW::ExportManager::AIFF;
        else if (formatStr == "flac") fmt = HDAW::ExportManager::FLAC;

        double sampleRate = optDouble(o, "sampleRate", 48000.0, nullptr);
        int    bitDepth   = optInt(o, "bitDepth", 24, nullptr);
        double startTime  = optDouble(o, "start", 0.0, nullptr);
        double endTime    = optDouble(o, "end", -1.0, nullptr);
        if (endTime <= 0.0)
            endTime = HDAW::ExportManager::calculateProjectDuration(engine.getProjectModel());
        double duration = std::max(0.001, endTime - startTime);

        auto* mainProc = engine.getMainProcessor();
        if (mainProc == nullptr)
            return makeError(-32603, "audio engine not initialized");
        auto& em = mainProc->getExportManager();
        if (em.isExporting())
            return makeError(-32603, "export already in progress");

        juce::File outFile(juce::String(path.toUtf8().constData()));
        if (outFile.existsAsFile()) outFile.deleteFile();

        juce::ValueTree projectCopy = engine.getProjectModel().getTree().createCopy();
        auto& formatManager = engine.getProjectPool().getFormatManager();
        auto* pluginManager = &engine.getPluginManager();

        // Progress callback runs on the export worker thread; hop to the main
        // thread before broadcasting so we never touch clients_ off-thread.
        if (server != nullptr) {
            FrontendServer* serverPtr = server;
            em.onProgress = [serverPtr, &em](float prog) {
                QJsonObject payload{
                    { "progress", static_cast<double>(prog) },
                    { "message", QString("rendering... %1%").arg(static_cast<int>(prog * 100.0)) },
                };
                serverPtr->broadcastNotificationFromAnyThread(notify::ExportProgress, payload);
            };
        }

        // The export worker runs on its own thread and hops back here via
        // QMetaObject::invokeMethod(..., QueuedConnection) for progress and
        // completion. Blocking on doneFuture.get() would stall the Qt event
        // loop, which (a) prevents the progress hops from firing until after
        // the export finishes, defeating the live progress notifications,
        // and (b) prevents aboutToQuit from firing, so a Ctrl-C during export
        // hangs the process. Spin a local event loop instead so queued
        // invocations are processed; quit when onComplete fires.
        QEventLoop loop;
        bool success = false;
        QString message;
        em.onComplete = [&](bool ok, const juce::String& msg) {
            success = ok;
            message = QString::fromUtf8(msg.toRawUTF8());
            QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
        };

        if (!em.startExport(projectCopy, formatManager, pluginManager, outFile,
                            sampleRate, startTime, duration, fmt, bitDepth)) {
            em.onProgress = nullptr;
            em.onComplete = nullptr;
            return makeError(-32603, "failed to start export");
        }

        if (server != nullptr) {
            server->broadcastNotificationFromAnyThread(notify::ExportProgress,
                QJsonObject{ { "progress", 0.0 }, { "message", "starting render" } });
        }

        // Process events until the worker's onComplete hops back here and
        // quits the loop. This keeps progress notifications streaming live
        // and lets aboutToQuit fire if the app is asked to exit mid-export.
        // This dispatch call is still the only one in flight — every other
        // WebSocket request is queued behind it — but the event loop now
        // turns over, so the UI and other Qt timers keep working.
        loop.exec();

        if (server != nullptr) {
            server->broadcastNotificationFromAnyThread(notify::ExportProgress,
                QJsonObject{ { "progress", success ? 1.0 : 0.0 }, { "message", message } });
        }

        em.onProgress = nullptr;
        em.onComplete = nullptr;

        if (!success)
            return makeError(-32603, QString("export failed: %1").arg(message));

        return { false, QJsonObject{
            { "outputPath", path },
            { "message", message },
        } };
    }

    if (m == "isExporting") {
        auto* mainProc = engine.getMainProcessor();
        bool exporting = (mainProc != nullptr) && mainProc->getExportManager().isExporting();
        return { false, exporting };
    }
    if (m == "cancel") {
        auto* mainProc = engine.getMainProcessor();
        if (mainProc != nullptr && mainProc->getExportManager().isExporting())
            mainProc->getExportManager().cancel();
        return { false, QJsonValue::Null };
    }

    return makeError(-32601, "unknown export method: " + m);
}

DispatchResult dispatchPreview(AudioEngine& engine, const QString& m, const QJsonValue& params) {
    const auto o = paramsObject(params);
    auto& preview = engine.getPreviewPlayer();

    if (m == "load") {
        std::string path;
        if (!requireString(o, "filePath", path, nullptr))
            return makeError(-32602, "filePath required");
        preview.loadFile(juce::File(juce::String(path)));
        return { false, QJsonValue::Null };
    }
    if (m == "play") {
        preview.play();
        return { false, QJsonValue::Null };
    }
    if (m == "stop") {
        preview.stop();
        return { false, QJsonValue::Null };
    }
    if (m == "setVolume") {
        float vol;
        if (!requireFloat(o, "volume", vol, nullptr))
            return makeError(-32602, "volume required");
        preview.setVolume(vol);
        return { false, QJsonValue::Null };
    }
    if (m == "setTempoMatch") {
        bool enabled;
        double fileBpm = optDouble(o, "fileBpm", 0.0, nullptr);
        if (!requireBool(o, "enabled", enabled, nullptr))
            return makeError(-32602, "enabled required");
        preview.setTempoMatch(enabled, fileBpm);
        return { false, QJsonValue::Null };
    }
    if (m == "setProjectBpm") {
        double bpm;
        if (!requireDouble(o, "bpm", bpm, nullptr))
            return makeError(-32602, "bpm required");
        preview.setProjectBpm(bpm);
        return { false, QJsonValue::Null };
    }
    if (m == "isPlaying") {
        return { false, preview.isPlaying() };
    }
    return makeError(-32601, "unknown preview method: " + m);
}

DispatchResult dispatchComposition(AudioEngine& engine, const QString& m, const QJsonValue& params) {
    const auto o = paramsObject(params);
    auto& c = engine.getProjectCommands();
    auto& ag = engine.getAudioGraphCommands();

    // --- Read-only queries (PhraseGenerator is a static utility) ---

    if (m == "getScaleModes") {
        QJsonArray arr;
        for (const auto& sm : PhraseGenerator::getScaleModes()) {
            QJsonArray intervals;
            for (int iv : sm.intervals) intervals.append(iv);
            arr.append(QJsonObject{ { "index", sm.index }, { "name", sm.name }, { "intervals", intervals } });
        }
        return { false, arr };
    }
    if (m == "getChordTypes") {
        QJsonArray arr;
        for (const auto& ct : PhraseGenerator::getChordTypes()) {
            QJsonArray intervals;
            for (int iv : ct.intervals) intervals.append(iv);
            arr.append(QJsonObject{ { "index", ct.index }, { "name", ct.name }, { "intervals", intervals } });
        }
        return { false, arr };
    }
    if (m == "getProgressionPatterns") {
        QJsonArray arr;
        for (const auto& pp : PhraseGenerator::getProgressionPatterns()) {
            QJsonArray chords;
            for (const auto& [degree, chordType] : pp.chords) {
                chords.append(QJsonObject{ { "degree", degree }, { "chordType", chordType } });
            }
            arr.append(QJsonObject{ { "index", pp.index }, { "name", pp.name }, { "chords", chords } });
        }
        return { false, arr };
    }
    if (m == "getStyleNames") {
        QJsonArray arr;
        for (int i = 0; i <= static_cast<int>(PhraseGenerator::Buildup); ++i) {
            arr.append(QJsonObject{ { "index", i }, { "name", PhraseGenerator::styleName(static_cast<PhraseGenerator::Style>(i)) } });
        }
        return { false, arr };
    }
    if (m == "getNoteName") {
        int pitch;
        if (!requireInt(o, "pitch", pitch, nullptr))
            return makeError(-32602, "pitch required");
        return { false, QString::fromUtf8(PhraseGenerator::noteName(pitch)) };
    }

    // --- Mutations: generate + insert MIDI clip ---

    // Shared lambda: generate notes, create clip, add notes, return { clipId, noteCount }
    auto generateIntoClip = [&](int trackIndex, double startBeat, double clipDuration,
                                const std::string& clipName,
                                const std::vector<PhraseGenerator::GeneratedNote>& notes) -> DispatchResult {
        int clipId = c.addMidiClip(trackIndex, startBeat, clipDuration, clipName);
        for (const auto& n : notes) {
            c.addNote(clipId, n.noteNumber, n.velocity, n.startBeat, n.durationBeats);
        }
        ag.rebuildRoutingGraph();
        QJsonObject result{ { "clipId", clipId }, { "noteCount", static_cast<int>(notes.size()) } };
        return { false, result };
    };

    if (m == "generatePhrase") {
        int trackIndex;
        std::string styleStr;
        if (!requireInt(o, "trackIndex", trackIndex, nullptr))
            return makeError(-32602, "trackIndex required");
        if (!requireString(o, "style", styleStr, nullptr))
            return makeError(-32602, "style required");

        PhraseGenerator::Style style = PhraseGenerator::Standard;
        if      (styleStr == "Arpeggio")   style = PhraseGenerator::Arpeggio;
        else if (styleStr == "BassLine")   style = PhraseGenerator::BassLine;
        else if (styleStr == "ChordStab")  style = PhraseGenerator::ChordStab;
        else if (styleStr == "Pad")        style = PhraseGenerator::Pad;
        else if (styleStr == "Lead")       style = PhraseGenerator::Lead;
        else if (styleStr == "RandomWalk") style = PhraseGenerator::RandomWalk;
        else if (styleStr == "Buildup")    style = PhraseGenerator::Buildup;

        PhraseGenerator::PhraseParams pp;
        pp.style = style;
        pp.lengthBeats = optDouble(o, "lengthBeats", 4.0, nullptr);
        pp.density = optInt(o, "density", 8, nullptr);
        pp.noteDuration = optDouble(o, "noteDuration", 0.5, nullptr);
        pp.scaleRoot = optInt(o, "scaleRoot", 0, nullptr);
        pp.scaleMode = optInt(o, "scaleMode", 0, nullptr);
        pp.lowNote = optInt(o, "lowNote", 48, nullptr);
        pp.highNote = optInt(o, "highNote", 84, nullptr);
        pp.minVelocity = optInt(o, "minVelocity", 60, nullptr);
        pp.maxVelocity = optInt(o, "maxVelocity", 110, nullptr);

        double startBeat = optDouble(o, "startBeat", 0.0, nullptr);
        auto notes = PhraseGenerator::generatePhrase(pp);
        std::string name = std::string("Phrase: ") + styleStr;
        return generateIntoClip(trackIndex, startBeat, pp.lengthBeats, name, notes);
    }

    if (m == "generateChord") {
        int trackIndex, rootPitch, chordType;
        if (!requireInt(o, "trackIndex", trackIndex, nullptr))
            return makeError(-32602, "trackIndex required");
        if (!requireInt(o, "rootPitch", rootPitch, nullptr))
            return makeError(-32602, "rootPitch required");
        if (!requireInt(o, "chordType", chordType, nullptr))
            return makeError(-32602, "chordType required");

        PhraseGenerator::ChordParams cp;
        cp.chordType = chordType;
        cp.voicing = optInt(o, "voicing", 0, nullptr);
        cp.inversion = optInt(o, "inversion", 0, nullptr);
        cp.arpeggiate = optBool(o, "arpeggiate", false, nullptr);
        cp.arpeggioRate = optDouble(o, "arpeggioRate", 0.125, nullptr);
        cp.durationBeats = optDouble(o, "durationBeats", 2.0, nullptr);
        cp.scaleRoot = optInt(o, "scaleRoot", 0, nullptr);
        cp.scaleMode = optInt(o, "scaleMode", 0, nullptr);
        cp.lowNote = optInt(o, "lowNote", 48, nullptr);
        cp.highNote = optInt(o, "highNote", 84, nullptr);
        cp.minVelocity = optInt(o, "minVelocity", 60, nullptr);
        cp.maxVelocity = optInt(o, "maxVelocity", 110, nullptr);

        double startBeat = optDouble(o, "startBeat", 0.0, nullptr);
        auto notes = PhraseGenerator::generateChord(rootPitch, cp);
        const char* ctName = PhraseGenerator::chordTypeName(chordType);
        std::string name = std::string("Chord: ") + ctName;
        return generateIntoClip(trackIndex, startBeat, cp.durationBeats, name, notes);
    }

    if (m == "generateProgression") {
        int trackIndex, patternIndex;
        if (!requireInt(o, "trackIndex", trackIndex, nullptr))
            return makeError(-32602, "trackIndex required");
        if (!requireInt(o, "patternIndex", patternIndex, nullptr))
            return makeError(-32602, "patternIndex required");

        PhraseGenerator::ProgressionParams prp;
        prp.patternIndex = patternIndex;
        prp.chordTypeOverride = optInt(o, "chordTypeOverride", -1, nullptr);
        prp.arpeggiate = optBool(o, "arpeggiate", false, nullptr);
        prp.arpeggioRate = optDouble(o, "arpeggioRate", 0.125, nullptr);
        prp.durationBeats = optDouble(o, "durationBeats", 2.0, nullptr);
        prp.beatsPerChord = optDouble(o, "beatsPerChord", 4.0, nullptr);
        prp.scaleRoot = optInt(o, "scaleRoot", 0, nullptr);
        prp.scaleMode = optInt(o, "scaleMode", 0, nullptr);
        prp.lowNote = optInt(o, "lowNote", 48, nullptr);
        prp.highNote = optInt(o, "highNote", 84, nullptr);
        prp.minVelocity = optInt(o, "minVelocity", 60, nullptr);
        prp.maxVelocity = optInt(o, "maxVelocity", 110, nullptr);

        const auto& patterns = PhraseGenerator::getProgressionPatterns();
        if (patternIndex < 0 || patternIndex >= static_cast<int>(patterns.size()))
            return makeError(-32602, "patternIndex out of range");

        double startBeat = optDouble(o, "startBeat", 0.0, nullptr);
        auto notes = PhraseGenerator::generateProgression(prp);
        double clipDuration = prp.beatsPerChord * static_cast<double>(patterns[patternIndex].chords.size());
        std::string name = std::string("Progression: ") + patterns[patternIndex].name;
        return generateIntoClip(trackIndex, startBeat, clipDuration, name, notes);
    }

    return makeError(-32601, "unknown composition method: " + m);
}

} // namespace (anonymous)

// ---- Public entry point ----------------------------------------------------

DispatchResult dispatch(AudioEngine& engine, const QString& method, const QJsonValue& params,
                        FrontendServer* server) {
    const int dot = method.indexOf('.');
    if (dot < 0) return makeError(-32601, "method must be 'namespace.method': " + method);
    const QString ns = method.left(dot);
    const QString m  = method.mid(dot + 1);

    if      (ns == method::Project)     return dispatchProject(engine.getProjectCommands(), m, params);
    else if (ns == method::Transport)   return dispatchTransport(engine.getTransportCommands(), m, params);
    else if (ns == method::AudioGraph)  return dispatchAudioGraph(engine.getAudioGraphCommands(), m, params);
    else if (ns == method::Read) {
        // getWaveformPeaks needs AudioEngine (for ProjectPool), not just ReadModel
        if (m == "getWaveformPeaks") {
            const auto o = paramsObject(params);
            int clipId = 0;
            if (!requireInt(o, "clipId", clipId, nullptr))
                return makeError(-32602, "clipId required");

            // Find the clip in the project model
            auto& pm = engine.getProjectModel();
            auto tl = pm.getTrackListTree();
            juce::ValueTree clip;
            for (int i = 0; i < tl.getNumChildren(); ++i) {
                auto cl = tl.getChild(i).getChildWithName(IDs::CLIP_LIST);
                for (int j = 0; j < cl.getNumChildren(); ++j) {
                    if (static_cast<int>(cl.getChild(j).getProperty(IDs::clipID)) == clipId) {
                        clip = cl.getChild(j);
                        break;
                    }
                }
                if (clip.isValid()) break;
            }
            if (!clip.isValid())
                return makeError(-32602, "clip not found");

            if (clip.getProperty(IDs::clipType).toString() != juce::String("audio"))
                return makeError(-32602, "not an audio clip");

            auto sourceFile = clip.getProperty(IDs::sourceFile).toString();
            if (sourceFile.isEmpty())
                return makeError(-32602, "no source file");

            auto file = juce::File(sourceFile);
            if (!file.existsAsFile())
                return makeError(-32602, "source file missing");

            auto& fmtMgr = engine.getProjectPool().getFormatManager();
            std::unique_ptr<juce::AudioFormatReader> reader(fmtMgr.createReaderFor(file));
            if (!reader)
                return makeError(-32602, "cannot open audio file");

            auto totalSamples = reader->lengthInSamples;
            if (totalSamples <= 0)
                return makeError(-32602, "empty audio");

            int numChannels = static_cast<int>(reader->numChannels);
            double sampleRate = reader->sampleRate;
            int numBins = optInt(o, "numBins", 1000, nullptr);
            numBins = std::clamp(numBins, 100, 10000);
            int64_t samplesPerBin = totalSamples / static_cast<int64_t>(numBins);
            if (samplesPerBin < 1) samplesPerBin = 1;

            juce::AudioBuffer<float> buffer(numChannels, static_cast<int>(samplesPerBin));
            QJsonArray peaks;

            for (int i = 0; i < numBins; ++i) {
                int64_t startSample = static_cast<int64_t>(i) * samplesPerBin;
                int numToRead = static_cast<int>(
                    (std::min)(samplesPerBin, totalSamples - startSample));
                if (numToRead <= 0) {
                    peaks.append(0.0);
                    peaks.append(0.0);
                    continue;
                }
                buffer.clear();
                reader->read(&buffer, 0, numToRead, startSample, true, true);

                float minVal = 0.0f, maxVal = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch) {
                    auto* data = buffer.getReadPointer(ch);
                    for (int s = 0; s < numToRead; ++s) {
                        if (data[s] < minVal) minVal = data[s];
                        if (data[s] > maxVal) maxVal = data[s];
                    }
                }
                peaks.append(static_cast<double>(minVal));
                peaks.append(static_cast<double>(maxVal));
            }

            QJsonObject result{{"peaks", peaks},
                               {"sampleRate", sampleRate},
                               {"numSamples", static_cast<qint64>(totalSamples)}};
            return { false, result };
        }
        return dispatchRead(engine.getReadModel(), m, params);
    }
    else if (ns == method::Plugin)      return dispatchPlugin(engine.getPluginService(), m, params, server);
    else if (ns == method::PluginParam) return dispatchPluginParam(engine.getPluginParamService(), m, params);
    else if (ns == method::Audio)       return dispatchAudio(engine, m, params);
    else if (ns == method::Midi)        return dispatchMidi(engine.getMidiService(), m, params);
    else if (ns == method::Export)      return dispatchExport(engine, m, params, server);
    else if (ns == method::Preview)     return dispatchPreview(engine, m, params);
    else if (ns == method::Composition) return dispatchComposition(engine, m, params);

    return makeError(-32601, "unknown method namespace: " + ns);
}

} // namespace frontend
