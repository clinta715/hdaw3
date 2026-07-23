#include "McpTools.h"
#include "McpTools_Private.h"
#include "McpExportTool.h"
#include "McpGuiInspectTools.h"
#include "McpServer.h"
#include "../model/ProjectModel.h"
#include "../engine/AudioEngine.h"
#include <QJsonArray>
#include <QJsonObject>

namespace mcp {

QString jstr(const juce::String& s)
{
    return QString::fromUtf8(s.toRawUTF8());
}

QJsonObject objSchema(const QJsonObject& props, const QJsonArray& required)
{
    QJsonObject s{{"type","object"},{"properties", props},{"additionalProperties", false}};
    if (!required.isEmpty()) s["required"] = required;
    return s;
}

juce::ValueTree findClip(AudioEngine* e, int clipId, int* outTrackIdx)
{
    auto tl = e->getProjectModel().getTrackListTree();
    for (int i = 0; i < tl.getNumChildren(); ++i) {
        auto cl = tl.getChild(i).getChildWithName(IDs::CLIP_LIST);
        for (int j = 0; j < cl.getNumChildren(); ++j) {
            auto c = cl.getChild(j);
            if (static_cast<int>(c.getProperty(IDs::clipID)) == clipId) {
                if (outTrackIdx) *outTrackIdx = i;
                return c;
            }
        }
    }
    return {};
}

juce::ValueTree findNote(AudioEngine* e, int noteId, int* outClipId)
{
    auto tl = e->getProjectModel().getTrackListTree();
    for (int i = 0; i < tl.getNumChildren(); ++i) {
        auto cl = tl.getChild(i).getChildWithName(IDs::CLIP_LIST);
        for (int j = 0; j < cl.getNumChildren(); ++j) {
            auto nl = cl.getChild(j).getChildWithName(IDs::MIDI_NOTE_LIST);
            if (!nl.isValid()) continue;
            for (int k = 0; k < nl.getNumChildren(); ++k) {
                auto n = nl.getChild(k);
                if (static_cast<int>(n.getProperty(IDs::noteID)) == noteId) {
                    if (outClipId) *outClipId = static_cast<int>(cl.getChild(j).getProperty(IDs::clipID));
                    return n;
                }
            }
        }
    }
    return {};
}

juce::ValueTree findLane(AudioEngine* e, int trackId, const QJsonValue& ref)
{
    auto tl = e->getProjectModel().getTrackListTree();
    if (trackId < 0 || trackId >= tl.getNumChildren()) return {};
    auto al = tl.getChild(trackId).getChildWithName(IDs::AUTOMATION_LIST);
    if (ref.isDouble()) {
        int pid = ref.toInt();
        for (int i = 0; i < al.getNumChildren(); ++i)
            if (static_cast<int>(al.getChild(i).getProperty(IDs::paramID)) == pid) return al.getChild(i);
    } else if (ref.isString()) {
        QString n = ref.toString();
        for (int i = 0; i < al.getNumChildren(); ++i)
            if (al.getChild(i).getProperty(IDs::name).toString() == juce::String(n.toUtf8().constData())) return al.getChild(i);
    }
    return {};
}

void registerAllTools(McpServer& s) {
    auto* e = s.engine();
    if (!e) return;
    registerProjectDomain(s, e);
    registerTransportDomain(s, e);
    registerAudioDomain(s, e);
    registerExportTool(s);
    registerGuiInspectTools(s, e);
}

} // namespace mcp
