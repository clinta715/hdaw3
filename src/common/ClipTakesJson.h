#pragma once
// Clip-take listing — ONE shaping shared by the MCP tool list_clip_takes
// (src/mcp/McpTools_AudioRead.cpp) and the JSON-RPC route read.getClipTakes
// (src/frontend/router/Router_Read.cpp), per the AGENTS.md parity contract:
// both surfaces must return the SAME take array for the same clipId.
//
// The tool walks TRACK_LIST/CLIP_LIST looking for the clipId and reports each
// TAKE child's {index, name, sourceFile, active} plus the clip's activeTake.
// Header-only; JUCE + Qt only.
#include "../model/ProjectModel.h"   // IDs:: namespace

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace HDAW {

// List the takes of one audio clip. Returns the exact tool text — the compact
// JSON array document on success, "clip not found" (outOk=false) otherwise.
inline QString clipTakesToolText(const juce::ValueTree& trackList, int clipId,
                                 bool* outOk = nullptr)
{
    if (outOk) *outOk = false;
    for (int t = 0; t < trackList.getNumChildren(); ++t) {
        auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
        for (int c = 0; c < clipList.getNumChildren(); ++c) {
            auto clip = clipList.getChild(c);
            if (static_cast<int>(clip.getProperty(IDs::clipID, 0)) == clipId) {
                auto takeList = clip.getChildWithName(IDs::TAKE_LIST);
                int activeIdx = static_cast<int>(clip.getProperty(IDs::activeTake, 0));
                QJsonArray arr;
                for (int i = 0; i < takeList.getNumChildren(); ++i) {
                    auto tk = takeList.getChild(i);
                    QJsonObject to;
                    to["index"] = i;
                    to["name"] = QString::fromUtf8(
                        tk.getProperty(IDs::name, "").toString().toRawUTF8());
                    to["sourceFile"] = QString::fromUtf8(
                        tk.getProperty(IDs::sourceFile, "").toString().toRawUTF8());
                    to["active"] = (i == activeIdx);
                    arr.append(to);
                }
                if (outOk) *outOk = true;
                return QString::fromUtf8(
                    QJsonDocument(arr).toJson(QJsonDocument::Compact));
            }
        }
    }
    return "clip not found";
}

} // namespace HDAW
