#include "McpTools.h"
#include "McpTools_Private.h"
#include "McpServer.h"
#include "McpToolDef.h"
#include "../engine/AudioEngine.h"
#include "../common/ProjectCommands.h"
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
        "(peak/outro/drop) map onto canonical kinds. One atomic undoable step.",
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
        "Assign a content recipe to a (section, role) cell of the song plan. source ∈ phrase | rhythm | break | pattern | harvest; params is the source-specific parameter object (see fill_cells). seed 0 = derived deterministically from the plan seed. Upsert by (section, role); validates the section against the plan.",
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
        "Execute cell recipes: each fill writes a MIDI clip spanning EXACTLY its section window (clip reused on re-fill) and records provenance + the used seed. mode: all (every unlocked cell) | unfilled (never-filled only). ONE undo transaction for the whole batch. Returns per-cell {clipId, noteCount, seedUsed, ok, error}.",
        objSchema({ { "mode", QJsonObject{ { "type", "string" }, { "enum", QJsonArray{ "all", "unfilled" } } } } }, {}),
        "composition",
        [e](const QJsonObject& a) -> McpToolResult {
            auto b = e->getProjectCommands().fillCells(a.value("mode").toString("all").toStdString());
            if (!b.ok && !b.error.empty())
                return McpToolResult::text(QString::fromStdString(b.error), true);
            QJsonArray cells;
            for (const auto& c : b.cells)
            {
                QJsonObject o{ { "ok", c.ok }, { "section", QString::fromStdString(c.section) },
                               { "role", QString::fromStdString(c.role) }, { "clipId", c.clipId },
                               { "noteCount", c.noteCount }, { "seedUsed", (double) (long long) c.seedUsed } };
                if (!c.error.empty()) o["error"] = QString::fromStdString(c.error);
                cells.append(o);
            }
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(QJsonObject{
                { "ok", b.ok }, { "filled", b.filled }, { "skippedLocked", b.skippedLocked },
                { "failed", b.failed }, { "cells", cells } }).toJson(QJsonDocument::Compact)));
        } });

    s.registerTool({ "reroll",
        "Re-roll cell seeds: matching unlocked cells get seed = lastSeed+1 (deterministic variation) and re-fill. Empty section/role = match all. One undo transaction.",
        objSchema({ { "section", QJsonObject{ { "type", "string" } } },
                    { "role", QJsonObject{ { "type", "string" } } } }, {}),
        "composition",
        [e](const QJsonObject& a) -> McpToolResult {
            auto b = e->getProjectCommands().rerollCells(
                a.value("section").toString().toStdString(), a.value("role").toString().toStdString());
            if (!b.ok && !b.error.empty())
                return McpToolResult::text(QString::fromStdString(b.error), true);
            QJsonArray cells;
            for (const auto& c : b.cells)
            {
                QJsonObject o{ { "ok", c.ok }, { "section", QString::fromStdString(c.section) },
                               { "role", QString::fromStdString(c.role) }, { "clipId", c.clipId },
                               { "noteCount", c.noteCount }, { "seedUsed", (double) (long long) c.seedUsed } };
                if (!c.error.empty()) o["error"] = QString::fromStdString(c.error);
                cells.append(o);
            }
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(QJsonObject{
                { "ok", b.ok }, { "filled", b.filled }, { "skippedLocked", b.skippedLocked },
                { "failed", b.failed }, { "cells", cells } }).toJson(QJsonDocument::Compact)));
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
}

} // namespace mcp
