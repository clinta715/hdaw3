#include "SongPlanView.h"

#include "../engine/SongStructureAudit.h"
#include "../model/ProjectModel.h"

#include <QJsonDocument>
#include <QJsonValue>
#include <QString>

namespace HDAW {

namespace {

// Matches mcp::jstr: UTF-8-safe juce::String -> QString.
QString jstr(const juce::String& s)
{
    return QString::fromUtf8(s.toRawUTF8());
}

// A property that may hold JSON (modulation / verify): emit the parsed object when
// it parses, else the raw string.
QJsonValue jsonOrString(const QString& s)
{
    if (s.isEmpty())
        return {};
    const auto obj = QJsonDocument::fromJson(s.toUtf8()).object();
    return obj.isEmpty() ? QJsonValue(s) : QJsonValue(obj);
}

} // namespace

QJsonObject layerHandoffJson(const ProjectCommands::LayerHandoff& h)
{
    QJsonObject o;
    if (!h.role.empty())          o["role"]          = QString::fromStdString(h.role);
    if (!h.soundIntent.empty())   o["soundIntent"]   = QString::fromStdString(h.soundIntent);
    if (!h.patternIntent.empty()) o["patternIntent"] = QString::fromStdString(h.patternIntent);
    if (!h.modulation.empty())    o["modulation"]    = jsonOrString(QString::fromStdString(h.modulation));
    if (!h.verify.empty())        o["verify"]        = jsonOrString(QString::fromStdString(h.verify));
    return o;
}

QJsonArray layerHandoffsJson(const juce::ValueTree& trackList, int trackId)
{
    QJsonArray arr;
    if (!trackList.isValid())
        return arr;

    const auto emitEntry = [&arr](const juce::ValueTree& t) {
        QJsonObject o;
        o["trackId"] = t.getParent().indexOf(t);
        o["name"] = jstr(t.getProperty(IDs::name, "Track").toString());
        o["hasHandoff"] = t.hasProperty(IDs::layerRole) || t.hasProperty(IDs::layerSoundIntent)
            || t.hasProperty(IDs::layerPatternIntent) || t.hasProperty(IDs::layerModulation)
            || t.hasProperty(IDs::layerVerify);
        if (o["hasHandoff"].toBool())
        {
            auto rd = [](const juce::ValueTree& tt, const juce::Identifier& id) {
                return tt.getProperty(id, "").toString();
            };
            const QString role = jstr(rd(t, IDs::layerRole));
            const QString sound = jstr(rd(t, IDs::layerSoundIntent));
            const QString pattern = jstr(rd(t, IDs::layerPatternIntent));
            const QString mod = jstr(rd(t, IDs::layerModulation));
            const QString ver = jstr(rd(t, IDs::layerVerify));
            if (!role.isEmpty())    o["role"] = role;
            if (!sound.isEmpty())   o["soundIntent"] = sound;
            if (!pattern.isEmpty()) o["patternIntent"] = pattern;
            if (!mod.isEmpty())     o["modulation"] = jsonOrString(mod);
            if (!ver.isEmpty())     o["verify"] = jsonOrString(ver);
        }
        arr.append(o);
    };

    if (trackId >= 0)
    {
        if (trackId < trackList.getNumChildren())
            emitEntry(trackList.getChild(trackId));
        return arr;
    }
    for (int i = 0; i < trackList.getNumChildren(); ++i)
        emitEntry(trackList.getChild(i));
    return arr;
}

QJsonObject structureAuditJson(const SongStructureAudit& audit)
{
    if (!audit.hasPlan)
        return QJsonObject{ { "ok", false }, { "hasPlan", false },
                            { "note", "no song plan set — structure gates not evaluated" } };
    QJsonArray sections, spans, dropsMissing;
    for (const auto& s : audit.sections)
    {
        QJsonArray roles, kinds;
        for (const auto& r : s.soundingRoles) roles.append(QString::fromStdString(r));
        for (const auto& k : s.roleKinds) kinds.append(QString::fromStdString(k));
        sections.append(QJsonObject{
            { "name", QString::fromStdString(s.name) },
            { "kind", QString::fromStdString(s.kind) },
            { "bars", s.bars },
            { "startBeat", s.startBeat },
            { "endBeat", s.endBeat },
            { "soundingRoles", roles },
            { "roleKinds", kinds },
            { "hasMelodic", s.hasMelodic },
            { "hasBackbeat", s.hasBackbeat } });
    }
    for (const auto& sp : audit.spans)
        spans.append(QJsonObject{
            { "flag", QString::fromStdString(sp.flag) },
            { "startName", QString::fromStdString(sp.startName) },
            { "endName", QString::fromStdString(sp.endName) },
            { "bars", sp.bars },
            { "startBeat", sp.startBeat },
            { "endBeat", sp.endBeat } });
    for (const auto& n : audit.dropNamesMissingBackbeat)
        dropsMissing.append(QString::fromStdString(n));
    QJsonArray dropsThinner;
    for (const auto& n : audit.dropNamesThinnerThanBuild)
        dropsThinner.append(QString::fromStdString(n));
    return QJsonObject{
        { "ok", audit.ok },
        { "hasPlan", true },
        { "gates", QJsonObject{
            { "boredomSpans", static_cast<int>(audit.spans.size()) },
            // expectBackbeat=false (backbeatChecked=false): the gate is
            // treated as satisfied — true — while dropsMissingBackbeat below
            // stays informational.
            { "allDropsHaveBackbeat",
              !audit.backbeatChecked || audit.dropNamesMissingBackbeat.empty() },
            { "dropsAtLeastBuildLoad", audit.dropNamesThinnerThanBuild.empty() },
            { "firstDropHasMotif", !audit.anyDrop || audit.firstDropHasMotif } } },
        { "dropChecks", QJsonObject{
            { "anyDrop", audit.anyDrop },
            { "firstDrop", QString::fromStdString(audit.firstDropName) },
            { "firstDropHasMotif", audit.firstDropHasMotif },
            { "backbeatChecked", audit.backbeatChecked },
            { "dropsMissingBackbeat", dropsMissing },
            { "dropsThinnerThanBuild", dropsThinner } } },
        { "sections", sections },
        { "spans", spans } };
}

QJsonObject cellFillBatchJson(const ProjectCommands::CellFillBatchResult& b, int definedCells)
{
    QJsonArray cells;
    for (const auto& c : b.cells)
    {
        QJsonObject o{ { "ok", c.ok }, { "section", QString::fromStdString(c.section) },
                       { "role", QString::fromStdString(c.role) }, { "trackId", c.trackId },
                       { "clipId", c.clipId },
                       { "noteCount", c.noteCount },
                       { "seedUsed", static_cast<double>(static_cast<long long>(c.seedUsed)) } };
        if (!c.error.empty()) o["error"] = QString::fromStdString(c.error);
        cells.append(o);
    }
    QJsonObject o{ { "ok", b.ok }, { "filled", b.filled },
                   { "skippedLocked", b.skippedLocked }, { "failed", b.failed },
                   { "cells", cells } };
    if (!b.error.empty()) o["error"] = QString::fromStdString(b.error);

    // Guards: distinguish "nothing to do" from "done". Without these, a project whose
    // set_cells failed reports the same ok:true/filled:0 as a legitimate no-op re-fill.
    if (definedCells <= 0)
    {
        o["noCells"] = true;
        o["warning"] = "no cell recipes are defined — nothing was written (did set_cells "
                       "fail? check its per-cell results)";
    }
    else if (b.filled == 0 && b.failed == 0 && b.skippedLocked == 0)
    {
        o["nothingToDo"] = true;
        o["warning"] = "no cell matched this call (already filled, or the section/role "
                       "filter matched nothing) — nothing was written";
    }
    return o;
}

} // namespace HDAW
