#include "engine/CorpusArranger.h"
#include "engine/MelodyVoicer.h"
#include <algorithm>
#include <random>

namespace HDAW {

namespace {
uint32_t hashUI(uint64_t v) { return static_cast<uint32_t>(v) ^ static_cast<uint32_t>(v >> 32); }
double unit01(std::mt19937& rng) { return static_cast<double>(rng() & 0xFFFFFF) / 16777216.0; }
int rInt(std::mt19937& rng, int lo, int hi) { return lo + static_cast<int>(unit01(rng) * (hi - lo + 1)); }
const char* kIntroNames[4] = { "fourOnFloor", "shortIntro", "midIntro", "longIntro" };
const char* kLenNames[3] = { "short", "mid", "extended" };
constexpr int kMaxNotes = 8192;
} // namespace

const char* CorpusArranger::introName(IntroMode m) { return kIntroNames[static_cast<int>(m)]; }

CorpusPlan CorpusArranger::samplePlan(const CorpusOptions& o)
{
    std::mt19937 rng(hashUI(static_cast<uint64_t>(o.seed)));
    CorpusPlan p;
    p.seed = o.seed;

    struct LM { const char* n; double p; int lo, hi; };
    static const LM lms[3] = { {"short",0.50,28,48}, {"mid",0.25,48,114}, {"extended",0.25,114,159} };
    int lmIdx = (o.lengthMode >= 0 && o.lengthMode <= 2) ? o.lengthMode : 2;
    if (o.lengthMode < 0) { double r = unit01(rng), acc = 0; for (int i = 0; i < 3; ++i) { acc += lms[i].p; if (r < acc) { lmIdx = i; break; } } }
    p.lengthMode = lms[lmIdx].n;
    const int bars = o.bars > 0 ? std::min(o.bars, 256) : rInt(rng, lms[lmIdx].lo, lms[lmIdx].hi);
    p.totalBars = bars;

    static const int introBars[4] = { 0, 2, 8, 16 };
    static const double introP[4] = { 0.30, 0.30, 0.25, 0.15 };
    int im = (o.introMode >= 0 && o.introMode <= 3) ? o.introMode : 3;
    if (o.introMode < 0) { double r = unit01(rng), acc = 0; for (int i = 0; i < 4; ++i) { acc += introP[i]; if (r < acc) { im = i; break; } } }
    p.introMode = kIntroNames[im];
    const int kickEntry = std::min(introBars[im], 24);

    p.constBass = o.constBass >= 0 ? (o.constBass != 0) : (unit01(rng) < 0.37);
    p.lateNovelty = o.lateNovelty >= 0 ? (o.lateNovelty != 0) : (unit01(rng) < 0.20);
    p.breakdown = o.breakdown >= 0 ? (o.breakdown != 0) : (unit01(rng) < 0.05);
    static const char* pool[4] = { "lead", "chord", "arp2", "fx" };
    p.noveltyRole = o.noveltyRole.empty() ? std::string(pool[rInt(rng, 0, 3)]) : o.noveltyRole;
    const int noveltyAt = static_cast<int>((0.45 + unit01(rng) * 0.25) * bars);
    const int breakLen = std::max(3, static_cast<int>(3 + unit01(rng) * 5));

    auto push = [&](const char* name, int len, std::vector<std::string> roles, double dens) {
        if (len < 2) return;
        CorpusSection s; s.name = name; s.bars = len; s.roles = std::move(roles); s.density = dens;
        s.barStart = p.sections.empty() ? 0 : (p.sections.back().barStart + p.sections.back().bars);
        p.sections.push_back(std::move(s));
    };

    push("intro", std::max(kickEntry, 4), {"pad","fx"}, 0.30);
    int cursor = p.sections.back().barStart + p.sections.back().bars;

    const int dropA = std::max(cursor + 8, static_cast<int>(bars * (0.22 + unit01(rng) * 0.12)));
    const int dropAClamped = std::min(std::max(dropA, cursor + 4), static_cast<int>(bars * 0.5));
    if (dropAClamped > cursor + 3) {
        std::vector<std::string> b1 = {"pad","fx","hats"};
        if (kickEntry > 0) b1.push_back("kick");
        if (p.constBass) b1.push_back("bass");
        push("build1", dropAClamped - cursor, b1, 0.55);
    }
    cursor = p.sections.back().barStart + p.sections.back().bars;

    int dropALen = std::min(32, static_cast<int>(bars * 0.20));
    dropALen = std::clamp(dropALen, 12, std::max(12, static_cast<int>(bars / 2) - cursor - 4));
    push("dropA", dropALen, {"kick","bass","hats","snare","clap","arp","stabs","fx"}, 0.85);
    cursor = p.sections.back().barStart + p.sections.back().bars;

    if (p.breakdown && bars >= 48) {
        const int target = static_cast<int>(bars * (0.45 + unit01(rng) * 0.25));
        const int breakStart = std::clamp(target, cursor + 4, bars - 24);
        if (breakStart > cursor + 2) { push("minibreak", breakLen, {"bass","pad"}, 0.45); p.kicklessBreakdown = true; }
    }
    cursor = p.sections.back().barStart + p.sections.back().bars;

    const int build2Len = std::max(4, static_cast<int>(bars * 0.06));
    push("build2", std::min(build2Len, std::max(4, bars - 12 - cursor)), {"kick","bass","hats","arp","fx","riser"}, 0.70);
    cursor = p.sections.back().barStart + p.sections.back().bars;

    std::vector<std::string> dropBRoles = {"kick","bass","hats","snare","clap","arp","stabs","fx"};
    if (p.lateNovelty) { dropBRoles.push_back(p.noveltyRole); (void)noveltyAt; }
    const int remaining = bars - cursor;
    const int outroBudget = std::max(2, std::min(4, remaining / 5));
    const int dropBLen = std::max(2, std::min(static_cast<int>(bars * 0.22), remaining - outroBudget));
    push("dropB", dropBLen, dropBRoles, p.lateNovelty ? 0.95 : 0.85);
    cursor = p.sections.back().barStart + p.sections.back().bars;
    push("outro", bars - cursor, {"bass","pad","fx"}, 0.35);

    return p;
}

PsytranceMarkovScore CorpusArranger::generate(const CorpusParams& par)
{
    PsytranceMarkovScore score;
    CorpusOptions opts = par.opts;
    opts.seed = par.seed;
    if (opts.bars <= 0) opts.bars = par.totalBars;
    const CorpusPlan plan = samplePlan(opts);
    score.totalBeats = plan.totalBars * 4.0;

    auto trackFor = [&](const std::string& r) -> int {
        if (r == "kick") return par.kick;
        if (r == "bass") return par.bass;
        if (r == "hat" || r == "hats") return par.hat;
        if (r == "snare") return par.snare;
        if (r == "clap") return par.clap;
        if (r == "rim") return par.rim;
        if (r == "arp" || r == "arp2" || r == "lead") return par.arp >= 0 ? par.arp : par.lead;
        if (r == "stab" || r == "stabs" || r == "chord") return par.stab;
        if (r == "pad") return par.pad;
        if (r == "riser") return par.riser;
        if (r == "down") return par.down;
        if (r == "fx") return par.lead >= 0 ? par.lead : par.arp;
        return -1;
    };

    std::map<std::string, RoleCtx> ctx;
    auto ensure = [&](const std::string& r) { if (ctx.count(r)) return; RoleCtx c; c.track = trackFor(r); c.clip.role = r; ctx.emplace(r, std::move(c)); };

    for (auto& s : plan.sections)
    {
        const int A = s.barStart, B = s.barStart + s.bars;
        for (const auto& role : s.roles)
        {
            const bool drop = s.name.find("drop") != std::string::npos;
            const bool build = s.name.find("build") != std::string::npos;
            auto emit = [&](const std::string& r, int lo, int hi, auto&& writer) {
                ensure(r); auto& c = ctx[r]; if (c.track < 0) return;
                for (int bar = std::max(0, lo); bar < std::min(hi, plan.totalBars); ++bar) writer(c, bar);
            };
            if (role == "kick") emit("kick", A, B, [](RoleCtx& c, int bar){ for (int b = 0; b < 4; ++b) c.add(bar*4.0+b, 36, 116, 0.18, kMaxNotes); });
            else if (role == "hat" || role == "hats") emit("hats", A, B, [drop](RoleCtx& c, int bar){ for (int k = 0; k < 4; ++k) c.add(bar*4.0+0.5+k, 44, drop?88:76, 0.09, kMaxNotes); });
            else if (role == "snare") emit("snare", A, B, [](RoleCtx& c, int bar){ c.add(bar*4.0+1, 38, 104, 0.12, kMaxNotes); c.add(bar*4.0+3, 38, 100, 0.12, kMaxNotes); });
            else if (role == "clap") emit("clap", A, B, [](RoleCtx& c, int bar){ c.add(bar*4.0+1, 42, 92, 0.10, kMaxNotes); c.add(bar*4.0+3, 42, 88, 0.10, kMaxNotes); });
            else if (role == "riser" && build) emit("riser", A, B, [](RoleCtx& c, int bar){ for (int i = 0; i < 8; ++i) c.add(bar*4.0+i*0.5, 48+i%12, 60+i*7, 0.05, kMaxNotes); });
            else if (role == "down" && drop) emit("down", A, B, [A](RoleCtx& c, int bar){ if (bar == A) c.add(bar*4.0, 45, 100, 0.6, kMaxNotes); });
            else if (role == "fx") emit("fx", A, B, [](RoleCtx& c, int bar){ if (bar % 2 == 0) c.add(bar*4.0, 60 + (bar % 12), 70, 0.04, kMaxNotes); });
        }
    }

    {
        HarmonyEngine harm;
        harm.setProgressions(par.progressionA, par.progressionB);
        std::mt19937 hrng(hashUI(static_cast<uint64_t>(par.seed) ^ 0x5EEDu));
        harm.initKey(par.keyRoot, par.scaleMode, hrng, 0);
        RoleCtx bass, arp, stab, pad;
        bass.track = par.bass; bass.clip.role = "bass";
        arp.track = par.arp; arp.clip.role = "arp";
        stab.track = par.stab; stab.clip.role = "stab";
        pad.track = par.pad; pad.clip.role = "pad";
        HarmonyStyle style;
        const int half = plan.totalBars * 2 / 5;
        bool swapped = false;
        for (int bar = 0; bar + 4 <= plan.totalBars; bar += 4)
        {
            std::set<std::string> active;
            std::set<std::string> activeMelodicRoles;
            for (const auto& s : plan.sections)
                if (bar >= s.barStart && bar < s.barStart + s.bars)
                    for (const auto& r : s.roles)
                    {
                        if (r == "bass") active.insert("bass");
                        else if (r == "arp" || r == "arp2") { active.insert("arp"); activeMelodicRoles.insert("arp"); }
                        else if (r == "lead") { active.insert("arp"); activeMelodicRoles.insert("lead"); }
                        else if (r == "stab" || r == "stabs" || r == "chord") active.insert("stab");
                        else if (r == "pad") active.insert("pad");
                    }
            if (!swapped && plan.lateNovelty && bar >= half) { harm.toggleSwapPattern(); swapped = true; }
            harm.writeWindowNotes(bar, 4, active, style, bass, arp, stab, pad, plan.totalBars, kMaxNotes);

            if (par.melodyCorpusPhraseProb > 0.0 && !activeMelodicRoles.empty() && arp.track >= 0)
            {
                const std::string phraseRole = activeMelodicRoles.count("lead") ? "lead" : "arp";
                const int phraseCount = melodyRoleCount(phraseRole.c_str());
                if (phraseCount > 0 && unit01(hrng) < par.melodyCorpusPhraseProb)
                {
                    const MelodicPhrase* ph = melodyRolePhrase(phraseRole.c_str(), rInt(hrng, 0, phraseCount - 1));
                    const double wStart = bar * 4.0;
                    const double wEnd = wStart + 16.0;
                    arp.clip.notes.erase(
                        std::remove_if(arp.clip.notes.begin(), arp.clip.notes.end(),
                                       [&](const PsytranceNote& n) { return n.startBeat >= wStart && n.startBeat < wEnd; }),
                        arp.clip.notes.end());
                    const auto voiced = voiceMelodyPhrase(*ph, harm.currentKeyRoot(), par.scaleMode,
                                                          MelodyTransposeMode::Diatonic,
                                                          style.arpVelocity, 4, 0.0, hrng);
                    for (const auto& v : voiced)
                        arp.add(wStart + v.startBeat, v.pitch, v.velocity, v.durationBeats, kMaxNotes);
                }
            }
        }
        for (auto* c : { &bass, &arp, &stab, &pad })
            if (c->track >= 0 && !c->clip.notes.empty()) ctx.emplace(c->clip.role, std::move(*c));
    }

    // Chunk roles into <= kChunkNotes-note clips anchored at their first
    // note (workaround: the LIVE routing graph drops note lists above ~226
    // notes per MIDI clip — verified live with both generators; in-process
    // tests never hit it because they lack a live routing graph). Each chunk
    // is its own clip at startBeats; notes are clip-local relative to that.
    constexpr int kChunkNotes = 200;
    for (auto& kv : ctx)
    {
        if (kv.second.track < 0 || kv.second.clip.notes.empty()) { score.skipped.push_back(kv.first); continue; }
        auto& src = kv.second.clip.notes;
        std::stable_sort(src.begin(), src.end(),
            [](const PsytranceNote& a, const PsytranceNote& b) { return a.startBeat < b.startBeat; });
        size_t i = 0;
        while (i < src.size())
        {
            PsytranceClip chunk;
            chunk.role = kv.second.clip.role;
            chunk.trackIndex = kv.second.track;
            const double anchor = src[i].startBeat;
            chunk.startBeats = anchor;
            size_t j = i;
            while (j < src.size() && static_cast<int>(j - i) < kChunkNotes)
            {
                auto n = src[j];
                n.startBeat -= anchor;
                chunk.notes.push_back(std::move(n));
                ++j;
            }
            score.notesTotal += static_cast<int>(chunk.notes.size());
            score.clips.push_back(std::move(chunk));
            i = j;
        }
    }
    if (score.clips.empty()) score.error = "no roles mapped";
    return score;
}

} // namespace HDAW
