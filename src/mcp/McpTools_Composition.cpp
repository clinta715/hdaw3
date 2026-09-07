#include "McpTools.h"
#include "McpTools_Private.h"
#include "McpServer.h"
#include "../engine/AudioEngine.h"
#include "../engine/PhraseGenerator.h"

namespace mcp {

void registerCompositionTools(McpServer& s, AudioEngine* e)
{
    auto parsePitchClass = [](QString token) -> int {
        token = token.trimmed().toLower();
        if (token.isEmpty())
            return -1;
        int pitch = -1;
        switch (token[0].toLatin1()) {
            case 'c': pitch = 0; break;
            case 'd': pitch = 2; break;
            case 'e': pitch = 4; break;
            case 'f': pitch = 5; break;
            case 'g': pitch = 7; break;
            case 'a': pitch = 9; break;
            case 'b': pitch = 11; break;
            default: return -1;
        }
        if (token.size() > 1) {
            if (token.at(1) == QChar('#')) ++pitch;
            else if (token.at(1) == QChar('b')) --pitch;
        }
        return (pitch + 12) % 12;
    };

    auto resolveScaleMode = [](const QString& text) -> int {
        const QString needle = text.trimmed().toLower();
        if (needle.isEmpty())
            return -1;
        for (const auto& mode : PhraseGenerator::getScaleModes()) {
            const QString full = QString::fromUtf8(mode.name).toLower();
            const int paren = full.indexOf(" (");
            const QString shortName = paren > 0 ? full.left(paren) : full;
            const QString alias = paren > 0 ? full.mid(paren + 2, full.length() - paren - 3) : QString();
            if (needle == full || needle == shortName || (!alias.isEmpty() && needle == alias))
                return mode.index;
        }
        return -1;
    };

    registerTimingTools(s, e);
    registerGenerateTools(s, e);
    registerInstrumentTools(s, e);
    registerPatternTools(s, e);

    s.registerTool({"batch_import_samples",
        "Import multiple audio files in one transaction using the existing importAudioFile primitive.",
        objSchema({{"samples", QJsonObject{{"type","array"}}}}, {"samples"}),
        "composition",
        [e](const QJsonObject& a) -> McpToolResult {
            const auto samples = a.value("samples").toArray();
            QJsonArray results;
            int imported = 0;
            e->getProjectCommands().beginTransaction("Batch import samples");
            for (const auto& v : samples) {
                const auto o = v.toObject();
                const QString path = o.value("path").toString();
                const int trackIndex = o.contains("trackIndex") ? o.value("trackIndex").toInt(-1) : -1;
                const double startBeat = o.contains("startBeat") ? o.value("startBeat").toDouble(0.0) : 0.0;
                const bool alignToGrid = o.contains("alignToGrid") ? o.value("alignToGrid").toBool(true) : true;
                QJsonObject item{{"path", path}, {"trackIndex", trackIndex}, {"startBeat", startBeat}, {"alignToGrid", alignToGrid}};
                if (path.isEmpty()) {
                    item["error"] = "missing path";
                    results.append(item);
                    continue;
                }
                auto r = e->getProjectCommands().importAudioFile(trackIndex, startBeat, path.toStdString(), alignToGrid);
                if (r.clipId < 0) {
                    item["error"] = QString::fromStdString(r.error.empty() ? "import failed" : r.error);
                } else {
                    item["clipId"] = r.clipId;
                    item["aligned"] = r.aligned;
                    item["bpm"] = r.bpm;
                    item["ratio"] = r.ratio;
                    item["offset"] = r.offset;
                    item["duration"] = r.duration;
                    item["bars"] = r.bars;
                    ++imported;
                }
                results.append(item);
            }
            e->getProjectCommands().endTransaction();
            QJsonObject out{{"imported", imported}, {"results", results}};
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"setup_remix",
        "Set remix tempo/scale and add named arranger regions for a section structure. Read/write only through existing project commands.",
        objSchema({
            {"bpm", QJsonObject{{"type","number"}}},
            {"key", QJsonObject{{"type","string"}}},
            {"scaleRoot", QJsonObject{{"type","integer"},{"minimum",0},{"maximum",11}}},
            {"scaleMode", QJsonObject{{"type","integer"},{"minimum",0},{"maximum",20}}},
            {"samplePool", QJsonObject{{"type","string"}}},
            {"structure", QJsonObject{{"type","array"},{"items", QJsonObject{{"type","string"}}}}},
            {"sectionLengthBeats", QJsonObject{{"type","number"},{"minimum",0.25}}}
        }),
        "composition",
        [e, parsePitchClass, resolveScaleMode](const QJsonObject& a) -> McpToolResult {
            const double bpm = a.contains("bpm") ? a.value("bpm").toDouble(-1.0) : -1.0;
            if (bpm > 0.0)
                e->getProjectCommands().setTempo(bpm);

            int root = a.contains("scaleRoot") ? a.value("scaleRoot").toInt(-1) : -1;
            int mode = a.contains("scaleMode") ? a.value("scaleMode").toInt(-1) : -1;
            if (a.contains("key")) {
                const QString key = a.value("key").toString().trimmed();
                const QStringList parts = key.split(' ', Qt::SkipEmptyParts);
                if (parts.size() < 2)
                    return McpToolResult::text("key must look like 'G minor'", true);
                const int parsedRoot = parsePitchClass(parts[0]);
                const int parsedMode = resolveScaleMode(parts.mid(1).join(' '));
                if (parsedRoot < 0 || parsedMode < 0)
                    return McpToolResult::text("could not parse key: " + key, true);
                if (root < 0) root = parsedRoot;
                if (mode < 0) mode = parsedMode;
            }
            if (root < 0) root = e->getReadModel().getScaleRoot();
            if (mode < 0) mode = e->getReadModel().getScaleMode();
            e->getProjectCommands().setScaleRoot(root);
            e->getProjectCommands().setScaleMode(mode);

            QJsonArray regions;
            const double sectionLengthBeats = a.contains("sectionLengthBeats") ? a.value("sectionLengthBeats").toDouble(16.0) : 16.0;
            if (sectionLengthBeats <= 0.0)
                return McpToolResult::text("sectionLengthBeats must be > 0", true);
            const auto structure = a.value("structure").toArray();
            e->getProjectCommands().beginTransaction("Setup remix");
            for (int i = 0; i < structure.size(); ++i) {
                const QString name = structure[i].toString().trimmed();
                if (name.isEmpty())
                    continue;
                const double startBeat = i * sectionLengthBeats;
                const double endBeat = startBeat + sectionLengthBeats;
                auto regionId = e->getProjectCommands().addArrangerRegion(name.toStdString(), startBeat, sectionLengthBeats);
                regions.append(QJsonObject{{"name", name}, {"regionID", QString::fromStdString(regionId)}, {"startBeat", startBeat}, {"endBeat", endBeat}});
            }
            e->getProjectCommands().endTransaction();

            QJsonObject out{{"tempo", bpm > 0.0 ? bpm : e->getReadModel().getTransport().bpm},
                            {"scaleRoot", root},
                            {"scaleMode", mode},
                            {"samplePool", a.value("samplePool").toString()},
                            {"regionsCreated", regions.size()},
                            {"regions", regions}};
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact)));
        }});

    s.registerTool({"create_section",
        "Create a named arranger region and place audio clips on tracks using existing clip-placement commands.",
        objSchema({
            {"sectionName", QJsonObject{{"type","string"}}},
            {"startBeat", QJsonObject{{"type","number"}}},
            {"endBeat", QJsonObject{{"type","number"}}},
            {"tracks", QJsonObject{{"type","array"}}}
        }, {"sectionName","startBeat","endBeat","tracks"}),
        "composition",
        [e](const QJsonObject& a) -> McpToolResult {
            const QString sectionName = a.value("sectionName").toString().trimmed();
            const double startBeat = a.value("startBeat").toDouble();
            const double endBeat = a.value("endBeat").toDouble();
            if (sectionName.isEmpty())
                return McpToolResult::text("sectionName is required", true);
            if (!(endBeat > startBeat))
                return McpToolResult::text("endBeat must be greater than startBeat", true);

            auto& cmds = e->getProjectCommands();
            const QString regionId = QString::fromStdString(cmds.addArrangerRegion(sectionName.toStdString(), startBeat, endBeat - startBeat));
            QJsonArray placedClips;
            const auto tracks = a.value("tracks").toArray();
            for (int i = 0; i < tracks.size(); ++i) {
                const auto t = tracks[i].toObject();
                int trackIndex = t.contains("trackIndex") ? t.value("trackIndex").toInt(i) : i;
                QString trackName = t.value("trackName").toString().trimmed();
                while (e->getProjectModel().getTrackListTree().getNumChildren() <= trackIndex) {
                    const QString createdName = trackName.isEmpty()
                        ? QString("Track %1").arg(e->getProjectModel().getTrackListTree().getNumChildren() + 1)
                        : trackName;
                    cmds.addTrack(createdName.toStdString());
                }

                const auto clips = t.value("clips").toArray();
                for (const auto& cv : clips) {
                    const auto c = cv.toObject();
                    const QString path = c.value("path").toString();
                    if (path.isEmpty())
                        continue;
                    const double clipStart = c.contains("startBeat") ? c.value("startBeat").toDouble(startBeat) : startBeat;
                    const double clipLength = c.contains("durationBeats") ? c.value("durationBeats").toDouble(endBeat - startBeat) : (endBeat - startBeat);
                    const QString clipName = c.value("name").toString();
                    const int clipId = cmds.addAudioClip(trackIndex, clipStart, clipLength, path.toStdString(), clipName.isEmpty() ? path.toStdString() : clipName.toStdString());
                    placedClips.append(QJsonObject{{"trackIndex", trackIndex}, {"clipId", clipId}, {"path", path}});
                }
            }

            QJsonObject out{{"regionID", regionId}, {"placedClips", placedClips}, {"trackCount", e->getProjectModel().getTrackListTree().getNumChildren()}};
            return McpToolResult::text(QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact)));
        }});
}

} // namespace mcp
