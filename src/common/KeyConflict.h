#pragma once
// key_check — will this candidate key clash with the project's key?
//
// The shared implementation behind BOTH surfaces of the MCP `key_check` tool /
// the `composition.keyCheck` RPC method (parity by construction, the
// MixVerdict / ModulationCoverage precedent): the argument resolver, the
// theory, and the payload builder all live here so the two surfaces cannot
// drift — docs/plans/2026-09-23-key-check.md (frozen interface).
//
// scaleIntervals(int) is THE mode -> pitch-class-interval table. It was lifted
// out of ArrangementGenerator.cpp (which now calls this one — do NOT write a
// second table) and is the same table MarkovRoles.h documents for snapToScale.
// It reads PhraseGenerator::getScaleModes(), the one canonical mode list.
//
// checkKeyConflict verdict rules (every class is symmetric in A/B except the
// directed root-to-root interval):
//   unison      same root, same mode
//   parallel    same root, different mode               (the parallel shift)
//   relative    different root, identical pitch-class sets (relative major/
//               minor — and symmetric scales pair up the same way)
//   consonant   roots a perfect fifth/fourth apart (7 or 5 semitones)
//   conflicting roots 1, 6 (tritone), or 11 semitones apart while pitch-class
//               overlap stays below kOverlapRescue
//   neutral     everything else — INCLUDING hostile root intervals rescued by
//               a high pitch-class overlap: the verdict must reflect how much
//               material the two scales SHARE, not only the root interval
//
// pitchClassOverlap = shared pitch classes / larger set size (0..1).

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "../engine/PhraseGenerator.h"

#include <algorithm>
#include <bitset>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace HDAW {

struct KeyVerdict {
    bool ok = false; std::string error;
    int intervalSemitones = -1;      // root-to-root (B relative to A, 0..11)
    std::string relation;            // unison | relative | parallel | consonant | neutral | conflicting
    std::string reason;              // human sentence the palette step can quote
    float pitchClassOverlap = 0.0f;  // 0..1
};

// Overlap at/above which even a hostile root interval (1 / 6 / 11 semitones)
// reads "neutral" instead of "conflicting": shared material softens the clash.
inline constexpr float kOverlapRescue = 0.5f;

inline bool scaleModeExists(int mode)
{
    for (const auto& m : PhraseGenerator::getScaleModes())
        if (m.index == mode) return true;
    return false;
}

// THE mode -> pitch-class table (lifted from ArrangementGenerator.cpp — the
// one place that used to own it). Unknown mode keeps the aeolian fallback.
inline std::vector<int> scaleIntervals(int scaleMode)
{
    for (const auto& m : PhraseGenerator::getScaleModes())
        if (m.index == scaleMode)
            return m.intervals;
    return { 0, 2, 3, 5, 7, 8, 10 }; // aeolian fallback
}

// Canonical scale-mode name as listed by get_scale_modes: "Minor (Aeolian)".
// "unknown" for -1 / unknown.
inline std::string scaleModeFullName(int mode)
{
    for (const auto& m : PhraseGenerator::getScaleModes())
        if (m.index == mode)
            return m.name;
    return "unknown";
}

// Lowercase + trimmed.
inline std::string trimmedLower(std::string s)
{
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    s = s.substr(b, e - b + 1);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Short, lowercase mode name for human key strings: "Minor (Aeolian)" ->
// "minor", "Harmonic Minor" -> "harmonic minor". "unknown" for -1 / unknown.
// The MCP key strings (analyze_midi_file) and every key_check reason use this
// spelling, so a key string round-trips through resolveScaleModeIndex.
inline std::string scaleModeShortName(int mode)
{
    for (const auto& m : PhraseGenerator::getScaleModes())
        if (m.index == mode) {
            std::string full = m.name;
            const auto paren = full.find(" (");
            if (paren != std::string::npos) full = full.substr(0, paren);
            std::transform(full.begin(), full.end(), full.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return full;
        }
    return "unknown";
}

// Resolve a scale-mode NAME to its PhraseGenerator index (-1 if unknown).
// Accepts the canonical name ("Minor (Aeolian)"), the short form ("minor"),
// and the parenthetical church-mode alias ("aeolian"), case-insensitively —
// exactly what the MCP scale_note tool accepts (one shared implementation for
// both surfaces, not a second resolver).
inline int resolveScaleModeIndex(const std::string& name)
{
    const std::string needle = trimmedLower(name);
    if (needle.empty()) return -1;
    for (const auto& m : PhraseGenerator::getScaleModes()) {
        const std::string full = trimmedLower(m.name);   // "minor (aeolian)"
        const auto paren = full.find(" (");
        const std::string shortName =
            (paren != std::string::npos) ? full.substr(0, paren) : full;  // "minor"
        const std::string church = (paren != std::string::npos)
            ? full.substr(paren + 2, full.size() - paren - 3)             // "aeolian"
            : std::string();
        if (needle == full || needle == shortName || (!church.empty() && needle == church))
            return m.index;
    }
    return -1;
}

// Pitch class for a note-name token, case-insensitive: naturals/sharps (the
// spelling analyze_midi_file emits via getMidiNoteName(sharps=true)) plus the
// common flat spellings a human types. -1 when unrecognized.
inline int parseNotePc(std::string tok)
{
    tok = trimmedLower(tok);
    if (tok.empty()) return -1;
    for (int pc = 0; pc < 12; ++pc) {
        std::string sharp = PhraseGenerator::noteName(60 + pc); // "C#4"
        sharp.pop_back();                                       // -> "C#", then lowercase
        std::transform(sharp.begin(), sharp.end(), sharp.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (tok == sharp) return pc;
    }
    static const std::pair<const char*, int> kFlats[] = {
        { "db", 1 }, { "eb", 3 }, { "gb", 6 }, { "ab", 8 }, { "bb", 10 }
    };
    for (const auto& [name, pc] : kFlats)
        if (tok == name) return pc;
    return -1;
}

// The human key string in exactly the spelling analyze_midi_file's `key`
// field uses: sharps, no octave, lowercase mode — "F minor".
inline std::string keyToString(int rootPc, int mode)
{
    std::string root = PhraseGenerator::noteName(60 + (((rootPc % 12) + 12) % 12));
    root.pop_back(); // "F4" -> "F"
    return root + " " + scaleModeShortName(mode);
}

// ── The theory ─────────────────────────────────────────────────────────────

inline KeyVerdict checkKeyConflict(int rootA, int modeA, int rootB, int modeB)
{
    KeyVerdict v;
    if (rootA < 0 || rootA > 11) {
        v.error = "rootA must be a pitch class 0..11 (got " + std::to_string(rootA) + ")";
        return v;
    }
    if (rootB < 0 || rootB > 11) {
        v.error = "rootB must be a pitch class 0..11 (got " + std::to_string(rootB) + ")";
        return v;
    }
    if (!scaleModeExists(modeA)) {
        v.error = "unknown scale mode index for key A: " + std::to_string(modeA);
        return v;
    }
    if (!scaleModeExists(modeB)) {
        v.error = "unknown scale mode index for key B: " + std::to_string(modeB);
        return v;
    }

    std::bitset<12> pcsA, pcsB;
    for (int iv : scaleIntervals(modeA)) pcsA.set((rootA + iv) % 12);
    for (int iv : scaleIntervals(modeB)) pcsB.set((rootB + iv) % 12);

    const int shared = static_cast<int>((pcsA & pcsB).count());
    const int denom = static_cast<int>((std::max)(pcsA.count(), pcsB.count()));
    v.pitchClassOverlap =
        denom > 0 ? static_cast<float>(shared) / static_cast<float>(denom) : 0.0f;
    v.intervalSemitones = (rootB - rootA + 12) % 12;

    char ov[16];
    std::snprintf(ov, sizeof(ov), "%.2f", static_cast<double>(v.pitchClassOverlap));
    const std::string sh = std::string(ov) + " overlap (" + std::to_string(shared) + "/"
                         + std::to_string(denom) + " tones)";
    const std::string head = keyToString(rootA, modeA) + " vs " + keyToString(rootB, modeB) + ": ";
    const int iv = v.intervalSemitones;
    const std::string ivs = std::to_string(iv);

    if (iv == 0 && modeA == modeB) {
        v.relation = "unison";
        v.reason = head + "identical key - no clash.";
    } else if (iv == 0) {
        v.relation = "parallel";
        v.reason = head + "same root, different mode - a parallel shift; " + sh
                 + ". Expect a deliberate dark/light color change rather than a clash.";
    } else if (pcsA == pcsB) {
        v.relation = "relative";
        v.reason = head + "relative keys - identical pitch material, so anything written "
                 "in one fits the other; " + sh + ".";
    } else if (iv == 5 || iv == 7) {
        v.relation = "consonant";
        v.reason = head + "roots " + ivs + " semitones apart ("
                 + (iv == 7 ? std::string("perfect fifth") : std::string("perfect fourth"))
                 + "); " + sh + " - a classic compatible relationship.";
    } else if (iv == 1 || iv == 6 || iv == 11) {
        const std::string label = (iv == 6) ? std::string("tritone")
                                            : std::string("minor second");
        if (v.pitchClassOverlap >= kOverlapRescue) {
            v.relation = "neutral";
            v.reason = head + "roots " + ivs + " semitones apart (" + label + "), but " + sh
                     + " - the shared tones soften the clash: less sour than the root "
                       "interval suggests.";
        } else {
            v.relation = "conflicting";
            v.reason = head + "roots " + ivs + " semitones apart (" + label + "); " + sh
                     + " - high risk of sounding sour. Audition before committing.";
        }
    } else {
        v.relation = "neutral";
        v.reason = head + "roots " + ivs + " semitones apart; " + sh
                 + " - workable but not a standard consonant relationship. Audition "
                   "before committing.";
    }
    v.ok = true;
    return v;
}

// ── Surface seam (shared by MCP `key_check` and RPC `composition.keyCheck`) ──

// Candidate half of a key_check call. Both surfaces pass their QJsonObject
// args straight in, so argument keys, defaults, and failure text are identical
// by construction. Shapes the rest of the surface already produces:
//   key         a human key string, "F minor" (analyze_midi_file's `key`);
//               must carry BOTH a note and a mode — a modeless key is what
//               analyze_midi_file emits when detection FAILED, so it errors
//               instead of guessing
//   root        0..11 pitch class (get_scale) or a MIDI note 0..127
//               (analyze_midi_file's fingerprint.rootNote), reduced mod 12
//   scaleMode   a PhraseGenerator mode index (the get_scale family's name)
//   scaleType   the same index under analyze_midi_file's name — scaleMode
//               wins when both are given
//   scale       a mode NAME ("minor", "Minor (Aeolian)", "aeolian") — the
//               spelling the MCP scale_note tool accepts
// Omitted fields fall back to the project's current key (get_scale): no
// arguments at all checks the project key against itself. A PRESENT but bad
// value always errors — "cannot determine the candidate's key: ..." — never a
// confident wrong answer.
struct CandidateKey {
    bool ok = false;
    std::string error;
    int root = 0;
    int mode = 0;
};

inline bool jsonToInt(const QJsonValue& v, int& out)
{
    if (!v.isDouble()) return false;
    const double d = v.toDouble();
    if (d != std::trunc(d)) return false;                       // reject 65.5
    if (d < static_cast<double>(std::numeric_limits<int>::min()) ||
        d > static_cast<double>(std::numeric_limits<int>::max()))
        return false;
    out = static_cast<int>(d);
    return true;
}

inline CandidateKey resolveCandidateKey(const QJsonObject& args,
                                        int projectRoot, int projectMode)
{
    CandidateKey c;
    const auto fail = [&c](const std::string& detail) {
        c.ok = false;
        c.error = "cannot determine the candidate's key: " + detail;
        return c;
    };

    const bool hasKey       = args.contains("key");
    const bool hasRoot      = args.contains("root");
    const bool hasScaleMode = args.contains("scaleMode");
    const bool hasScaleType = args.contains("scaleType");
    const bool hasScale     = args.contains("scale");

    if (hasKey) { // a complete human key wins over any integer fields
        if (!args.value("key").isString())
            return fail("\"key\" must be a string");
        const std::string s = args.value("key").toString().toStdString();
        const auto ws = s.find_first_of(" \t");
        const std::string tok = s.substr(0, ws);
        const std::string rest = (ws == std::string::npos)
            ? std::string() : trimmedLower(s.substr(ws + 1));
        const int root = parseNotePc(tok);
        if (root < 0)
            return fail("unrecognized note name \"" + tok + "\" in \"" + s + "\"");
        if (rest.empty())
            return fail("no scale mode in \"" + s + "\" - give a full key like "
                        "\"F minor\", or pass scaleMode/scaleType/scale");
        const int mode = resolveScaleModeIndex(rest);
        if (mode < 0)
            return fail("unknown scale mode \"" + rest + "\" in \"" + s + "\"");
        c.ok = true;
        c.root = root;
        c.mode = mode;
        return c;
    }

    // Integer/name shape: fields you omit default to the project key; fields
    // you give are validated.
    c.root = projectRoot;
    c.mode = projectMode;

    if (hasRoot) {
        int root;
        if (!jsonToInt(args.value("root"), root))
            return fail("\"root\" must be an integer");
        if (root < 0 || root > 127)
            return fail("root out of range 0..127 (got " + std::to_string(root) + ")");
        c.root = root % 12;
    }
    if (hasScaleMode || hasScaleType) {
        const char* which = hasScaleMode ? "scaleMode" : "scaleType";
        int mode;
        if (!jsonToInt(args.value(hasScaleMode ? "scaleMode" : "scaleType"), mode))
            return fail(std::string("\"") + which + "\" must be an integer");
        if (!scaleModeExists(mode))
            return fail(std::string("unknown ") + which + " " + std::to_string(mode));
        c.mode = mode;
    } else if (hasScale) {
        if (!args.value("scale").isString())
            return fail("\"scale\" must be a string");
        const std::string s = args.value("scale").toString().toStdString();
        const int mode = resolveScaleModeIndex(s);
        if (mode < 0)
            return fail("unknown scale \"" + s + "\"");
        c.mode = mode;
    }

    c.ok = true;
    return c;
}

// The ONE payload builder (parity by construction). pitchClassOverlap is
// rounded to 4 decimals so the JSON survives any serialization round-trip
// byte-identically on both surfaces.
inline QJsonObject keyCheckJson(const KeyVerdict& v)
{
    QJsonObject o;
    o["ok"] = v.ok;
    if (v.ok) {
        o["intervalSemitones"] = v.intervalSemitones;
        o["relation"] = QString::fromStdString(v.relation);
        o["reason"] = QString::fromStdString(v.reason);
        o["pitchClassOverlap"] =
            std::round(static_cast<double>(v.pitchClassOverlap) * 10000.0) / 10000.0;
    } else {
        o["error"] = QString::fromStdString(v.error);
    }
    return o;
}

} // namespace HDAW
