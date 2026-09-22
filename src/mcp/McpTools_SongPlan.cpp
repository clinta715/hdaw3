#include "McpTools.h"
#include "McpTools_Private.h"
#include "McpServer.h"
#include "McpToolDef.h"
#include "../engine/AudioEngine.h"
#include "../engine/SongStructureAudit.h"
#include "../common/ProjectCommands.h"
#include "../common/SongPlanView.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <vector>

// Song plan tools (plan/cell workflow keystone, docs/plans/2026-09-11):
// deterministic arrangement skeleton + Song Brief interchange + section
// templates. Composition verbs consume the plan by section NAME — no beat
// math at the call site.

namespace mcp {

namespace {

QJsonObject sectionJson(const ProjectCommands::SongPlanSection& s)
{
    return QJsonObject{ { "name", QString::fromStdString(s.name) },
                        { "kind", QString::fromStdString(s.kind) },
                        { "bars", s.bars },
                        { "startBeat", s.startBeat },
                        { "endBeat", s.endBeat } };
}

QJsonObject planEchoJson(const ProjectCommands::SongPlanResult& r)
{
    QJsonArray sections;
    for (const auto& s : r.plan.sections) sections.append(sectionJson(s));
    QJsonArray warnings;
    for (const auto& w : r.warnings) warnings.append(QString::fromStdString(w));
    return QJsonObject{ { "ok", true },
                        { "bpm", r.plan.bpm },
                        { "keyRoot", r.plan.keyRoot },
                        { "scaleMode", r.plan.scaleMode },
                        { "style", QString::fromStdString(r.plan.style) },
                        { "seed", (double) (long long) r.plan.seed },
                        { "totalBars", r.plan.totalBars },
                        { "regionsCreated", r.regionsCreated },
                        { "regionsUpdated", r.regionsUpdated },
                        { "warnings", warnings },
                        { "sections", sections } };
}

QJsonObject planDataJson(const ProjectCommands::SongPlanData& plan, bool hasPlan)
{
    QJsonArray sections;
    for (const auto& s : plan.sections) sections.append(sectionJson(s));
    return QJsonObject{ { "ok", true },
                        { "hasPlan", hasPlan },
                        { "bpm", plan.bpm },
                        { "keyRoot", plan.keyRoot },
                        { "scaleMode", plan.scaleMode },
                        { "style", QString::fromStdString(plan.style) },
                        { "seed", (double) (long long) plan.seed },
                        { "totalBars", plan.totalBars },
                        { "sections", sections } };
}

ProjectCommands::SongPlanData planFromArgs(const QJsonObject& a, bool* ok, QString* err)
{
    *ok = true;
    ProjectCommands::SongPlanData plan;
    plan.bpm = a.value("bpm").toDouble(120.0);
    plan.keyRoot = a.value("keyRoot").toInt(0);
    plan.scaleMode = a.value("scaleMode").toInt(1);
    plan.style = a.value("style").toString().toStdString();
    plan.seed = (uint64_t) (long long) a.value("seed").toDouble(0);
    plan.totalBars = a.value("totalBars").toInt(0);
    const auto secs = a.value("sections");
    if (!secs.isArray())
    {
        *ok = false;
        *err = "sections array required (each: {name, kind, bars})";
        return plan;
    }
    for (const auto& sv : secs.toArray())
    {
        const auto so = sv.toObject();
        auto& s = plan.sections.emplace_back();
        s.name = so.value("name").toString().toStdString();
        s.kind = so.value("kind").toString().toStdString();
        s.bars = so.value("bars").toInt(8);
    }
    return plan;
}

} // namespace

void registerSongPlanTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({ "set_song_plan",
        "Set the SONG PLAN: the deterministic arrangement skeleton (sections: name + kind + bars) "
        "plus key/tempo/style/seed. Sections are 4/4; totalBars must equal the sum of section bars. "
        "Creates/updates section-typed arranger regions (matched by name) in ONE undoable step — "
        "composition verbs then reference sections by name. kind is one of: intro, build, mainA, mini, "
        "mainB, breakdown, finale, other (case-insensitive). Returns the resolved plan.",
        objSchema({ { "bpm", QJsonObject{ { "type", "number" } } },
                    { "keyRoot", QJsonObject{ { "type", "integer" }, { "minimum", 0 }, { "maximum", 11 } } },
                    { "scaleMode", QJsonObject{ { "type", "integer" }, { "minimum", 0 }, { "maximum", 12 } } },
                    { "style", QJsonObject{ { "type", "string" } } },
                    { "seed", QJsonObject{ { "type", "integer" }, { "minimum", 0 } } },
                    { "totalBars", QJsonObject{ { "type", "integer" }, { "minimum", 1 } } },
                    { "sections", QJsonObject{ { "type", "array" }, { "items", QJsonObject{
                        { "type", "object" },
                        { "properties", QJsonObject{
                            { "name", QJsonObject{ { "type", "string" } } },
                            { "kind", QJsonObject{ { "type", "string" } } },
                            { "bars", QJsonObject{ { "type", "integer" }, { "minimum", 1 } } } } },
                        { "required", QJsonArray{ "name", "kind", "bars" } } } } } } },
                   { "totalBars", "sections" }),
        "composition",
        [e](const QJsonObject& a) -> McpToolResult {
            bool ok = false; QString err;
            auto plan = planFromArgs(a, &ok, &err);
            if (!ok) return McpToolResult::text(err, true);
            auto r = e->getProjectCommands().setSongPlan(plan);
            if (!r.ok) return McpToolResult::text(QString::fromStdString(r.error), true);
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(planEchoJson(r)).toJson(QJsonDocument::Compact)));
        } });

    s.registerTool({ "get_song_plan",
        "Read the CURRENT song plan (sections with resolved start/end beats, kind, bars) — hasPlan=false "
        "when none is set. Composition tools reference sections by the names returned here.",
        objSchema({}, {}),
        "composition",
        [e](const QJsonObject&) -> McpToolResult {
            auto plan = e->getProjectCommands().getSongPlan();
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(planDataJson(plan, !plan.sections.empty())).toJson(QJsonDocument::Compact)));
        } });

    s.registerTool({ "save_section_template",
        "Save the CURRENT song plan as a named, reusable section template (JSON under AppData/HDAW/"
        "section-templates). Reusable skeletons like 'full-on-v4' or 'dark-138'.",
        objSchema({ { "name", QJsonObject{ { "type", "string" } } } }, { "name" }),
        "composition",
        [e](const QJsonObject& a) -> McpToolResult {
            const std::string name = a.value("name").toString().toStdString();
            std::string err;
            if (!e->getProjectCommands().saveSectionTemplate(name, &err))
                return McpToolResult::text(QString::fromStdString(err), true);
            return McpToolResult::text(QString("ok: template '%1' saved").arg(QString::fromStdString(name)));
        } });

    s.registerTool({ "load_section_template",
        "Load a section template and RETURN its plan data (does NOT apply it — feed it to set_song_plan "
        "to apply).",
        objSchema({ { "name", QJsonObject{ { "type", "string" } } } }, { "name" }),
        "composition",
        [e](const QJsonObject& a) -> McpToolResult {
            const std::string name = a.value("name").toString().toStdString();
            std::string err;
            auto plan = e->getProjectCommands().loadSectionTemplate(name, &err);
            if (plan.sections.empty())
                return McpToolResult::text(QString::fromStdString(err.empty() ? "load failed" : err), true);
            auto o = planDataJson(plan, true);
            o["name"] = QString::fromStdString(name);
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
        } });

    s.registerTool({ "list_section_templates",
        "List saved section-template names.",
        objSchema({}, {}),
        "composition",
        [e](const QJsonObject&) -> McpToolResult {
            QJsonArray arr;
            for (const auto& n : e->getProjectCommands().listSectionTemplates())
                arr.append(QString::fromStdString(n));
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(QJsonObject{ { "templates", arr } }).toJson(QJsonDocument::Compact)));
        } });

    s.registerTool({ "apply_song_brief",
        "Apply a psy-song-session Song Brief (JSON object or string) as the song plan: "
        "bpm/keyRoot/scaleMode/style/seed/totalBars/sections[{name,type,bars}]. Brief type aliases "
        "(peak/outro/drop) map onto canonical kinds — alias 'drop' resolves to kind mainB, so pass "
        "an explicit kind when the exact section kind matters. Argument: `brief` (object or JSON string). "
        "Returns the resolved plan {ok, briefApplied, bpm, keyRoot, scaleMode, seed, style, totalBars, "
        "regionsCreated, regionsUpdated, warnings[], sections:[{name, kind, startBeat, endBeat, bars}]}. "
        "One atomic undoable step.",
        objSchema({ { "brief", QJsonObject{ { "type", "object" } } } }, { "brief" }),
        "composition",
        [e](const QJsonObject& a) -> McpToolResult {
            const auto bv = a.value("brief");
            if (!bv.isObject() && !bv.isString())
                return McpToolResult::text("brief must be an object or a JSON string", true);
            const QString briefStr = bv.isString()
                ? bv.toString()
                : QString::fromUtf8(QJsonDocument(bv.toObject()).toJson(QJsonDocument::Compact));
            auto r = e->getProjectCommands().applySongBrief(briefStr.toStdString());
            if (!r.ok) return McpToolResult::text(QString::fromStdString(r.error), true);
            auto echo = planEchoJson(r);
            echo["briefApplied"] = true;
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(echo).toJson(QJsonDocument::Compact)));
        } });

    s.registerTool({ "set_cell",
        "Assign a content recipe to a (section, role) cell of the song plan. source ∈ phrase | rhythm | break | pattern | harvest; params is the source-specific parameter object (see fill_cells). seed 0 = derived deterministically from the plan seed. Rhythm cells: omitted pulseA/pulseB/rotationA/rotationB are drawn from the cell seed (bare cells vary per song — pass explicit values to pin); params.corpusRole (kick/snare/clap/hats/perc/...) fills from a seeded corpus-bank phrase instead. Upsert by (section, role); validates the section against the plan.",
        objSchema({ { "section", QJsonObject{ { "type", "string" } } },
                    { "role", QJsonObject{ { "type", "string" } } },
                    { "trackId", QJsonObject{ { "type", "integer" } } },
                    { "source", QJsonObject{ { "type", "string" }, { "enum", QJsonArray{ "phrase", "rhythm", "break", "pattern", "harvest" } } } },
                    { "params", QJsonObject{ { "type", "object" } } },
                    { "seed", QJsonObject{ { "type", "integer" }, { "minimum", 0 } } },
                    { "locked", QJsonObject{ { "type", "boolean" } } } },
                   { "section", "role", "trackId", "source" }),
        "composition",
        [e](const QJsonObject& a) -> McpToolResult {
            ProjectCommands::CellRecipe rec;
            rec.section = a.value("section").toString().toStdString();
            rec.role = a.value("role").toString().toStdString();
            rec.trackId = a.value("trackId").toInt(-1);
            rec.sourceKind = a.value("source").toString().toStdString();
            const auto pv = a.value("params");
            rec.paramsJson = pv.isString() ? pv.toString().toStdString()
                : (pv.isObject() ? QJsonDocument(pv.toObject()).toJson(QJsonDocument::Compact).toStdString() : std::string());
            rec.seed = (uint64_t) (long long) a.value("seed").toDouble(0);
            rec.locked = a.value("locked").toBool(false);
            std::string err;
            if (!e->getProjectCommands().setCellRecipe(rec, &err))
                return McpToolResult::text(QString::fromStdString(err), true);
            return McpToolResult::text(QString("ok: cell %1/%2 set").arg(QString::fromStdString(rec.section), QString::fromStdString(rec.role)));
        } });

    s.registerTool({ "set_cells",
        "Batch variant of set_cell: assign MULTIPLE (section, role) recipes in ONE undo unit and one "
        "round trip (AGENTS.md performance rule — a 9-role x 10-section track is 55 cells). cells: "
        "array of {section, role, trackId, source, params?, seed?, locked?} with the same validation "
        "as set_cell (validated per recipe BEFORE any write). A failing recipe is reported and does "
        "not abort the batch; nothing is written for it. Returns {ok, count, failed, "
        "cells:[{section, role, ok, error?}]}.",
        objSchema({ { "cells", QJsonObject{ { "type", "array" }, { "items", QJsonObject{
                        { "type", "object" },
                        { "properties", QJsonObject{
                            { "section", QJsonObject{ { "type", "string" } } },
                            { "role", QJsonObject{ { "type", "string" } } },
                            { "trackId", QJsonObject{ { "type", "integer" } } },
                            { "source", QJsonObject{ { "type", "string" },
                                { "enum", QJsonArray{ "phrase", "rhythm", "break", "pattern", "harvest" } } } },
                            { "params", QJsonObject{ { "type", "object" } } },
                            { "seed", QJsonObject{ { "type", "integer" }, { "minimum", 0 } } },
                            { "locked", QJsonObject{ { "type", "boolean" } } } } },
                        { "required", QJsonArray{ "section", "role", "trackId", "source" } } } } } } },
                   { "cells" }),
        "composition",
        [e](const QJsonObject& a) -> McpToolResult {
            const QJsonArray arr = a.value("cells").toArray();
            if (arr.isEmpty())
                return McpToolResult::text("cells array required", true);
            std::vector<ProjectCommands::CellRecipe> recipes;
            std::vector<QString> sections, roles;
            recipes.reserve((size_t) arr.size());
            for (const auto& cv : arr)
            {
                const auto o = cv.toObject();
                ProjectCommands::CellRecipe rec;
                rec.section = o.value("section").toString().toStdString();
                rec.role = o.value("role").toString().toStdString();
                rec.trackId = o.value("trackId").toInt(-1);
                rec.sourceKind = o.value("source").toString().toStdString();
                const auto pv = o.value("params");
                rec.paramsJson = pv.isString() ? pv.toString().toStdString()
                    : (pv.isObject()
                        ? QJsonDocument(pv.toObject()).toJson(QJsonDocument::Compact).toStdString()
                        : std::string());
                rec.seed = (uint64_t) (long long) o.value("seed").toDouble(0);
                rec.locked = o.value("locked").toBool(false);
                recipes.push_back(rec);
                sections.push_back(o.value("section").toString());
                roles.push_back(o.value("role").toString());
            }
            // ONE command call = ONE undo unit and one message-loop tick.
            std::vector<std::string> errors;
            const int okCount = e->getProjectCommands().setCellRecipes(recipes, &errors);
            QJsonArray results;
            int failed = 0;
            for (size_t i = 0; i < recipes.size(); ++i)
            {
                QJsonObject r{ { "section", sections[i] }, { "role", roles[i] } };
                if (i < errors.size() && !errors[i].empty())
                {
                    r["ok"] = false;
                    r["error"] = QString::fromStdString(errors[i]);
                    ++failed;
                }
                else r["ok"] = true;
                results.append(r);
            }
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(QJsonObject{
                { "ok", failed == 0 }, { "count", okCount }, { "failed", failed },
                { "cells", results } }).toJson(QJsonDocument::Compact)));
        } });

    s.registerTool({ "get_cells",
        "List all cell recipes (section, role, trackId, source, params, seed, locked, lastClipId, lastSeed).",
        objSchema({}, {}),
        "composition",
        [e](const QJsonObject&) -> McpToolResult {
            QJsonArray arr;
            for (const auto& r : e->getProjectCommands().getCells())
            {
                QJsonObject o{ { "section", QString::fromStdString(r.section) },
                               { "role", QString::fromStdString(r.role) },
                               { "trackId", r.trackId },
                               { "source", QString::fromStdString(r.sourceKind) },
                               { "seed", (double) (long long) r.seed },
                               { "locked", r.locked },
                               { "lastClipId", r.lastClipId },
                               { "lastSeed", (double) (long long) r.lastSeed } };
                if (!r.paramsJson.empty())
                {
                    auto d = QJsonDocument::fromJson(QString::fromStdString(r.paramsJson).toUtf8());
                    if (d.isObject()) o["params"] = d.object();
                }
                arr.append(o);
            }
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(QJsonObject{ { "cells", arr } }).toJson(QJsonDocument::Compact)));
        } });

    s.registerTool({ "remove_cell",
        "Remove a cell recipe (does NOT delete its generated clip).",
        objSchema({ { "section", QJsonObject{ { "type", "string" } } },
                    { "role", QJsonObject{ { "type", "string" } } } }, { "section", "role" }),
        "composition",
        [e](const QJsonObject& a) -> McpToolResult {
            const bool ok = e->getProjectCommands().removeCellRecipe(
                a.value("section").toString().toStdString(), a.value("role").toString().toStdString());
            if (!ok) return McpToolResult::text("no such cell", true);
            return McpToolResult::text("ok");
        } });

    s.registerTool({ "fill_cells",
        "Execute cell recipes: each fill writes a MIDI clip spanning EXACTLY its section window (clip reused on re-fill) and records provenance + the used seed. mode: all (every unlocked cell) | unfilled (never-filled only). ONE undo transaction for the whole batch. Returns {ok, filled, skippedLocked, failed, cells:[{ok, section, role, trackId, clipId, noteCount, seedUsed, error?}]}. A no-op is FLAGGED: `noCells:true` + warning when no cell recipes are defined at all (the state a FAILED set_cells leaves behind — nothing was written), or `nothingToDo:true` + warning when this call matched nothing.",
        objSchema({ { "mode", QJsonObject{ { "type", "string" }, { "enum", QJsonArray{ "all", "unfilled" } } } } }, {}),
        "composition",
        [e](const QJsonObject& a) -> McpToolResult {
            auto& cmds = e->getProjectCommands();
            // getCells() BEFORE the fill: a zero count means set_cells never landed, which
            // the shared payload now flags instead of reporting a plain ok:true/filled:0.
            const int definedCells = static_cast<int>(cmds.getCells().size());
            auto b = cmds.fillCells(a.value("mode").toString("all").toStdString());
            if (!b.ok && !b.error.empty())
                return McpToolResult::text(QString::fromStdString(b.error), true);
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(
                HDAW::cellFillBatchJson(b, definedCells)).toJson(QJsonDocument::Compact)));
        } });

    s.registerTool({ "reroll",
        "Re-roll cell seeds: matching unlocked cells get seed = lastSeed+1 (deterministic variation) and re-fill. Empty section/role = match all. One undo transaction. Returns the same batch payload as fill_cells, including its noCells/nothingToDo guards.",
        objSchema({ { "section", QJsonObject{ { "type", "string" } } },
                    { "role", QJsonObject{ { "type", "string" } } } }, {}),
        "composition",
        [e](const QJsonObject& a) -> McpToolResult {
            auto& cmds = e->getProjectCommands();
            const int definedCells = static_cast<int>(cmds.getCells().size());
            auto b = cmds.rerollCells(
                a.value("section").toString().toStdString(), a.value("role").toString().toStdString());
            if (!b.ok && !b.error.empty())
                return McpToolResult::text(QString::fromStdString(b.error), true);
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(
                HDAW::cellFillBatchJson(b, definedCells)).toJson(QJsonDocument::Compact)));
        } });

    s.registerTool({ "get_clip_provenance",
        "Where did this clip's notes come from? {found, tool, source, seed, params} for clips written by fill_cells; found=false for manual clips; error for missing clips.",
        objSchema({ { "clipId", QJsonObject{ { "type", "integer" } } } }, { "clipId" }),
        "composition",
        [e](const QJsonObject& a) -> McpToolResult {
            auto js = e->getProjectCommands().getClipProvenance(a.value("clipId").toInt(-1));
            if (js.empty()) return McpToolResult::text("clip not found", true);
            return McpToolResult::text(QString::fromStdString(js));
        } });

    s.registerTool({ "export_song_brief",
        "Export the CURRENT song plan as a Song Brief JSON document (verbatim when the plan was "
        "brief-applied, else synthesized from the plan).",
        objSchema({}, {}),
        "composition",
        [e](const QJsonObject&) -> McpToolResult {
            std::string err;
            auto brief = e->getProjectCommands().exportSongBrief(&err);
            if (brief.empty())
                return McpToolResult::text(QString::fromStdString(err.empty() ? "export failed" : err), true);
            return McpToolResult::text(QString::fromStdString(brief));
        } });

    // ── Layer handoff ledger (hybrid workflow; project-native) ──────────────
    // One handoff per layer = workflow metadata stored as track properties
    // (persist through save/load; audio processors never read them). The
    // orchestrator/agent contract in docs/skills/psy-song-session now persists
    // handoff evidence here instead of relying on external layers.json parsing;
    // audit_modulation_coverage verifies the modulation evidence mechanically.
    {
        // The handoff JSON shape lives in src/common/SongPlanView.cpp — shared with
        // the RPC method composition.getLayerHandoffs, so the surfaces cannot drift.
        auto handoffObj = [](const ProjectCommands::LayerHandoff& h) {
            return HDAW::layerHandoffJson(h);
        };

        s.registerTool({ "set_layer_handoff",
            "Persist a LAYER HANDOFF on a track — the project-native ledger for the psy-song-session "
            "workflow. Fields: role (bass/lead/kick/...), soundIntent, patternIntent, modulation "
            "{target, recipe, depth, readback}, verify {beforeRms, afterRms, verifyPart, warnings[]}. "
            "Written as track properties in ONE undo unit; survives save/load (whole-tree "
            "serialization). Read back with get_layer_handoffs; verify the modulation evidence with "
            "audit_modulation_coverage. Removes nothing — call clear_layer_handoff to wipe.",
            objSchema({ { "trackId", QJsonObject{ { "type", "integer" } } },
                        { "role", QJsonObject{ { "type", "string" } } },
                        { "soundIntent", QJsonObject{ { "type", "string" } } },
                        { "patternIntent", QJsonObject{ { "type", "string" } } },
                        { "modulation", QJsonObject{ { "type", "object" } } },
                        { "verify", QJsonObject{ { "type", "object" } } } },
                      { "trackId" }),
            "composition",
            [e, handoffObj](const QJsonObject& a) -> McpToolResult {
                int trackId = a.value("trackId").toInt(-1);
                auto trackList = e->getProjectModel().getTrackListTree();
                if (trackId < 0 || trackId >= trackList.getNumChildren())
                    return McpToolResult::text("trackId out of range", true);
                ProjectCommands::LayerHandoff h;
                h.role          = a.value("role").toString().toStdString();
                h.soundIntent   = a.value("soundIntent").toString().toStdString();
                h.patternIntent = a.value("patternIntent").toString().toStdString();
                if (a.contains("modulation") && a.value("modulation").isObject())
                    h.modulation = QString::fromUtf8(QJsonDocument(a.value("modulation").toObject())
                        .toJson(QJsonDocument::Compact)).toStdString();
                if (a.contains("verify") && a.value("verify").isObject())
                    h.verify = QString::fromUtf8(QJsonDocument(a.value("verify").toObject())
                        .toJson(QJsonDocument::Compact)).toStdString();
                if (h.empty())
                    return McpToolResult::text("empty handoff: provide role, soundIntent, patternIntent, modulation, and/or verify", true);
                std::string err;
                if (!e->getProjectCommands().setLayerHandoff(trackId, h, &err))
                    return McpToolResult::text(QString::fromStdString(err.empty() ? "set_layer_handoff failed" : err), true);
                QJsonObject o{ { "ok", true }, { "trackId", trackId } };
                auto fields = handoffObj(h);
                for (auto it = fields.begin(); it != fields.end(); ++it) o[it.key()] = it.value();
                return McpToolResult::text(QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
            } });

        s.registerTool({ "clear_layer_handoff",
            "Remove a track's layer handoff (all ledger properties) in one undo unit. Out-of-range "
            "trackId is an error; clearing an empty handoff is a no-op success.",
            objSchema({ { "trackId", QJsonObject{ { "type", "integer" } } } }, { "trackId" }),
            "composition",
            [e](const QJsonObject& a) -> McpToolResult {
                int trackId = a.value("trackId").toInt(-1);
                auto trackList = e->getProjectModel().getTrackListTree();
                if (trackId < 0 || trackId >= trackList.getNumChildren())
                    return McpToolResult::text("trackId out of range", true);
                std::string err;
                if (!e->getProjectCommands().clearLayerHandoff(trackId, &err))
                    return McpToolResult::text(QString::fromStdString(err.empty() ? "clear_layer_handoff failed" : err), true);
                return McpToolResult::text(QString::fromUtf8(QJsonDocument(
                    QJsonObject{ { "ok", true }, { "trackId", trackId } }).toJson(QJsonDocument::Compact)));
            } });

        s.registerTool({ "get_layer_handoffs",
            "Read the project layer-handoff ledger. With trackId: that track only. Without: every "
            "track. Each entry: {trackId, name, hasHandoff, role, soundIntent, patternIntent, "
            "modulation {target, recipe, depth, readback}, verify {beforeRms, afterRms, verifyPart, "
            "warnings}}. Tracks without a handoff are included with hasHandoff=false and no fields.",
            objSchema({ { "trackId", QJsonObject{ { "type", "integer" } } } }, {}),
            "composition",
            [e](const QJsonObject& a) -> McpToolResult {
                auto trackList = e->getProjectModel().getTrackListTree();
                const bool all = !a.contains("trackId");
                int trackId = all ? -1 : a.value("trackId").toInt(-1);
                if (!all && (trackId < 0 || trackId >= trackList.getNumChildren()))
                    return McpToolResult::text("trackId out of range", true);
                // Ledger shaping lives in src/common/SongPlanView.cpp — shared with
                // the RPC method composition.getLayerHandoffs.
                return McpToolResult::text(QString::fromUtf8(QJsonDocument(
                    HDAW::layerHandoffsJson(trackList, all ? -1 : trackId))
                        .toJson(QJsonDocument::Compact)));
            } });
    }

        s.registerTool({ "audit_song_structure",
        "READ-ONLY arrangement-variety audit (Mix Verifier boredom/static-span gates). Maps each "
        "song-plan section to the roles that actually sound in it (clip overlap; roles from the "
        "layer-handoff ledger, else track names) and reports: boredom spans (>= 8 bars of "
        "consecutive non-build sections with no melodic and no backbeat role — hats-only / "
        "bass-hat-only / silence), drop sections missing a clap/snare backbeat, and whether the "
        "first drop carries a lead/stab/motif. Gates: boredomSpans, allDropsHaveBackbeat, "
        "firstDropHasMotif, dropsAtLeastBuildLoad. Never mutates; no render.",
        objSchema({}, {}),
        "composition",
        [e](const QJsonObject&) -> McpToolResult {
            const auto plan = e->getProjectCommands().getSongPlan();
            const double bpm = e->getProjectModel().getTree().getProperty(IDs::tempo, 0.0);
            const auto audit = HDAW::auditSongStructure(
                e->getProjectModel().getTrackListTree(), plan, bpm);
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(HDAW::structureAuditJson(audit)).toJson(QJsonDocument::Compact)));
        } });
}

} // namespace mcp

