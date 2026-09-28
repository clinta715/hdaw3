#pragma once
// Unit-EXPLICIT time windows at the tool boundary (slice S6 of
// docs/plans/2026-09-28-agent-mechanization.md §4).
//
// ONE resolver, adopted by BOTH surfaces through the SAME choke points:
//   * the MCP tool dispatch (src/mcp/McpServer.cpp, after schema validation and
//     before the handler runs);
//   * the JSON-RPC dispatch (src/frontend/FrontendRouter.cpp, before the
//     namespace routers run).
// Because both surfaces call the SAME normalizer with the SAME spec table, an
// accepted conversion and a refusal are byte-identical BY CONSTRUCTION
// (AGENTS.md "Feature parity: MCP + RPC").
//
// The contract a caller sees:
//   * a window may be expressed with its MUSICAL spelling (`startBeat`/`endBeat`,
//     or the tool's documented bare `start`/`end`), its WALL-CLOCK twin
//     (`startSec`/`endSec`), or — where a bare spelling exists — either, chosen
//     by the optional `unit: "beats"|"seconds"` key;
//   * two spellings for the same endpoint that disagree (converted beats differ
//     by more than 1e-9) are REFUSED, naming both keys, never silently picked;
//   * the response echoes the unit actually used (spec.echoUnit).
//
// Canonical internal representation is BEATS (the tree's musical space);
// seconds are converted at the project BPM. A tool whose handler expects
// seconds (export_audio) still reads the key it always read — the normalizer
// writes the resolved value back in handlerUnit.
//
// This header is Qt-light (QJsonObject/QString only) so it can be included from
// src/common, the MCP server and the router without dragging engine/JUCE
// headers into a Qt-first translation unit (the INCLUDE-ORDER trap).
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <cmath>

namespace HDAW {

// ── units ───────────────────────────────────────────────────────────────────

inline double windowUnitSecondsToBeats(double seconds, double bpm)
{
    return bpm > 0.0 ? seconds * bpm / 60.0 : seconds;
}

inline double windowUnitBeatsToSeconds(double beats, double bpm)
{
    return bpm > 0.0 ? beats * 60.0 / bpm : beats;
}

// ── refusals (the ONE text per class, shared by both surfaces) ───────────────

// Two spellings of the SAME endpoint converted to different beats.
inline QString windowConflictError(const QString& keyA, const QString& keyB)
{
    return QStringLiteral("conflicting window units: %1 and %2 disagree").arg(keyA, keyB);
}

// A `unit` key whose value is neither "beats" nor "seconds".
inline QString windowBadUnitError(const QString& value)
{
    return QStringLiteral("invalid unit: %1 (expected \"beats\" or \"seconds\")").arg(value);
}

// The shared ordering refusal, used where a tool has no refusal text of its own.
inline constexpr const char* kWindowOrderError =
    "startBeat/endBeat invalid: endBeat must be greater than startBeat";

// ── spec + result ───────────────────────────────────────────────────────────

// One endpoint's spellings.
//   beatKey    explicit musical spelling ("" when the tool has none)
//   secKey     explicit wall-clock twin ("" when the tool has none)
//   bareKey    ambiguous spelling interpreted per `unit`/defaultUnit ("" none)
//   handlerKey where the handler reads the value; "" => beatKey, else bareKey
//   array      the keys hold ARRAYS of numbers resolved element-wise (times[])
struct WindowUnitEndpoint
{
    QString beatKey;
    QString secKey;
    QString bareKey;
    QString handlerKey;
    bool    array = false;
};

struct WindowUnitSpec
{
    QString tool;                 // MCP tool name (the registry key)
    QString rpc;                  // RPC "namespace.method" ("" if not routed)
    WindowUnitEndpoint start;
    WindowUnitEndpoint end;
    QString handlerUnit = QStringLiteral("beats");    // unit the handler expects
    QString defaultUnit = QStringLiteral("beats");    // interpretation of bareKey
    QString unitKey     = QStringLiteral("unit");
    QString nestedArray;          // "" or an array-of-objects key normalized per element
    bool    topLevel    = true;   // false: the window lives ONLY on nested elements
    bool    checkOrder  = false;  // refuse end <= start with orderError/shared text
    QString orderError;           // "" => kWindowOrderError
    bool    echoUnit    = false;  // echo the unit actually used in the response
    bool    echoText    = false;  // also echo onto a NON-JSON (plain text) response
    bool    surfaceMcp  = true;   // spec applies to the MCP tool surface
    bool    surfaceRpc  = true;   // spec applies to the JSON-RPC surface
    // For a FILE-based tool that takes a `bpm` argument (mix_report / mix_verdict
    // / mix_diff, whose windows describe a rendered file), convert with the
    // tool's OWN bpm when present and > 0, falling back to the project BPM.
    bool    useArgsBpm  = false;
};

struct WindowUnitArgs
{
    double  startBeats = 0.0;
    double  endBeats   = 0.0;
    QString unitUsed;              // "beats" | "seconds" | "" (nothing resolved)
    bool    startPresent = false;
    bool    endPresent   = false;
};

// ── resolution ──────────────────────────────────────────────────────────────

namespace windowunit_detail {

inline bool numericAt(const QJsonObject& o, const QString& key, double& v)
{
    if (key.isEmpty() || !o.contains(key)) return false;
    const QJsonValue jv = o.value(key);
    if (!jv.isDouble()) return false;
    v = jv.toDouble();
    return true;
}

inline bool arrayAt(const QJsonObject& o, const QString& key, QJsonArray& out)
{
    if (key.isEmpty() || !o.contains(key)) return false;
    const QJsonValue jv = o.value(key);
    if (!jv.isArray()) return false;
    out = jv.toArray();
    return true;
}

inline QString unitOfObject(const QJsonObject& o, const WindowUnitSpec& spec, QString& error)
{
    QString unit = spec.defaultUnit;
    if (spec.unitKey.isEmpty() || !o.contains(spec.unitKey)) return unit;
    const QJsonValue uv = o.value(spec.unitKey);
    if (!uv.isString()) return unit;   // schema types it; non-string is not ours
    const QString u = uv.toString();
    if (u != QLatin1String("beats") && u != QLatin1String("seconds"))
    {
        error = windowBadUnitError(u);
        return QString();
    }
    return u;
}

// Resolve one SCALAR endpoint. `error` is set only on a conflict.
inline bool resolveScalarEndpoint(const QJsonObject& o, double bpm,
                                  const WindowUnitEndpoint& ep, const QString& effectiveUnit,
                                  bool& present, double& beats, QString& kind, QString& error)
{
    struct Cand { QString key; double beats; QString kind; };
    const QList<Cand> cands = [&]() {
        QList<Cand> list;
        double v = 0.0;
        if (!ep.beatKey.isEmpty() && ep.beatKey != ep.bareKey && numericAt(o, ep.beatKey, v))
            list.append({ ep.beatKey, v, QStringLiteral("beats") });
        if (numericAt(o, ep.secKey, v))
            list.append({ ep.secKey, windowUnitSecondsToBeats(v, bpm), QStringLiteral("seconds") });
        if (ep.bareKey != ep.beatKey && numericAt(o, ep.bareKey, v))
        {
            const bool sec = (effectiveUnit == QLatin1String("seconds"));
            list.append({ ep.bareKey, sec ? windowUnitSecondsToBeats(v, bpm) : v,
                          sec ? QStringLiteral("seconds") : QStringLiteral("beats") });
        }
        return list;
    }();

    if (cands.isEmpty()) { present = false; return true; }
    present = true;
    for (int i = 0; i < cands.size(); ++i)
        for (int j = i + 1; j < cands.size(); ++j)
            if (std::abs(cands[i].beats - cands[j].beats) > 1e-9)
            {
                error = windowConflictError(cands[i].key, cands[j].key);
                return false;
            }
    beats = cands.front().beats;
    kind  = cands.front().kind;
    for (const auto& c : cands)
        if (c.kind == QLatin1String("beats")) { kind = QStringLiteral("beats"); break; }
    return true;
}

// Resolve one ARRAY endpoint element-wise (times[] / timesSec[]).
inline bool resolveArrayEndpoint(const QJsonObject& o, double bpm,
                                 const WindowUnitEndpoint& ep, const QString& effectiveUnit,
                                 bool& present, QJsonArray& beatsOut, QString& kind, QString& error)
{
    QJsonArray beatArr, secArr, bareArr;
    const bool hasBeat = arrayAt(o, ep.beatKey, beatArr);
    const bool hasSec  = arrayAt(o, ep.secKey, secArr);
    const bool hasBare = (ep.bareKey != ep.beatKey) && arrayAt(o, ep.bareKey, bareArr);
    if (!hasBeat && !hasSec && !hasBare) { present = false; return true; }
    present = true;

    const bool bareSec = (effectiveUnit == QLatin1String("seconds"));
    QJsonArray beats;   // canonical
    auto push = [&](const QJsonValue& v, bool sec) {
        beats.append(sec ? windowUnitSecondsToBeats(v.toDouble(), bpm) : v.toDouble());
    };
    if (hasBeat) for (const auto& v : beatArr) push(v, false);
    else if (hasSec) for (const auto& v : secArr) push(v, true);
    else for (const auto& v : bareArr) push(v, bareSec);

    // conflict when two spellings are present with different counts/values
    auto agree = [&](const QJsonArray& a, bool aSec) {
        if (a.size() != beats.size()) return false;
        for (int i = 0; i < a.size(); ++i)
        {
            const double x = aSec ? windowUnitSecondsToBeats(a[i].toDouble(), bpm) : a[i].toDouble();
            if (std::abs(x - beats[i].toDouble()) > 1e-9) return false;
        }
        return true;
    };
    if (hasBeat && hasSec && !agree(secArr, true))
    { error = windowConflictError(ep.beatKey, ep.secKey); return false; }
    if (hasBeat && hasBare && !agree(bareArr, bareSec))
    { error = windowConflictError(ep.beatKey, ep.bareKey); return false; }

    beatsOut = beats;
    kind = (!hasSec && !bareSec) ? QStringLiteral("beats") : QStringLiteral("seconds");
    if (hasBeat) kind = QStringLiteral("beats");
    return true;
}

} // namespace windowunit_detail

// Resolve a window pair with an ALREADY-RESOLVED effective unit (used for nested
// element objects that inherit the parent's `unit`). `out.unitUsed` is the unit
// the caller actually expressed the window in.
inline bool resolveWindowWithUnit(const QJsonObject& o, double bpm, const WindowUnitSpec& spec,
                                  const QString& effectiveUnit, WindowUnitArgs& out, QString& error)
{
    using namespace windowunit_detail;
    error.clear();
    out = WindowUnitArgs{};

    QString startKind, endKind;
    if (spec.start.array)
    {
        QJsonArray a;
        if (!resolveArrayEndpoint(o, bpm, spec.start, effectiveUnit, out.startPresent, a, startKind, error))
            return false;
    }
    else if (!resolveScalarEndpoint(o, bpm, spec.start, effectiveUnit, out.startPresent, out.startBeats, startKind, error))
        return false;

    if (spec.end.array)
    {
        QJsonArray a;
        if (!resolveArrayEndpoint(o, bpm, spec.end, effectiveUnit, out.endPresent, a, endKind, error))
            return false;
    }
    else if (!resolveScalarEndpoint(o, bpm, spec.end, effectiveUnit, out.endPresent, out.endBeats, endKind, error))
        return false;

    if (out.startPresent && startKind == QLatin1String("beats")) out.unitUsed = QStringLiteral("beats");
    else if (out.startPresent) out.unitUsed = startKind;
    else if (out.endPresent)   out.unitUsed = endKind;
    else                       out.unitUsed = effectiveUnit;

    if (spec.checkOrder && !spec.start.array && !spec.end.array
        && out.startPresent && out.endPresent && !(out.endBeats > out.startBeats))
    {
        error = spec.orderError.isEmpty() ? QString::fromUtf8(kWindowOrderError) : spec.orderError;
        return false;
    }
    return true;
}

// Resolve a window pair, validating the `unit` key first.
inline bool resolveWindow(const QJsonObject& o, double bpm, const WindowUnitSpec& spec,
                          WindowUnitArgs& out, QString& error)
{
    const QString effUnit = windowunit_detail::unitOfObject(o, spec, error);
    if (!error.isEmpty()) return false;
    return resolveWindowWithUnit(o, bpm, spec, effUnit, out, error);
}

// Convert a resolved BEATS window into the unit the HANDLER expects and write it
// into the handler's key (creating it). A missing endpoint is left untouched so
// the handler's own default still applies.
inline QString windowHandlerKey(const WindowUnitEndpoint& ep)
{
    if (!ep.handlerKey.isEmpty()) return ep.handlerKey;
    if (!ep.beatKey.isEmpty())    return ep.beatKey;
    return ep.bareKey;
}

inline void writeResolvedWindow(QJsonObject& o, double bpm, const WindowUnitSpec& spec,
                                const WindowUnitArgs& w)
{
    auto toHandler = [&](const WindowUnitEndpoint& ep, bool present, double beats)
    {
        const QString key = windowHandlerKey(ep);
        if (key.isEmpty() || !present) return;   // absent stays absent (handler default)
        if (spec.handlerUnit == QLatin1String("seconds"))
            o[key] = windowUnitBeatsToSeconds(beats, bpm);
        else
            o[key] = beats;
    };
    toHandler(spec.start, w.startPresent, w.startBeats);
    toHandler(spec.end,   w.endPresent,   w.endBeats);
}

// Array endpoints: recompute the canonical BEATS array and write it into the
// handler's key (times[] stays a beats array after the rewrite).
inline void writeResolvedArrayWindow(QJsonObject& o, double bpm, const WindowUnitSpec& spec,
                                     const QString& effectiveUnit)
{
    auto writeArray = [&](const WindowUnitEndpoint& ep)
    {
        const QString key = windowHandlerKey(ep);
        if (key.isEmpty()) return;
        QJsonArray beats, src;
        const bool hasBeat = windowunit_detail::arrayAt(o, ep.beatKey, beats);
        if (hasBeat) { o[key] = beats; return; }
        if (windowunit_detail::arrayAt(o, ep.secKey, src))
        {
            QJsonArray conv;
            for (const auto& v : src) conv.append(windowUnitSecondsToBeats(v.toDouble(), bpm));
            o[key] = conv;
            return;
        }
        if (ep.bareKey != ep.beatKey && windowunit_detail::arrayAt(o, ep.bareKey, src))
        {
            const bool sec = (effectiveUnit == QLatin1String("seconds"));
            QJsonArray conv;
            for (const auto& v : src) conv.append(sec ? windowUnitSecondsToBeats(v.toDouble(), bpm)
                                                      : v.toDouble());
            o[key] = conv;
        }
    };
    writeArray(spec.start);
    writeArray(spec.end);
}

// The single entry point both surfaces call for ONE spec.
//   * resolves the window (refusing a conflict / bad unit / (optional) order),
//   * writes the resolved value into the handler's key(s) in handlerUnit,
//   * reports `unitUsed` for the response echo.
// `o` is modified in place (it is the caller's argument copy).
inline bool normalizeWindowArgsForSpec(QJsonObject& o, double bpm, const WindowUnitSpec& spec,
                                       QString& unitUsed, QString& error)
{
    unitUsed.clear();
    error.clear();

    // A file-based tool that carries its own `bpm` (mix_report / mix_verdict /
    // mix_diff) converts with THAT tempo: its windows describe a rendered file,
    // not the live project.
    if (spec.useArgsBpm)
    {
        double argBpm = 0.0;
        if (windowunit_detail::numericAt(o, QStringLiteral("bpm"), argBpm) && argBpm > 0.0)
            bpm = argBpm;
    }

    // Top-level window.
    WindowUnitArgs w;
    if (spec.topLevel)
    {
        if (!resolveWindow(o, bpm, spec, w, error)) return false;
        if (w.startPresent || w.endPresent)
        {
            if (spec.start.array || spec.end.array)
            {
                const QString effUnit = windowunit_detail::unitOfObject(o, spec, error);
                if (!error.isEmpty()) return false;
                writeResolvedArrayWindow(o, bpm, spec, effUnit);
            }
            else
                writeResolvedWindow(o, bpm, spec, w);
            if (!w.unitUsed.isEmpty()) unitUsed = w.unitUsed;
        }
    }
    else
    {
        // Nested-only spec: still validate the top-level `unit` key.
        windowunit_detail::unitOfObject(o, spec, error);
        if (!error.isEmpty()) return false;
    }

    // Nested element objects (sections[]/events[]) inherit the effective unit.
    if (!spec.nestedArray.isEmpty() && o.contains(spec.nestedArray))
    {
        const QString effUnit = windowunit_detail::unitOfObject(o, spec, error);
        if (!error.isEmpty()) return false;
        QJsonArray arr = o.value(spec.nestedArray).toArray();
        for (int i = 0; i < arr.size(); ++i)
        {
            if (!arr[i].isObject()) continue;
            QJsonObject el = arr[i].toObject();
            WindowUnitArgs ew;
            if (!resolveWindowWithUnit(el, bpm, spec, effUnit, ew, error)) return false;
            if (ew.startPresent || ew.endPresent)
            {
                writeResolvedWindow(el, bpm, spec, ew);
                if (!ew.unitUsed.isEmpty()) unitUsed = ew.unitUsed;
            }
            arr[i] = el;
        }
        o[spec.nestedArray] = arr;
    }
    return true;
}

// ── response echo ───────────────────────────────────────────────────────────

// Add `unit` to a compact-JSON object payload. A NON-JSON (plain text) payload
// is left UNTOUCHED unless `allowText` — several tools answer with a bare id or
// "ok", and appending text there would corrupt the value a caller parses
// (add_note returns the noteId as text). export_audio opts in with allowText
// because its status line has no JSON shape to carry the unit.
inline QString withUnitEchoText(const QString& text, const QString& unitUsed, bool allowText = false)
{
    if (unitUsed.isEmpty()) return text;
    const QJsonDocument d = QJsonDocument::fromJson(text.toUtf8());
    if (d.isObject())
    {
        QJsonObject o = d.object();
        o.insert(QStringLiteral("unit"), unitUsed);
        return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
    }
    if (!allowText) return text;
    return text + QStringLiteral(" unit=") + unitUsed;
}

// ── registry ────────────────────────────────────────────────────────────────
// Every tool that speaks a time window, with the spellings it accepts. Keys:
//   B = explicit musical spelling, S = wall-clock twin, A = ambiguous/bare.
// `handlerUnit` is the unit the tool's OWN handler already reads (unchanged);
// `defaultUnit` is what a bare key means when no `unit` override is present.
// Reasons for the exclusions are recorded in docs/plans/…-mechanization.md §4.
inline const QList<WindowUnitSpec>& windowUnitSpecs()
{
    static const QList<WindowUnitSpec> specs = [] {
        auto ep = [](const char* b, const char* s, const char* a, const char* h, bool arr = false) {
            return WindowUnitEndpoint{ QString::fromUtf8(b), QString::fromUtf8(s),
                                       QString::fromUtf8(a), QString::fromUtf8(h), arr };
        };
        auto mk = [&](const char* tool, const char* rpc,
                      WindowUnitEndpoint start, WindowUnitEndpoint end,
                      const char* handlerUnit, const char* defaultUnit,
                      const char* nested, bool echo, bool order,
                      bool topLevelArg = true, bool echoText = false) {
            WindowUnitSpec s;
            s.tool = QString::fromUtf8(tool);
            s.rpc  = QString::fromUtf8(rpc);
            s.start = start; s.end = end;
            s.handlerUnit = QString::fromUtf8(handlerUnit);
            s.defaultUnit = QString::fromUtf8(defaultUnit);
            s.nestedArray = QString::fromUtf8(nested);
            s.echoUnit = echo;
            s.checkOrder = order;
            s.topLevel = topLevelArg;
            s.echoText = echoText;
            return s;
        };

        // An RPC-only spec: the route names its window keys differently from the
        // MCP tool (verified per route), so it gets its own spelling set.
        auto mkRpc = [&](const char* tool, const char* rpc,
                         WindowUnitEndpoint start, WindowUnitEndpoint end,
                         const char* nested = "", bool topLevelArg = true) {
            WindowUnitSpec s = mk(tool, rpc, start, end, "beats", "beats", nested, false, false,
                                  topLevelArg);
            s.surfaceMcp = false;
            return s;
        };

        QList<WindowUnitSpec> v;
        // ── the six ambiguous tools (bare start/end; default unit BEATS) ──────
        // Each now also accepts the explicit `startBeat`/`endBeat` musical
        // spelling, so a caller never has to remember whose bare name is which.
        v << mk("automation_preset", "project.applyAutomationPreset",
                ep("startBeat", "startSec", "start", "start"), ep("endBeat", "endSec", "end", "end"),
                "beats", "beats", "sections", true, false);
        v << mk("apply_movement_plan", "project.applyMovementPlan",
                ep("startBeat", "startSec", "start", "start"), ep("endBeat", "endSec", "end", "end"),
                "beats", "beats", "events", true, false, /*topLevel=*/false);
        v << mk("generate_automation_envelope", "project.generateAutomationEnvelope",
                ep("startBeat", "startSec", "start", "start"), ep("endBeat", "endSec", "end", "end"),
                "beats", "beats", "", true, true);
        v << mk("generate_clip_gain_envelope", "project.generateClipGainEnvelope",
                ep("startBeat", "startSec", "start", "start"), ep("endBeat", "endSec", "end", "end"),
                "beats", "beats", "", true, true);
        v << mk("generate_clip_cc_lane", "project.generateClipCcLane",
                ep("startBeat", "startSec", "start", "start"), ep("endBeat", "endSec", "end", "end"),
                "beats", "beats", "", true, true);
        // export_audio: bare start/end are SECONDS; the beat spelling is the new mode.
        // `end <= 0` is a meaningful sentinel (project end) so ordering is not refused.
        v << mk("export_audio", "export.audio",
                ep("startBeat", "startSec", "start", "start"), ep("endBeat", "endSec", "end", "end"),
                "seconds", "seconds", "", true, false, /*topLevel=*/true, /*echoText=*/true);

        // ── already-explicit beat windows: add the seconds twin ──────────────
        v << mk("verify_window", "composition.verifyWindow",
                ep("startBeat", "startSec", "", ""), ep("endBeat", "endSec", "", ""),
                "beats", "beats", "", false, false);
        v << mk("verify_part", "composition.verifyPart",
                ep("startBeat", "startSec", "", ""), ep("endBeat", "endSec", "", ""),
                "beats", "beats", "", false, false);
        v << mk("query_notes", "read.queryNotes",
                ep("startBeat", "startSec", "", ""), ep("endBeat", "endSec", "", ""),
                "beats", "beats", "", false, false);
        v << mk("query_clips", "read.queryClips",
                ep("startBeat", "startSec", "", ""), ep("endBeat", "endSec", "", ""),
                "beats", "beats", "", false, false);
        v << mk("create_section", "",
                ep("startBeat", "startSec", "", ""), ep("endBeat", "endSec", "", ""),
                "beats", "beats", "", false, false);
        v << mk("duplicate_region", "project.duplicateRegion",
                ep("startBeat", "startSec", "", ""), ep("endBeat", "endSec", "", ""),
                "beats", "beats", "", false, false);
        v << mk("ripple_delete", "project.rippleDelete",
                ep("startBeat", "startSec", "", ""), ep("endBeat", "endSec", "", ""),
                "beats", "beats", "", false, false);
        v << mk("insert_silence", "project.insertSilence",
                ep("startBeat", "startSec", "", ""), ep("endBeat", "endSec", "", ""),
                "beats", "beats", "", false, false);
        v << mk("param_verity", "composition.verifyParamSweep",
                ep("startBeat", "startSec", "", ""), ep("", "", "", ""),
                "beats", "beats", "", false, false);
        v << mk("param_verity_corpus", "composition.verifyParamCorpus",
                ep("startBeat", "startSec", "", ""), ep("", "", "", ""),
                "beats", "beats", "", false, false);
        v << mk("tone_verity", "audio.verifyTone",
                ep("startBeat", "startSec", "", ""), ep("", "", "", ""),
                "beats", "beats", "", false, false);
        v << mk("place_patterns", "composition.placePatterns",
                ep("startBeat", "startSec", "", ""), ep("", "", "", ""),
                "beats", "beats", "", false, false);
        v << mk("import_audio_file", "",
                ep("startBeat", "startSec", "", ""), ep("", "", "", ""),
                "beats", "beats", "", false, false);
        v << mk("add_instrument_part", "composition.addInstrumentPart",
                ep("startBeat", "startSec", "", ""), ep("", "", "", ""),
                "beats", "beats", "", false, false);

        // ── ambiguous bare beat windows across the clip/note/composition verbs ─
        // MCP surface: the canonical key is the bare one; the route (when it is a
        // twin) names it differently, so it gets its OWN spec below.
        v << mk("add_midi_clip", "",
                ep("startBeat", "startSec", "start", "start"), ep("lengthBeat", "lengthSec", "length", "length"),
                "beats", "beats", "", true, false);
        v << mk("add_audio_clip", "",
                ep("startBeat", "startSec", "start", "start"), ep("lengthBeat", "lengthSec", "length", "length"),
                "beats", "beats", "", true, false);
        v << mk("set_clip", "",
                ep("startBeat", "startSec", "start", "start"), ep("durationBeat", "durationSec", "duration", "duration"),
                "beats", "beats", "", true, false);
        // set_clip fade pairs: bare fadeIn/fadeOut are SECONDS, the musical twin is beats.
        v << mk("set_clip", "",
                ep("fadeInBeat", "fadeInSec", "fadeIn", "fadeIn"), ep("", "", "", ""),
                "seconds", "seconds", "", true, false);
        v << mk("set_clip", "",
                ep("fadeOutBeat", "fadeOutSec", "fadeOut", "fadeOut"), ep("", "", "", ""),
                "seconds", "seconds", "", true, false);
        v << mk("move_clip", "",
                ep("startBeat", "startSec", "start", "start"), ep("", "", "", ""),
                "beats", "beats", "", true, false);
        v << mk("duplicate_clip", "",
                ep("startBeat", "startSec", "start", "start"), ep("", "", "", ""),
                "beats", "beats", "", true, false);
        v << mk("add_note", "",
                ep("startBeat", "startSec", "start", "start"), ep("durationBeat", "durationSec", "duration", "duration"),
                "beats", "beats", "", true, false);
        v << mk("add_notes", "",
                ep("startBeat", "startSec", "start", "start"), ep("durationBeat", "durationSec", "duration", "duration"),
                "beats", "beats", "", true, false);
        v << mk("set_note", "",
                ep("startBeat", "startSec", "start", "start"), ep("durationBeat", "durationSec", "duration", "duration"),
                "beats", "beats", "", true, false);
        v << mk("generate_chord", "",
                ep("startBeat", "startSec", "start", "start"), ep("lengthBeat", "lengthSec", "length", "length"),
                "beats", "beats", "", true, false);
        v << mk("generate_phrase", "",
                ep("startBeat", "startSec", "start", "start"), ep("lengthBeat", "lengthSec", "length", "length"),
                "beats", "beats", "", true, false);
        v << mk("generate_progression", "",
                ep("startBeat", "startSec", "start", "start"), ep("", "", "", ""),
                "beats", "beats", "", true, false);
        v << mk("generate_rhythm_pattern", "",
                ep("startBeat", "startSec", "start", "start"), ep("", "", "", ""),
                "beats", "beats", "", true, false);
        v << mk("generate_psytrance", "",
                ep("startBeat", "startSec", "start", "start"), ep("endBeat", "endSec", "end", "end"),
                "beats", "beats", "", true, false);
        v << mk("add_automation_point", "project.addAutomationPoint",
                ep("timeBeat", "timeSec", "time", "time"), ep("", "", "", ""),
                "beats", "beats", "", true, false);
        // set_automation_points: the window lives on each `points[]` element.
        v << mk("set_automation_points", "",
                ep("timeBeat", "timeSec", "time", "time"), ep("", "", "", ""),
                "beats", "beats", "points", true, false, /*topLevel=*/false);
        v << mk("slice_clip_at_times", "project.sliceClipAtTimes",
                ep("times", "timesSec", "times", "times", true), ep("", "", "", ""),
                "beats", "beats", "", true, false);
        v << mk("add_cc_point", "project.addCcPoint",
                ep("", "beatSec", "beat", "beat"), ep("", "", "", ""),
                "beats", "beats", "", true, false);
        v << mk("set_cc_point", "project.setCcPoint",
                ep("", "beatSec", "beat", "beat"), ep("", "", "", ""),
                "beats", "beats", "", true, false);
        v << mk("audition_plugin", "composition.auditionPlugin",
                ep("noteDurationBeat", "noteDurationSec", "noteDuration", "noteDuration"), ep("", "", "", ""),
                "beats", "beats", "", true, false);
        v << mk("list_notes", "",
                ep("startGteBeat", "startGteSec", "startGte", "startGte"),
                ep("startLtBeat", "startLtSec", "startLt", "startLt"),
                "beats", "beats", "", true, false);
        v << mk("remove_notes", "",
                ep("startGteBeat", "startGteSec", "startGte", "startGte"),
                ep("startLtBeat", "startLtSec", "startLt", "startLt"),
                "beats", "beats", "", true, false);
        v << mk("set_note_velocities", "",
                ep("startGteBeat", "startGteSec", "startGte", "startGte"),
                ep("startLtBeat", "startLtSec", "startLt", "startLt"),
                "beats", "beats", "", true, false);

        // ── seconds-default windows (bare/wall-clock keys + a beat spelling) ──
        // The handler reads the BARE key, so handlerKey points at it.
        v << mk("mix_report", "audio.mixReport",
                ep("startBeat", "startSec", "start", "start"), ep("endBeat", "endSec", "end", "end"),
                "seconds", "seconds", "sections", true, false, /*topLevel=*/false);
        v << mk("mix_verdict", "audio.mixVerdict",
                ep("startBeat", "startSec", "start", "start"), ep("endBeat", "endSec", "end", "end"),
                "seconds", "seconds", "sections", true, false, /*topLevel=*/false);
        v << mk("mix_diff", "",
                ep("startBeat", "startSec", "start", "start"), ep("endBeat", "endSec", "end", "end"),
                "seconds", "seconds", "sections", true, false, /*topLevel=*/false);
        // Arranger / transport / seek: their payload is a JSON OBJECT carrying
        // the original value under a named key (add_arranger_region →
        // {"regionID": …}; the status replies → {"ok": true}), so the unit can
        // be echoed like every other adopted tool (echoUnit=true).
        v << mk("add_arranger_region", "project.addArrangerRegion",
                ep("startTimeBeat", "startTimeSec", "startTime", "startTime"),
                ep("durationBeat", "durationSec", "duration", "duration"),
                "seconds", "seconds", "", true, false);
        v << mk("set_arranger_region_bounds", "project.setArrangerRegionBounds",
                ep("startTimeBeat", "startTimeSec", "startTime", "startTime"),
                ep("durationBeat", "durationSec", "duration", "duration"),
                "seconds", "seconds", "", true, false);
        v << mk("transport", "",
                ep("loopStartBeat", "loopStartSec", "loopStart", "loopStart"),
                ep("loopEndBeat", "loopEndSec", "loopEnd", "loopEnd"),
                "seconds", "seconds", "", true, false);
        v << mk("seek", "",
                ep("positionBeat", "positionSec", "position", "position"), ep("", "", "", ""),
                "seconds", "seconds", "", true, false);

        // ── RPC-only specs: routes whose window keys differ from the tool's ───
        // Verified against the router bodies (project.addMidiClip/addAudioClip:
        // Router_Project.cpp:259-270 -> the command converts beats at
        // AudioEngineCommands_Clips.cpp:24-25/91-92; moveClip/moveClipWithOverlap
        // `newStart` beats at :127/:154; importAudioFile `start` beats at :49;
        // addNote `startBeat`/`durationBeats` at :451; the composition generators
        // `startBeat`/`lengthBeats`/`durationBeats` at Router_Composition.cpp:
        // 656/744-747/775-779/795/809/891).
        v << mkRpc("add_midi_clip", "project.addMidiClip",
                   ep("startBeat", "startSec", "start", "start"),
                   ep("durationBeat", "durationSec", "duration", "duration"));
        v << mkRpc("add_audio_clip", "project.addAudioClip",
                   ep("startBeat", "startSec", "start", "start"),
                   ep("durationBeat", "durationSec", "duration", "duration"));
        v << mkRpc("move_clip", "project.moveClip",
                   ep("newStartBeat", "newStartSec", "newStart", "newStart"), ep("", "", "", ""));
        v << mkRpc("move_clip", "project.moveClipWithOverlap",
                   ep("newStartBeat", "newStartSec", "newStart", "newStart"), ep("", "", "", ""));
        v << mkRpc("import_audio_file", "project.importAudioFile",
                   ep("startBeat", "startSec", "start", "start"), ep("", "", "", ""));
        v << mkRpc("add_note", "project.addNote",
                   ep("startBeat", "startSec", "", ""), ep("durationBeats", "durationSec", "", ""));
        v << mkRpc("generate_chord", "composition.generateChord",
                   ep("startBeat", "startSec", "", ""), ep("durationBeats", "durationSec", "", ""));
        v << mkRpc("generate_phrase", "composition.generatePhrase",
                   ep("startBeat", "startSec", "", ""), ep("lengthBeats", "lengthSec", "", ""));
        v << mkRpc("generate_progression", "composition.generateProgression",
                   ep("startBeat", "startSec", "", ""), ep("", "", "", ""));
        v << mkRpc("generate_rhythm_pattern", "composition.generateRhythmPattern",
                   ep("startBeat", "startSec", "", ""), ep("", "", "", ""));
        v << mkRpc("generate_psytrance", "composition.generatePsytrance",
                   ep("startBeat", "startSec", "start", "start"),
                   ep("endBeat", "endSec", "end", "end"), "sections", /*topLevel=*/false);
        // Route-only setters the set_note / set_clip tools fan out to: their own
        // spelling is `startBeat`/`durationBeats` (beats) and `start`/`duration`
        // (beats) for the clip setters; fades are seconds.
        v << mkRpc("set_note", "project.setNoteStart",
                   ep("startBeat", "startSec", "", ""), ep("", "", "", ""));
        v << mkRpc("set_note", "project.setNoteDuration",
                   ep("durationBeats", "durationSec", "", ""), ep("", "", "", ""));
        v << mkRpc("set_clip", "project.setClipStart",
                   ep("startBeat", "startSec", "start", "start"), ep("", "", "", ""));
        v << mkRpc("set_clip", "project.setClipDuration",
                   ep("durationBeat", "durationSec", "duration", "duration"), ep("", "", "", ""));
        v << mkRpc("set_clip", "project.setClipFadeIn",
                   ep("fadeInBeat", "fadeInSec", "fadeIn", "fadeIn"), ep("", "", "", ""));
        v << mkRpc("set_clip", "project.setClipFadeOut",
                   ep("fadeOutBeat", "fadeOutSec", "fadeOut", "fadeOut"), ep("", "", "", ""));

        // mix_report / mix_verdict / mix_diff describe a RENDERED FILE: convert
        // their windows with the tool's own `bpm` argument when present.
        for (auto& s : v)
            if (s.tool == QLatin1String("mix_report") || s.tool == QLatin1String("mix_verdict")
                || s.tool == QLatin1String("mix_diff"))
                s.useArgsBpm = true;
        return v;
    }();
    return specs;
}

// All specs for ONE MCP tool name (a tool may have more than one pair, e.g.
// set_clip start/duration + fadeIn/fadeOut).
inline QList<WindowUnitSpec> windowSpecsFor(const QString& tool)
{
    QList<WindowUnitSpec> out;
    for (const auto& s : windowUnitSpecs())
        if (s.tool == tool) out.append(s);
    return out;
}

// All specs for ONE MCP tool name, optionally filtered to ONE surface (a tool
// may have more than one pair, and the MCP tool and its RPC route may use
// DIFFERENT argument names — each such route gets its own rpc-only spec).
inline QList<WindowUnitSpec> windowSpecsFor(const QString& tool, bool rpc = false)
{
    QList<WindowUnitSpec> out;
    for (const auto& s : windowUnitSpecs())
        if (s.tool == tool && (rpc ? s.surfaceRpc : s.surfaceMcp)) out.append(s);
    return out;
}

// The MCP tool name for an RPC method ("" when the method speaks no window).
inline QString windowToolForRpcMethod(const QString& method)
{
    for (const auto& s : windowUnitSpecs())
        if (!s.rpc.isEmpty() && s.rpc == method) return s.tool;
    return QString();
}

// Merge the endpoint keys a spec accepts into a SCHEDULE-free presence test:
// does this tool speak a window at all? (used by the hooks to decide to run).
inline bool toolSpeaksWindow(const QString& tool, bool rpc = false)
{
    return !windowSpecsFor(tool, rpc).isEmpty();
}

// Normalize EVERY spec of ONE tool on ONE surface. Returns false with `error`
// on the first refusal; `unitUsed` is the unit actually used ("" when the tool
// speaks no window or nothing was expressed).
inline bool normalizeWindowArgsForTool(const QString& tool, QJsonObject& o, double bpm,
                                       QString& unitUsed, QString& error, bool rpc = false)
{
    unitUsed.clear();
    error.clear();
    const QList<WindowUnitSpec> specs = windowSpecsFor(tool, rpc);
    if (specs.isEmpty()) return true;
    for (const auto& s : specs)
    {
        QString u, e;
        if (!normalizeWindowArgsForSpec(o, bpm, s, u, e)) { error = e; return false; }
        if (!u.isEmpty()) unitUsed = u;
    }
    return true;
}

// The echo decision for ONE tool on ONE surface: the first spec asking for it.
inline bool toolEchoesUnit(const QString& tool, bool rpc = false)
{
    for (const auto& s : windowSpecsFor(tool, rpc))
        if (s.echoUnit) return true;
    return false;
}

// Whether the tool's plain-text response may carry the ` unit=<u>` suffix.
inline bool toolEchoesUnitText(const QString& tool, bool rpc = false)
{
    for (const auto& s : windowSpecsFor(tool, rpc))
        if (s.echoText) return true;
    return false;
}

// ── schema injection ────────────────────────────────────────────────────────
// Declare every spelling the specs accept in the tool's inputSchema, so the MCP
// validator (additionalProperties:false) ACCEPTS the twin keys instead of
// refusing them as "unknown property". This is the SAME central-hook idea as
// S1's unit annotation (McpServer::registerTool enriches every schema once):
// the spec table is the single source of truth, so the accepted keys and the
// resolver's keys cannot drift. Idempotent — an existing key is left alone.
inline QString windowInjectedDescription(const QString& key, const WindowUnitSpec& spec)
{
    auto describe = [&](const WindowUnitEndpoint& ep) -> QString {
        if (key == ep.secKey && !ep.secKey.isEmpty())
            return QStringLiteral("Wall-clock (seconds) spelling of `%1`.")
                .arg(ep.bareKey.isEmpty() ? ep.beatKey : ep.bareKey);
        if (key == ep.beatKey && !ep.beatKey.isEmpty() && ep.beatKey != ep.bareKey)
            return QStringLiteral("Musical (beats) spelling of `%1`.")
                .arg(ep.bareKey.isEmpty() ? ep.beatKey : ep.bareKey);
        return QString();
    };
    QString d = describe(spec.start);
    if (d.isEmpty()) d = describe(spec.end);
    return d;
}

inline void injectWindowEndpointKeys(QJsonObject& props, const WindowUnitSpec& spec)
{
    for (const WindowUnitEndpoint* ep : { &spec.start, &spec.end })
    {
        for (const QString& key : { ep->beatKey, ep->secKey, ep->bareKey })
        {
            if (key.isEmpty() || props.contains(key)) continue;
            QJsonObject p{{"type", ep->array ? "array" : "number"}};
            if (ep->array) p["items"] = QJsonObject{{"type","number"}};
            const QString desc = windowInjectedDescription(key, spec);
            if (!desc.isEmpty()) p["description"] = desc;
            props[key] = p;
        }
    }
}

inline QJsonObject injectWindowUnitProperties(const QString& tool, const QJsonObject& schemaIn)
{
    const QList<WindowUnitSpec> specs = windowSpecsFor(tool, /*rpc=*/false);
    if (specs.isEmpty()) return schemaIn;

    QJsonObject schema = schemaIn;
    if (!schema.value("properties").isObject()) return schema;
    QJsonObject props = schema.value("properties").toObject();

    bool wantsUnit = false;
    for (const auto& s : specs)
    {
        if (s.topLevel)
            injectWindowEndpointKeys(props, s);
        wantsUnit = true;
        // Nested array-of-objects windows (sections[]/events[]/points[]) need the
        // same keys on their item schema.
        if (!s.nestedArray.isEmpty() && props.contains(s.nestedArray))
        {
            QJsonObject arr = props.value(s.nestedArray).toObject();
            QJsonObject items = arr.value("items").toObject();
            if (items.value("type").toString() == QLatin1String("object"))
            {
                QJsonObject ip = items.value("properties").toObject();
                if (!ip.isEmpty())
                {
                    injectWindowEndpointKeys(ip, s);
                    items["properties"] = ip;
                    arr["items"] = items;
                    props[s.nestedArray] = arr;
                }
            }
        }
    }
    if (wantsUnit && !props.contains(QStringLiteral("unit")))
        props[QStringLiteral("unit")] = QJsonObject{
            {"type","string"},
            {"description","Optional override of the bare start/end unit: \"beats\" or \"seconds\". "
                           "With no `unit` key the bare keys keep the tool's documented default."}};
    schema["properties"] = props;
    return schema;
}

} // namespace HDAW
