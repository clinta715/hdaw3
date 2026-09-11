#include "AudioEngineCommands.h"
#include "AudioEngine.h"
#include "PsytranceGenerator.h"
#include "PhraseGenerator.h"
#include "RhythmPatternGenerator.h"
#include "PatternLibrary.h"
#include "../model/ProjectModel.h"

#include <juce_core/juce_core.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <stdexcept>

// Song plan (plan/cell workflow keystone, docs/plans/2026-09-11):
// deterministic arrangement skeleton + Song Brief interchange. Tree-only —
// no DSP, no audio-graph rebuilds.

namespace {
constexpr double kBeatsPerBar = 4.0; // 4/4 assumption, matches guide §4

juce::String generateRegionID()
{
    static juce::Random rng { juce::Time::currentTimeMillis() };
    return juce::String::toHexString(rng.nextInt64());
}

std::string lowerAscii(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s) out.push_back((char) std::tolower((unsigned char) c));
    return out;
}

// kindFromName maps aliases case-insensitively; anything unknown falls through
// to Other — so a literal "other" is accepted, but a name that merely maps to
// Other is rejected at the trust boundary (Gate 9).
bool knownSectionKind(const std::string& kind)
{
    if (HDAW::PsytranceGenerator::kindFromName(kind) != HDAW::PsytranceSectionKind::Other)
        return true;
    return lowerAscii(kind) == "other";
}

// Brief enum aliases (brief.schema.json) → canonical kinds.
std::string briefTypeToKind(const std::string& type)
{
    const auto lower = lowerAscii(type);
    if (lower == "peak") return "mainA";
    if (lower == "outro") return "finale";
    if (lower == "drop") return "mainB";
    return type;
}

juce::File sectionTemplateDir()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("HDAW").getChildFile("section-templates");
}

juce::String sanitizeTemplateName(const std::string& name)
{
    juce::String out;
    for (char c : name)
    {
        const auto uc = (unsigned char) c;
        if (std::isalnum(uc) || c == '-' || c == '_' || c == ' ')
            out += juce::String::charToString((juce::juce_wchar) uc);
    }
    return out.trim();
}

juce::var planToVar(const ProjectCommands::SongPlanData& plan)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("bpm", plan.bpm);
    obj->setProperty("keyRoot", plan.keyRoot);
    obj->setProperty("scaleMode", plan.scaleMode);
    obj->setProperty("style", juce::String(plan.style));
    obj->setProperty("seed", (double) (long long) plan.seed);
    obj->setProperty("totalBars", plan.totalBars);
    juce::Array<juce::var> sections;
    for (const auto& s : plan.sections)
    {
        auto* so = new juce::DynamicObject();
        so->setProperty("name", juce::String(s.name));
        so->setProperty("kind", juce::String(s.kind));
        so->setProperty("bars", s.bars);
        so->setProperty("startBeat", s.startBeat);
        so->setProperty("endBeat", s.endBeat);
        sections.add(juce::var(so));
    }
    obj->setProperty("sections", sections);
    return juce::var(obj);
}

ProjectCommands::SongPlanData planFromVar(const juce::var& v)
{
    ProjectCommands::SongPlanData plan;
    if (!v.isObject()) return plan;
    plan.bpm = (double) v.getProperty("bpm", 120.0);
    plan.keyRoot = (int) v.getProperty("keyRoot", 0);
    plan.scaleMode = (int) v.getProperty("scaleMode", 1);
    plan.style = v.getProperty("style", "").toString().toStdString();
    plan.seed = (uint64_t) (long long) (double) v.getProperty("seed", 0.0);
    plan.totalBars = (int) v.getProperty("totalBars", 0);
    const auto sections = v.getProperty("sections", {});
    if (sections.isArray())
    {
        for (int i = 0; i < sections.size(); ++i)
        {
            const auto& s = sections[i];
            auto& out = plan.sections.emplace_back();
            out.name = s.getProperty("name", "").toString().toStdString();
            out.kind = s.getProperty("kind", s.getProperty("type", "mainA")).toString().toStdString();
            out.bars = (int) s.getProperty("bars", 8);
        }
    }
    return plan;
}

// Shared validation for EVERY plan entry point (Gate 9). Empty string = ok.
juce::String validatePlan(const ProjectCommands::SongPlanData& plan)
{
    if (plan.sections.empty())
        return "plan must contain at least one section";
    if (plan.bpm < 60.0 || plan.bpm > 200.0)
        return "bpm out of range (60..200)";
    if (plan.keyRoot < 0 || plan.keyRoot > 11)
        return "keyRoot out of range (0..11)";
    if (plan.scaleMode < 0 || plan.scaleMode > 12)
        return "scaleMode out of range (0..12)";
    int barSum = 0;
    for (const auto& s : plan.sections)
    {
        if (s.name.empty())
            return "section name must not be empty";
        if (s.bars <= 0)
            return "section '" + juce::String(s.name) + "' has non-positive bars";
        if (!knownSectionKind(s.kind))
            return "unknown section kind '" + juce::String(s.name) + ":" + juce::String(s.kind) + "'";
        barSum += s.bars;
    }
    if (barSum != plan.totalBars)
        return "totalBars (" + juce::String(plan.totalBars)
               + ") != sum of section bars (" + juce::String(barSum) + ")";
    return {};
}

// Shared write path for setSongPlan / applySongBrief so each public entry
// owns exactly ONE undo transaction (no nesting — JUCE transactions don't
// nest). Validation must happen BEFORE calling this.
void applyPlanWrites(AudioEngine& engine, const ProjectCommands::SongPlanData& plan,
                     ProjectCommands::SongPlanResult& result, juce::UndoManager& um,
                     bool replaceTypedRegions)
{
    auto root = engine.getProjectModel().getTree();

    auto planTree = root.getChildWithName(IDs::SONG_PLAN);
    if (!planTree.isValid())
    {
        planTree = { IDs::SONG_PLAN, {} };
        root.addChild(planTree, -1, &um);
    }
    planTree.setProperty(IDs::spBpm, plan.bpm, &um);
    planTree.setProperty(IDs::spKeyRoot, plan.keyRoot, &um);
    planTree.setProperty(IDs::spScaleMode, plan.scaleMode, &um);
    planTree.setProperty(IDs::spStyle, juce::String(plan.style), &um);
    planTree.setProperty(IDs::spSeed, (double) (long long) plan.seed, &um);
    planTree.setProperty(IDs::spTotalBars, plan.totalBars, &um);

    auto arrangerList = root.getChildWithName(IDs::ARRANGER_LIST);
    if (!arrangerList.isValid())
    {
        arrangerList = { IDs::ARRANGER_LIST, {} };
        root.addChild(arrangerList, -1, &um);
    }

    double beat = 0.0;
    for (const auto& s : plan.sections)
    {
        const double startBeat = beat;
        const double endBeat = beat + s.bars * kBeatsPerBar;
        beat = endBeat;

        juce::ValueTree region;
        for (int i = 0; i < arrangerList.getNumChildren(); ++i)
        {
            auto candidate = arrangerList.getChild(i);
            if (candidate.getProperty(IDs::regionName).toString().equalsIgnoreCase(juce::String(s.name)))
            {
                region = candidate;
                ++result.regionsUpdated;
                break;
            }
        }
        if (!region.isValid())
        {
            region = { IDs::ARRANGER_REGION, {} };
            region.setProperty(IDs::regionID, generateRegionID(), &um);
            region.setProperty(IDs::regionName, juce::String(s.name), &um);
            region.setProperty(IDs::color, 0, &um);
            arrangerList.addChild(region, -1, &um);
            ++result.regionsCreated;
        }
        region.setProperty(IDs::startTime, startBeat, &um);
        region.setProperty(IDs::duration, endBeat - startBeat, &um);
        region.setProperty(IDs::sectionKind, juce::String(s.kind), &um);

        auto& out = result.plan.sections.emplace_back();
        out.name = s.name;
        out.kind = s.kind;
        out.bars = s.bars;
        out.startBeat = startBeat;
        out.endBeat = endBeat;
    }

    result.ok = true;
    result.plan.bpm = plan.bpm;
    result.plan.keyRoot = plan.keyRoot;
    result.plan.scaleMode = plan.scaleMode;
    result.plan.style = plan.style;
    result.plan.seed = plan.seed;
    result.plan.totalBars = plan.totalBars;

    if (!replaceTypedRegions)
    {
        // Incremental semantic: regions outside the plan are NEVER deleted
        // silently (chains may reference them) — surfaced as warnings;
        // removal stays explicit.
        for (int i = 0; i < arrangerList.getNumChildren(); ++i)
        {
            const auto candidate = arrangerList.getChild(i);
            if (!candidate.hasProperty(IDs::sectionKind)) continue;
            const auto nm = candidate.getProperty(IDs::regionName).toString();
            bool inPlan = false;
            for (const auto& s : plan.sections)
                if (nm.equalsIgnoreCase(juce::String(s.name))) { inPlan = true; break; }
            if (!inPlan)
                result.warnings.push_back("region '" + nm.toStdString()
                                          + "' is not part of the plan (left untouched)");
        }
        return;
    }

    // Full-replacement semantic (brief application): a brief defines the
    // whole song, so plan-typed regions outside it are removed — with the
    // same chain-entry cascade as removeArrangerRegion.
    auto chainList = root.getChildWithName(IDs::ARRANGER_CHAIN_LIST);
    for (int i = arrangerList.getNumChildren() - 1; i >= 0; --i)
    {
        auto candidate = arrangerList.getChild(i);
        if (!candidate.hasProperty(IDs::sectionKind)) continue;
        const auto nm = candidate.getProperty(IDs::regionName).toString();
        bool inPlan = false;
        for (const auto& s : plan.sections)
            if (nm.equalsIgnoreCase(juce::String(s.name))) { inPlan = true; break; }
        if (inPlan) continue;

        const auto rid = candidate.getProperty(IDs::regionID).toString();
        if (chainList.isValid())
            for (int c = 0; c < chainList.getNumChildren(); ++c)
            {
                auto chain = chainList.getChild(c);
                for (int e = chain.getNumChildren() - 1; e >= 0; --e)
                {
                    auto entry = chain.getChild(e);
                    if (entry.getProperty(IDs::regionID, "").toString() == rid)
                        chain.removeChild(entry, &um);
                }
            }
        arrangerList.removeChild(candidate, &um);
    }
}
} // namespace

ProjectCommands::SongPlanResult AudioEngineCommands::setSongPlan(const SongPlanData& plan)
{
    SongPlanResult result;
    auto fail = [&result](const juce::String& msg) {
        result.error = msg.toStdString();
        return result;
    };

    // Gate 9: validate everything BEFORE any write.
    if (const auto err = validatePlan(plan); err.isNotEmpty())
        return fail(err);

    beginTransaction("Set song plan");
    applyPlanWrites(engine_, plan, result, engine_.getProjectModel().getUndoManager(), /*replaceTypedRegions=*/false);
    endTransaction();
    return result;
}

ProjectCommands::SongPlanData AudioEngineCommands::getSongPlan() const
{
    SongPlanData data;
    auto planTree = engine_.getProjectModel().getTree().getChildWithName(IDs::SONG_PLAN);
    if (!planTree.isValid()) return data; // sections empty => no plan set
    data.bpm = (double) planTree.getProperty(IDs::spBpm, 120.0);
    data.keyRoot = (int) planTree.getProperty(IDs::spKeyRoot, 0);
    data.scaleMode = (int) planTree.getProperty(IDs::spScaleMode, 1);
    data.style = planTree.getProperty(IDs::spStyle, "").toString().toStdString();
    data.seed = (uint64_t) (long long) (double) planTree.getProperty(IDs::spSeed, 0.0);
    data.totalBars = (int) planTree.getProperty(IDs::spTotalBars, 0);

    auto arrangerList = engine_.getProjectModel().getTree().getChildWithName(IDs::ARRANGER_LIST);
    if (!arrangerList.isValid()) return data;
    std::vector<juce::ValueTree> regions;
    for (int i = 0; i < arrangerList.getNumChildren(); ++i)
        if (arrangerList.getChild(i).hasProperty(IDs::sectionKind))
            regions.push_back(arrangerList.getChild(i));
    std::sort(regions.begin(), regions.end(), [](const juce::ValueTree& a, const juce::ValueTree& b) {
        return (double) a.getProperty(IDs::startTime) < (double) b.getProperty(IDs::startTime);
    });
    for (const auto& r : regions)
    {
        auto& s = data.sections.emplace_back();
        s.name = r.getProperty(IDs::regionName).toString().toStdString();
        s.kind = r.getProperty(IDs::sectionKind).toString().toStdString();
        s.startBeat = (double) r.getProperty(IDs::startTime);
        s.endBeat = s.startBeat + (double) r.getProperty(IDs::duration);
        s.bars = (int) std::lround((s.endBeat - s.startBeat) / kBeatsPerBar);
    }
    return data;
}

bool AudioEngineCommands::saveSectionTemplate(const std::string& name, std::string* error)
{
    auto fail = [error](const juce::String& msg) {
        if (error) *error = msg.toStdString();
        return false;
    };
    const juce::String safe = sanitizeTemplateName(name);
    if (safe.isEmpty())
        return fail("template name must contain alphanumeric characters");

    const auto plan = getSongPlan();
    if (plan.sections.empty())
        return fail("no song plan set — nothing to save");

    auto dir = sectionTemplateDir();
    if (!dir.exists())
        dir.createDirectory();

    const juce::File file = dir.getChildFile(safe + ".json");
    const juce::String json = juce::JSON::toString(planToVar(plan), true);
    if (!file.replaceWithText(json))
        return fail("failed to write template file: " + file.getFullPathName());
    return true;
}

ProjectCommands::SongPlanData AudioEngineCommands::loadSectionTemplate(const std::string& name, std::string* error)
{
    auto fail = [error](const juce::String& msg) {
        if (error) *error = msg.toStdString();
        return SongPlanData {};
    };
    const juce::String safe = sanitizeTemplateName(name);
    if (safe.isEmpty())
        return fail("template name must contain alphanumeric characters");

    const juce::File file = sectionTemplateDir().getChildFile(safe + ".json");
    if (!file.existsAsFile())
        return fail("template not found: " + safe);

    const auto parsed = juce::JSON::parse(file);
    auto plan = planFromVar(parsed);
    if (plan.sections.empty())
        return fail("file is not a section template: " + safe);
    return plan;
}

std::vector<std::string> AudioEngineCommands::listSectionTemplates() const
{
    std::vector<std::string> names;
    auto dir = sectionTemplateDir();
    if (!dir.exists()) return names;
    for (const auto& f : dir.findChildFiles(juce::File::findFiles, false, "*.json"))
        names.push_back(f.getFileNameWithoutExtension().toStdString());
    std::sort(names.begin(), names.end());
    return names;
}

ProjectCommands::SongPlanResult AudioEngineCommands::applySongBrief(const std::string& briefJson)
{
    SongPlanResult result;
    auto fail = [&result](const juce::String& msg) {
        result.error = msg.toStdString();
        return result;
    };

    const auto parsed = juce::JSON::parse(juce::String(briefJson));
    if (!parsed.isObject())
        return fail("brief is not a JSON object");

    SongPlanData plan;
    plan.bpm = (double) parsed.getProperty("bpm", 120.0);
    plan.keyRoot = (int) parsed.getProperty("keyRoot", 0);
    plan.scaleMode = (int) parsed.getProperty("scaleMode", 1);
    plan.style = parsed.getProperty("style", "").toString().toStdString();
    plan.seed = (uint64_t) (long long) (double) parsed.getProperty("seed", 0.0);
    plan.totalBars = (int) parsed.getProperty("totalBars", 0);

    const auto sections = parsed.getProperty("sections", {});
    if (!sections.isArray() || sections.size() == 0)
        return fail("brief.sections must be a non-empty array");
    int barSum = 0;
    for (int i = 0; i < sections.size(); ++i)
    {
        const auto& s = sections[i];
        auto& out = plan.sections.emplace_back();
        out.name = s.getProperty("name", "").toString().toStdString();
        out.kind = briefTypeToKind(s.getProperty("type", "mainA").toString().toStdString());
        out.bars = (int) s.getProperty("bars", 8);
        barSum += out.bars;
    }
    if (plan.totalBars == 0)
        plan.totalBars = barSum;

    // Gate 9: same validation contract as setSongPlan, BEFORE any write.
    if (const auto err = validatePlan(plan); err.isNotEmpty())
        return fail(err);

    auto& um = engine_.getProjectModel().getUndoManager();
    beginTransaction("Apply song brief");
    applyPlanWrites(engine_, plan, result, um, /*replaceTypedRegions=*/true);
    // Exact brief round-trip: export returns the applied brief verbatim.
    auto planTree = engine_.getProjectModel().getTree().getChildWithName(IDs::SONG_PLAN);
    if (planTree.isValid())
        planTree.setProperty(IDs::spBriefJson, juce::String(briefJson), &um);
    endTransaction();
    return result;
}

std::string AudioEngineCommands::exportSongBrief(std::string* error) const
{
    auto planTree = engine_.getProjectModel().getTree().getChildWithName(IDs::SONG_PLAN);
    if (!planTree.isValid())
    {
        if (error) *error = "no song plan set";
        return {};
    }
    // Verbatim when the plan came from a brief; synthesized otherwise.
    auto stored = planTree.getProperty(IDs::spBriefJson, "").toString();
    if (stored.isNotEmpty())
        return stored.toStdString();

    const auto plan = getSongPlan();
    return juce::JSON::toString(planToVar(plan), true).toStdString();
}

// ══════════════════════════════ Cells (Phase C) ═══════════════════════════
// Recipes execute through the SAME engine generators the standalone tools use;
// the clip spans exactly its section window; provenance rides on the clip.

namespace {

constexpr int kCellNoteCap = 8191; // MidiClipProcessor ceiling (place_patterns parity)

uint64_t deriveCellSeed(uint64_t planSeed, const std::string& section,
                        const std::string& role)
{
    // FNV-1a over planSeed+keys — deterministic variation slot per cell.
    // Masked to 53 bits: full uint64 seeds do not survive the double/JSON
    // round-trip (ValueTree property + wire) exactly, and values >= 2^63
    // flip sign through (long long). 53 bits is exact everywhere.
    uint64_t h = planSeed ? planSeed : 1469598103934665603ull;
    const std::string key = section + ":" + role;
    for (unsigned char c : key) { h ^= c; h *= 1099511628211ull; }
    h &= 0x1FFFFFFFFFFFFFull;
    return h ? h : 1;
}

// Accepts BOTH the API enum identifiers ("BassLine", used by the router/MCP
// style maps) and the display names ("Bass Line") — matched by stripping
// spaces/hyphens/underscores/& case-insensitively.
juce::String normStyle(const juce::String& s)
{
    juce::String out;
    for (juce::String::CharPointerType p = s.getCharPointer(); p.isNotEmpty(); /* advanced by getAndAdvance */)
    {
        const auto c = p.getAndAdvance();
        if (c != L' ' && c != L'-' && c != L'&' && c != L'_') out += c;
    }
    return out;
}

bool phraseStyleFromName(const juce::String& name, PhraseGenerator::Style& out)
{
    const auto want = normStyle(name);
    for (int i = 0; i < (int) PhraseGenerator::NumStyles; ++i)
        if (const char* n = PhraseGenerator::styleName((PhraseGenerator::Style) i))
            if (normStyle(n).equalsIgnoreCase(want)) { out = (PhraseGenerator::Style) i; return true; }
    return false;
}

int paramI(const juce::var& p, const char* k, int def) { return p.hasProperty(k) ? (int) (double) p.getProperty(k, def) : def; }
double paramD(const juce::var& p, const char* k, double def) { return p.hasProperty(k) ? (double) p.getProperty(k, def) : def; }
bool paramB(const juce::var& p, const char* k, bool def) { return p.hasProperty(k) ? (bool) p.getProperty(k, def) : def; }
juce::String paramS(const juce::var& p, const char* k, const juce::String& def) { return p.hasProperty(k) ? p.getProperty(k, def).toString() : def; }

void applyStyleParams(PhraseGenerator::PhraseParams& pp, const juce::var& sp)
{
    if (!sp.isObject()) return;
    pp.trapHiHat.ratchetChance        = paramD(sp, "ratchetChance", pp.trapHiHat.ratchetChance);
    pp.aleatoric.restProbability      = paramD(sp, "restProbability", pp.aleatoric.restProbability);
    pp.callResponse.responseVariation = paramD(sp, "responseVariation", pp.callResponse.responseVariation);
    pp.swingComping.swingPercent      = paramI(sp, "swingPercent", pp.swingComping.swingPercent);
    pp.markovMelody.rhythmGrid        = paramI(sp, "rhythmGrid", pp.markovMelody.rhythmGrid);
    pp.markovMelody.stateCount        = paramI(sp, "stateCount", pp.markovMelody.stateCount);
}

void overridePhraseFields(PhraseGenerator::PhraseParams& pp, const juce::var& p)
{
    if (!p.isObject()) return;
    if (p.hasProperty("style"))
    {
        PhraseGenerator::Style st;
        if (phraseStyleFromName(p.getProperty("style", "").toString(), st)) pp.style = st;
    }
    pp.lengthBeats  = paramD(p, "lengthBeats", pp.lengthBeats);
    pp.density      = paramI(p, "density", pp.density);
    pp.noteDuration = paramD(p, "noteDuration", pp.noteDuration);
    pp.scaleRoot    = paramI(p, "scaleRoot", pp.scaleRoot);
    pp.scaleMode    = paramI(p, "scaleMode", pp.scaleMode);
    pp.lowNote      = paramI(p, "lowNote", pp.lowNote);
    pp.highNote     = paramI(p, "highNote", pp.highNote);
    pp.minVelocity  = paramI(p, "minVelocity", pp.minVelocity);
    pp.maxVelocity  = paramI(p, "maxVelocity", pp.maxVelocity);
    applyStyleParams(pp, p.getProperty("styleParams", {}));
}

HDAW::PatternLibrary& cellPatternLib()
{
    static HDAW::PatternLibrary lib(
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("HDAW").getChildFile("patterns"));
    return lib;
}

bool knownSourceKind(const std::string& kind)
{
    static const std::vector<std::string> kSources = { "phrase", "rhythm", "break", "pattern", "harvest" };
    return std::find(kSources.begin(), kSources.end(), kind) != kSources.end();
}

} // namespace

bool AudioEngineCommands::setCellRecipe(const CellRecipe& recipe, std::string* error)
{
    auto fail = [&](const juce::String& msg) {
        if (error) *error = msg.toStdString();
        return false;
    };

    // Gate 9: validate against the plan BEFORE writing anything.
    const auto plan = getSongPlan();
    if (plan.sections.empty())
        return fail("no song plan set — call set_song_plan first");
    bool sectionFound = false;
    for (const auto& s : plan.sections)
        if (juce::String(s.name).equalsIgnoreCase(juce::String(recipe.section))) { sectionFound = true; break; }
    if (!sectionFound)
        return fail("unknown section '" + juce::String(recipe.section) + "'");
    if (recipe.role.empty())
        return fail("role must not be empty");
    if (recipe.trackId < 0)
        return fail("trackId required");
    if (!knownSourceKind(recipe.sourceKind))
        return fail("unknown sourceKind '" + juce::String(recipe.sourceKind)
                    + "' (phrase|rhythm|break|pattern|harvest)");

    juce::var params;
    if (!recipe.paramsJson.empty())
    {
        params = juce::JSON::parse(juce::String(recipe.paramsJson));
        if (!params.isObject())
            return fail("params must be a JSON object");
    }
    if (recipe.sourceKind == "phrase" && params.isObject() && params.hasProperty("style"))
    {
        PhraseGenerator::Style st;
        if (!phraseStyleFromName(params.getProperty("style", "").toString(), st))
            return fail("unknown phrase style '" + params.getProperty("style", "").toString() + "'");
    }
    if (recipe.sourceKind == "break" && params.isObject() && params.hasProperty("style"))
    {
        BreakPatternGenerator::Style bs;
        if (!BreakPatternGenerator::styleFromName(params.getProperty("style", "").toString().toStdString(), bs))
            return fail("unknown break style '" + params.getProperty("style", "").toString() + "'");
    }
    if (recipe.sourceKind == "pattern" && (!params.isObject() || !params.hasProperty("patternId")))
        return fail("pattern cell requires params.patternId");
    if (recipe.sourceKind == "harvest")
    {
        if (!params.isObject() || !params.getProperty("notes", {}).isArray())
            return fail("harvest cell requires params.notes array");
    }

    auto& um = engine_.getProjectModel().getUndoManager();
    auto planTree = engine_.getProjectModel().getTree().getChildWithName(IDs::SONG_PLAN);
    if (!planTree.isValid())
        return fail("no song plan set");

    beginTransaction("Set cell recipe");
    auto cells = planTree.getChildWithName(IDs::CELLS);
    if (!cells.isValid())
    {
        cells = { IDs::CELLS, {} };
        planTree.addChild(cells, -1, &um);
    }
    juce::ValueTree node;
    for (int i = 0; i < cells.getNumChildren(); ++i)
    {
        auto c = cells.getChild(i);
        if (c.getProperty(IDs::cellSection).toString().equalsIgnoreCase(juce::String(recipe.section))
            && c.getProperty(IDs::cellRole).toString().equalsIgnoreCase(juce::String(recipe.role)))
        { node = c; break; }
    }
    const bool isNew = !node.isValid();
    if (isNew)
    {
        node = { IDs::CELL, {} };
        cells.addChild(node, -1, &um);
    }
    node.setProperty(IDs::cellSection, juce::String(recipe.section), &um);
    node.setProperty(IDs::cellRole, juce::String(recipe.role), &um);
    node.setProperty(IDs::cellTrack, recipe.trackId, &um);
    node.setProperty(IDs::cellSource, juce::String(recipe.sourceKind), &um);
    node.setProperty(IDs::cellParams, juce::String(recipe.paramsJson), &um);
    node.setProperty(IDs::cellSeed, (double) (long long) recipe.seed, &um);
    node.setProperty(IDs::cellLocked, recipe.locked, &um);
    if (isNew || !node.hasProperty(IDs::cellLastClipId)) node.setProperty(IDs::cellLastClipId, -1, &um);
    if (isNew || !node.hasProperty(IDs::cellLastSeed))    node.setProperty(IDs::cellLastSeed, 0.0, &um);
    endTransaction();
    return true;
}

std::vector<ProjectCommands::CellRecipe> AudioEngineCommands::getCells() const
{
    std::vector<CellRecipe> out;
    auto planTree = engine_.getProjectModel().getTree().getChildWithName(IDs::SONG_PLAN);
    if (!planTree.isValid()) return out;
    auto cells = planTree.getChildWithName(IDs::CELLS);
    for (int i = 0; i < cells.getNumChildren(); ++i)
    {
        const auto c = cells.getChild(i);
        auto& r = out.emplace_back();
        r.section    = c.getProperty(IDs::cellSection, "").toString().toStdString();
        r.role       = c.getProperty(IDs::cellRole, "").toString().toStdString();
        r.trackId    = (int) c.getProperty(IDs::cellTrack, -1);
        r.sourceKind = c.getProperty(IDs::cellSource, "").toString().toStdString();
        r.paramsJson = c.getProperty(IDs::cellParams, "").toString().toStdString();
        r.seed       = (uint64_t) (long long) (double) c.getProperty(IDs::cellSeed, 0.0);
        r.locked     = (bool) c.getProperty(IDs::cellLocked, false);
        r.lastClipId = (int) c.getProperty(IDs::cellLastClipId, -1);
        r.lastSeed   = (uint64_t) (long long) (double) c.getProperty(IDs::cellLastSeed, 0.0);
    }
    return out;
}

bool AudioEngineCommands::removeCellRecipe(const std::string& section, const std::string& role)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto planTree = engine_.getProjectModel().getTree().getChildWithName(IDs::SONG_PLAN);
    auto cells = planTree.getChildWithName(IDs::CELLS);
    for (int i = 0; i < cells.getNumChildren(); ++i)
    {
        auto c = cells.getChild(i);
        if (c.getProperty(IDs::cellSection).toString().equalsIgnoreCase(juce::String(section))
            && c.getProperty(IDs::cellRole).toString().equalsIgnoreCase(juce::String(role)))
        {
            beginTransaction("Remove cell recipe");
            cells.removeChild(c, &um);
            endTransaction();
            return true;
        }
    }
    return false;
}

ProjectCommands::CellFillResult AudioEngineCommands::fillOneCell(const CellRecipe& cell,
                                                                 const SongPlanData& plan)
{
    CellFillResult res;
    res.section = cell.section;
    res.role = cell.role;
    res.trackId = cell.trackId;
    auto& um = engine_.getProjectModel().getUndoManager();

    const SongPlanSection* sec = nullptr;
    for (const auto& s : plan.sections)
        if (juce::String(s.name).equalsIgnoreCase(juce::String(cell.section))) { sec = &s; break; }
    if (!sec) { res.error = "unknown section '" + cell.section + "'"; return res; }

    const double winBeats = sec->endBeat - sec->startBeat;
    const uint64_t seedUsed = cell.seed ? cell.seed
        : deriveCellSeed(plan.seed, sec->name, cell.role);
    res.seedUsed = seedUsed;

    const juce::var params = cell.paramsJson.empty()
        ? juce::var() : juce::JSON::parse(juce::String(cell.paramsJson));

    // Reuse the previous clip when it still exists (content replaced);
    // otherwise mint one that spans exactly the section window.
    int clipId = cell.lastClipId;
    int ownerTrack = -1;
    auto clipNode = (clipId >= 0) ? findClipById(clipId, ownerTrack) : juce::ValueTree();
    if (clipId < 0 || !clipNode.isValid() || ownerTrack != cell.trackId)
    {
        clipId = addMidiClip(cell.trackId, sec->startBeat, winBeats,
                             cell.section + " " + cell.role);
        clipNode = findClipById(clipId, ownerTrack);
    }
    else
    {
        clearNotes(clipId);
    }
    res.clipId = clipId;

    int noteCount = 0;
    auto addGuarded = [&](int pitch, int vel, double start, double dur) {
        if (noteCount < kCellNoteCap) addNote(clipId, pitch, vel, start, dur);
        ++noteCount;
    };

    if (cell.sourceKind == "phrase" || cell.sourceKind == "pattern")
    {
        PhraseGenerator::PhraseParams pp;
        pp.seed = seedUsed;
        pp.lengthBeats = winBeats;
        pp.scaleRoot = plan.keyRoot;
        pp.scaleMode = plan.scaleMode;
        if (cell.sourceKind == "pattern")
        {
            HDAW::PatternPreset preset;
            juce::String perr;
            const auto pid = paramS(params, "patternId", "");
            if (pid.isEmpty()) { res.error = "pattern cell requires params.patternId"; return res; }
            if (!cellPatternLib().loadPattern(pid, preset, perr))
            { res.error = "pattern load failed: " + perr.toStdString(); return res; }
            if (preset.paramsJson.isNotEmpty())
                overridePhraseFields(pp, juce::JSON::parse(preset.paramsJson));
            if (preset.styleParamsJson.isNotEmpty())
                applyStyleParams(pp, juce::JSON::parse(preset.styleParamsJson));
            if (preset.style.isNotEmpty())
            {
                PhraseGenerator::Style st;
                if (phraseStyleFromName(preset.style, st)) pp.style = st;
            }
        }
        overridePhraseFields(pp, params);
        pp.seed = seedUsed;   // seed authority stays the cell's
        for (const auto& n : PhraseGenerator::generatePhrase(pp))
            addGuarded(n.noteNumber, n.velocity, n.startBeat, n.durationBeats);
    }
    else if (cell.sourceKind == "rhythm")
    {
        RhythmPatternGenerator::Params rp;
        rp.grid = paramI(params, "grid", 16);
        rp.bars = paramI(params, "bars", std::max(1, sec->bars));
        rp.pulseA = paramI(params, "pulseA", 4);
        rp.pulseB = paramI(params, "pulseB", 3);
        rp.rotationA = paramI(params, "rotationA", 1);
        rp.rotationB = paramI(params, "rotationB", 1);
        rp.pitchA = paramI(params, "pitchA", 36);
        rp.pitchB = paramI(params, "pitchB", 42);
        rp.velocityA = paramI(params, "velocityA", 112);
        rp.velocityB = paramI(params, "velocityB", 96);
        rp.dsl = paramS(params, "dsl", "").toStdString();
        rp.dslPitch = paramI(params, "dslPitch", 39);
        rp.dslVelocity = paramI(params, "dslVelocity", 104);
        std::vector<RhythmPatternGenerator::Note> notes;
        try { notes = RhythmPatternGenerator::generate(rp); }
        catch (const std::invalid_argument& e) { res.error = e.what(); return res; }
        for (const auto& n : notes)
            addGuarded(n.pitch, n.velocity, n.startBeat, n.durationBeats);
    }
    else if (cell.sourceKind == "break")
    {
        BreakPatternParams bp;
        bp.trackIndex = cell.trackId;
        bp.slotIndex = paramI(params, "slotIndex", 0);
        bp.clipId = clipId;
        BreakPatternGenerator::Style bs;
        if (!BreakPatternGenerator::styleFromName(paramS(params, "style", "amen").toStdString(), bs))
        { res.error = "unknown break style"; return res; }
        bp.style = bs;
        bp.bars = paramI(params, "bars", std::max(1, sec->bars));
        bp.grid = paramI(params, "grid", 4);
        bp.dropFirst = paramB(params, "dropFirst", false);
        bp.ghostFills = paramI(params, "ghostFills", bs == BreakPatternGenerator::Style::JungleEdit ? 1 : 0);
        bp.velocityMin = paramI(params, "velocityMin", 60);
        bp.velocityMax = paramI(params, "velocityMax", 100);
        bp.seed = seedUsed;
        const auto br = generateChoppedBreak(bp);
        if (!br.ok) { res.error = br.error; return res; }
        noteCount = br.added;
    }
    else if (cell.sourceKind == "harvest")
    {
        const auto notes = params.getProperty("notes", {});
        if (!notes.isArray()) { res.error = "harvest cell requires params.notes array"; return res; }
        for (int i = 0; i < notes.size(); ++i)
        {
            const auto n = notes[i];
            addGuarded((int) (double) n.getProperty("pitch", 60),
                       (int) (double) n.getProperty("velocity", 100),
                       (double) n.getProperty("startBeat", 0.0),
                       (double) n.getProperty("durationBeats", 0.25));
        }
    }
    else
    {
        res.error = "unknown sourceKind '" + cell.sourceKind + "'";
        return res;
    }

    // Provenance on the clip + cell bookkeeping (re-fill targets the same clip).
    if (clipNode.isValid())
    {
        clipNode.setProperty(IDs::genTool, "fillCells", &um);
        clipNode.setProperty(IDs::genSource, juce::String(cell.sourceKind), &um);
        clipNode.setProperty(IDs::genSeed, (double) (long long) seedUsed, &um);
        clipNode.setProperty(IDs::genParams, juce::String(cell.paramsJson), &um);
    }
    auto planTree = engine_.getProjectModel().getTree().getChildWithName(IDs::SONG_PLAN);
    auto cellNodes = planTree.getChildWithName(IDs::CELLS);
    for (int i = 0; i < cellNodes.getNumChildren(); ++i)
    {
        auto c = cellNodes.getChild(i);
        if (c.getProperty(IDs::cellSection).toString().equalsIgnoreCase(juce::String(cell.section))
            && c.getProperty(IDs::cellRole).toString().equalsIgnoreCase(juce::String(cell.role)))
        {
            c.setProperty(IDs::cellLastClipId, clipId, &um);
            c.setProperty(IDs::cellLastSeed, (double) (long long) seedUsed, &um);
            break;
        }
    }

    res.noteCount = noteCount;
    res.ok = true;
    return res;
}

ProjectCommands::CellFillBatchResult AudioEngineCommands::fillCells(const std::string& mode)
{
    CellFillBatchResult batch;
    if (mode != "all" && mode != "unfilled")
    { batch.error = "mode must be all|unfilled"; return batch; }
    const auto plan = getSongPlan();
    if (plan.sections.empty())
    { batch.error = "no song plan set"; return batch; }

    // Clip add/update flows through the tree listeners' incremental routing
    // path (lesson 6) — no explicit graph rebuild here, matching every other
    // clip command.
    beginTransaction("Fill cells");
    for (const auto& cell : getCells())
    {
        if (cell.locked) { ++batch.skippedLocked; continue; }
        if (mode == "unfilled" && cell.lastClipId >= 0) continue;
        const auto res = fillOneCell(cell, plan);
        batch.cells.push_back(res);
        if (res.ok) ++batch.filled; else ++batch.failed;
    }
    endTransaction();

    batch.ok = (batch.failed == 0);
    return batch;
}

ProjectCommands::CellFillBatchResult AudioEngineCommands::rerollCells(const std::string& section,
                                                                      const std::string& role)
{
    CellFillBatchResult batch;
    const auto plan = getSongPlan();
    if (plan.sections.empty())
    { batch.error = "no song plan set"; return batch; }

    std::vector<CellRecipe> todo;
    for (auto cell : getCells())
    {
        if (!section.empty() && !juce::String(cell.section).equalsIgnoreCase(juce::String(section))) continue;
        if (!role.empty() && !juce::String(cell.role).equalsIgnoreCase(juce::String(role))) continue;
        if (cell.locked) { ++batch.skippedLocked; continue; }
        // Bump: last used seed + 1 (or derive once, then +1 on next reroll).
        const uint64_t base = cell.lastSeed ? cell.lastSeed
            : (cell.seed ? cell.seed : deriveCellSeed(plan.seed, cell.section, cell.role));
        cell.seed = base + 1;
        todo.push_back(cell);
    }

    beginTransaction("Reroll cells");
    auto& um = engine_.getProjectModel().getUndoManager();
    auto planTree = engine_.getProjectModel().getTree().getChildWithName(IDs::SONG_PLAN);
    auto cellNodes = planTree.getChildWithName(IDs::CELLS);
    for (auto& cell : todo)
    {
        for (int i = 0; i < cellNodes.getNumChildren(); ++i)
        {
            auto c = cellNodes.getChild(i);
            if (c.getProperty(IDs::cellSection).toString().equalsIgnoreCase(juce::String(cell.section))
                && c.getProperty(IDs::cellRole).toString().equalsIgnoreCase(juce::String(cell.role)))
                c.setProperty(IDs::cellSeed, (double) (long long) cell.seed, &um);
        }
        const auto res = fillOneCell(cell, plan);
        batch.cells.push_back(res);
        if (res.ok) ++batch.filled; else ++batch.failed;
    }
    endTransaction();
    batch.ok = (batch.failed == 0);
    return batch;
}

std::string AudioEngineCommands::getClipProvenance(int clipId) const
{
    int track = -1;
    const auto node = findClipById(clipId, track);
    if (!node.isValid()) return {};
    auto* obj = new juce::DynamicObject();
    obj->setProperty("clipId", clipId);
    if (!node.hasProperty(IDs::genTool))
    {
        obj->setProperty("found", false);
        return juce::JSON::toString(juce::var(obj), true).toStdString();
    }
    obj->setProperty("found", true);
    obj->setProperty("tool", node.getProperty(IDs::genTool, "").toString());
    obj->setProperty("source", node.getProperty(IDs::genSource, "").toString());
    obj->setProperty("seed", (double) (long long) (double) node.getProperty(IDs::genSeed, 0.0));
    obj->setProperty("params", node.getProperty(IDs::genParams, "").toString());
    return juce::JSON::toString(juce::var(obj), true).toStdString();
}

