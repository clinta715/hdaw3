#pragma once
#include <juce_core/juce_core.h>
#include <string>
#include <vector>

#include "../common/ProjectCommands.h"
#include "../model/ProjectModel.h"

// ─────────────────────────────────────────────────────────────────────────────
// SongStructureAudit — READ-ONLY arrangement-variety audit (the Mix Verifier
// boredom/static-span gates from docs/skills/psy-song-session). Pure tree
// analysis: song-plan sections (BEATS) × track clips (SECONDS) × layer-handoff
// roles / track names. No audio, no mutation, no engine state — the caller
// passes the track-list ValueTree and the current plan.
//
// Gates (each mapped to the psy-song-session Mix Verifier contract):
//   * boredom spans  — a run of >= 8 bars of consecutive non-build sections
//     where NO melodic role and NO backbeat role sounds: hats-only (only
//     texture/percussion roles), bass-hat-only (only floor + texture), or
//     silence. Build sections are tension markers and never flagged.
//   * drop backbeat  — every drop-kind section (mainA/mainB/finale/drop)
//     must have a backbeat role (clap/snare/backbeat) sounding.
//   * first-drop motif — the FIRST drop-kind section must have a melodic
//     role (lead/stab/arp/pad/chord/pluck/...) sounding.
//
// Roles come from the layer-handoff ledger (IDs::layerRole) when a track has
// one, else from the track name (lowercased substring match). A track sounds
// in a section when any of its clips overlaps the section's seconds range
// (beat→sec via the plan bpm; clips are already in seconds).
// ─────────────────────────────────────────────────────────────────────────────

namespace HDAW {

enum class RoleKind {
    Floor,      // kick / bass / sub
    Texture,    // hats, shaker, rim, perc, cymbal, ride, fx, riser, ...
    Backbeat,   // clap / snare / backbeat
    Melodic,    // lead / stab / arp / pad / chord / pluck / motif / vocal / ...
    Other
};

inline const char* roleKindName(RoleKind k)
{
    switch (k)
    {
        case RoleKind::Floor:    return "floor";
        case RoleKind::Texture:  return "texture";
        case RoleKind::Backbeat: return "backbeat";
        case RoleKind::Melodic:  return "melodic";
        default:                 return "other";
    }
}

// First matching category wins (most-relevant-checked first: backbeat and
// melodic matter most to the gates; floor before texture so "kickhats" reads
// floor). Empty/unclassified -> Other.
inline RoleKind roleKindFor(const juce::String& roleLower)
{
    static const juce::StringArray backbeatWords{ "clap", "snare", "backbeat" };
    static const juce::StringArray melodicWords{
        "lead", "stab", "arp", "pad", "chord", "pluck", "motif", "vocal",
        "synth", "acid", "growl", "melody", "ost" };
    static const juce::StringArray floorWords{ "kick", "bass", "sub" };
    static const juce::StringArray textureWords{
        "hat", "shaker", "rim", "perc", "cymbal", "ride", "crash",
        "noise", "riser", "down", "fx" };
    const auto wi = [&roleLower](const juce::StringArray& words) -> bool {
        for (const auto& w : words)
            if (roleLower.contains(w)) return true;
        return false;
    };
    if (wi(backbeatWords)) return RoleKind::Backbeat;
    if (wi(melodicWords))  return RoleKind::Melodic;
    if (wi(floorWords))    return RoleKind::Floor;
    if (wi(textureWords))  return RoleKind::Texture;
    return RoleKind::Other;
}

struct StructureSectionAudit {
    std::string name;
    std::string kind;
    double startBeat = 0.0;
    double endBeat = 0.0;
    int bars = 0;
    std::vector<std::string> soundingRoles;   // lowercased role strings
    std::vector<std::string> roleKinds;       // per-role kind name, parallel
    bool hasMelodic = false;
    bool hasBackbeat = false;
};

struct StructureSpan {
    std::string flag;          // hats-only | bass-hat-only | silence
    std::string startName;     // first section name in the run
    std::string endName;       // last section name in the run
    double startBeat = 0.0;
    double endBeat = 0.0;
    int bars = 0;
};

struct SongStructureAudit {
    bool hasPlan = false;
    std::vector<StructureSectionAudit> sections;
    std::vector<StructureSpan> spans;
    std::vector<std::string> dropNamesMissingBackbeat;
    // Structural load proxy for the drop-vs-build loudness gate: a drop whose
    // sounding-role count is SMALLER than the build that precedes it is almost
    // always the quieter payoff (the audio-level check lives in mix_report
    // loudnessGates; this one is free and works without a render).
    std::vector<std::string> dropNamesThinnerThanBuild;
    std::string firstDropName;
    bool anyDrop = false;
    bool firstDropHasMotif = false;
    bool ok = false;    // all gates pass (vacuous when !hasPlan)
};

inline bool isDropKind(const juce::String& kindLower)
{
    return kindLower == "maina" || kindLower == "mainb"
        || kindLower == "finale" || kindLower == "drop";
}

inline bool isBuildKind(const juce::String& kindLower)
{
    return kindLower == "build" || kindLower == "build2";
}

inline SongStructureAudit auditSongStructure(const juce::ValueTree& trackList,
                                             const ProjectCommands::SongPlanData& plan,
                                             double bpm)
{
    SongStructureAudit audit;
    if (plan.sections.empty())
        return audit; // hasPlan stays false
    audit.hasPlan = true;

    const double effBpm = (plan.bpm > 0.0) ? plan.bpm : (bpm > 0.0 ? bpm : 120.0);
    const double spb = 60.0 / effBpm;

    // Roles per track (handoff first, then name; lowercased).
    struct TrackInfo { std::string role; RoleKind kind; bool anyClip; };
    std::vector<TrackInfo> tracks;
    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        const auto track = trackList.getChild(t);
        juce::String role = track.getProperty(IDs::layerRole, "").toString();
        if (role.isEmpty())
            role = track.getProperty(IDs::name, "").toString();
        role = role.toLowerCase();
        TrackInfo ti;
        ti.role = role.toStdString();
        ti.kind = roleKindFor(role);
        ti.anyClip = false;
        const auto clipList = track.getChildWithName(IDs::CLIP_LIST);
        if (clipList.isValid())
        {
            for (int c = 0; c < clipList.getNumChildren(); ++c)
            {
                const auto clip = clipList.getChild(c);
                const double cs = static_cast<double>(clip.getProperty(IDs::startTime, 0.0));
                const double ce = cs + static_cast<double>(clip.getProperty(IDs::duration, 0.0));
                if (ce > cs) { ti.anyClip = true; break; }
            }
        }
        tracks.push_back(ti);
    }

    // Per-section sounding roles (clip overlap, seconds conversion).
    std::vector<const ProjectCommands::SongPlanSection*> secPtrs;
    std::vector<std::vector<int>> soundingIdx(plan.sections.size());
    for (std::size_t i = 0; i < plan.sections.size(); ++i)
    {
        const auto& s = plan.sections[i];
        secPtrs.push_back(&s);
        const double s0 = s.startBeat * spb;
        const double s1 = s.endBeat * spb;
        for (int t = 0; t < static_cast<int>(tracks.size()); ++t)
        {
            if (!tracks[t].anyClip) continue;
            const auto track = trackList.getChild(t);
            const auto clipList = track.getChildWithName(IDs::CLIP_LIST);
            if (!clipList.isValid()) continue;
            for (int c = 0; c < clipList.getNumChildren(); ++c)
            {
                const auto clip = clipList.getChild(c);
                const double cs = static_cast<double>(clip.getProperty(IDs::startTime, 0.0));
                const double ce = cs + static_cast<double>(clip.getProperty(IDs::duration, 0.0));
                if (ce > s0 && cs < s1)
                {
                    soundingIdx[i].push_back(t);
                    break;
                }
            }
        }
    }

    bool hasMelodic = false, hasBackbeat = false;
    for (std::size_t i = 0; i < plan.sections.size(); ++i)
    {
        const auto& s = *secPtrs[i];
        StructureSectionAudit sa;
        sa.name = s.name;
        sa.kind = s.kind;
        sa.startBeat = s.startBeat;
        sa.endBeat = s.endBeat;
        sa.bars = s.bars;

        for (const int t : soundingIdx[i])
        {
            const auto& ti = tracks[t];
            if (std::find(sa.soundingRoles.begin(), sa.soundingRoles.end(), ti.role)
                == sa.soundingRoles.end())
            {
                sa.soundingRoles.push_back(ti.role);
                sa.roleKinds.push_back(roleKindName(ti.kind));
            }
            sa.hasMelodic  = sa.hasMelodic  || ti.kind == RoleKind::Melodic;
            sa.hasBackbeat = sa.hasBackbeat || ti.kind == RoleKind::Backbeat;
        }
        audit.sections.push_back(sa);
    }

    // Boredom spans: consecutive NON-build sections with no melodic and no
    // backbeat role, run of >= 8 bars. Later combined with the silent case.
    const auto kindLowerOf = [](const std::string& k) { return juce::String(k).toLowerCase(); };
    // Boredom spans = maximal runs of consecutive NON-build sections that are
    // sparse (no melodic, no backbeat). Build sections are tension markers and
    // BREAK the run — they never join or extend a flagged span.
    std::vector<int> runIdx;
    auto flushRun = [&]() {
        if (runIdx.empty()) return;
        int bars = 0;
        for (const int i : runIdx)
            bars += secPtrs[i]->bars;
        if (bars >= 8)
        {
            StructureSpan span;
            bool hasFloor = false, hasTexture = false, hasOther = false;
            for (const int i : runIdx)
            {
                const auto& sa = audit.sections[static_cast<std::size_t>(i)];
                if (span.startName.empty()) { span.startName = sa.name; span.startBeat = sa.startBeat; }
                span.endName = sa.name;
                span.endBeat = sa.endBeat;
                for (const auto& k : sa.roleKinds)
                {
                    if (k == std::string("floor"))   hasFloor = true;
                    if (k == std::string("texture")) hasTexture = true;
                    if (k == std::string("other"))   hasOther = true;
                }
            }
            span.bars = bars;
            if (!hasFloor && !hasOther && !hasTexture) span.flag = "silence";
            else if (hasFloor && !hasOther)            span.flag = "bass-hat-only";
            else if (!hasFloor && !hasOther)           span.flag = "hats-only";
            else                                       span.flag = "unclassified-roles";
            audit.spans.push_back(std::move(span));
        }
        runIdx.clear();
    };
    for (std::size_t i = 0; i < audit.sections.size(); ++i)
    {
        const auto& sa = audit.sections[i];
        if (isBuildKind(kindLowerOf(sa.kind))) { flushRun(); continue; }
        const bool sparse = !sa.hasMelodic && !sa.hasBackbeat;
        if (sparse) runIdx.push_back(static_cast<int>(i));
        else flushRun();
    }
    flushRun();

    // Drop gates.
    for (std::size_t i = 0; i < audit.sections.size(); ++i)
    {
        const auto& sa = audit.sections[i];
        const auto kl = kindLowerOf(sa.kind);
        if (!isDropKind(kl)) continue;
        audit.anyDrop = true;
        if (audit.firstDropName.empty())
        {
            audit.firstDropName = sa.name;
            audit.firstDropHasMotif = sa.hasMelodic;
        }
        if (!sa.hasBackbeat)
            audit.dropNamesMissingBackbeat.push_back(sa.name);
        // Load proxy: fewer sounding roles than the build that precedes it.
        if (i > 0)
        {
            const auto& prev = audit.sections[i - 1];
            if (isBuildKind(kindLowerOf(prev.kind))
                && sa.soundingRoles.size() < prev.soundingRoles.size())
                audit.dropNamesThinnerThanBuild.push_back(sa.name);
        }
    }

    audit.ok = audit.spans.empty() && audit.dropNamesMissingBackbeat.empty()
        && audit.dropNamesThinnerThanBuild.empty()
        && (!audit.anyDrop || audit.firstDropHasMotif);
    return audit;
}

} // namespace HDAW
