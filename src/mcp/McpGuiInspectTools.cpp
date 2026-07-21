#include "McpGuiInspectTools.h"
#include "McpServer.h"
#include "McpToolDef.h"
#include "../engine/AudioEngine.h"
#include "../ui/GuiInspector.h"
#include <QJsonDocument>

namespace mcp {

static QJsonObject guiObjSchema(const QJsonObject& props,
                                const QJsonArray& required = {})
{
    QJsonObject s;
    s["type"] = "object";
    s["properties"] = props;
    if (!required.isEmpty()) s["required"] = required;
    s["additionalProperties"] = false;
    return s;
}

static McpToolResult jsonResult(const QJsonObject& obj)
{
    return McpToolResult::text(
        QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

static McpToolResult jsonResult(const QJsonArray& arr)
{
    return McpToolResult::text(
        QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

void registerGuiInspectTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({
        "gui.snapshot",
        "Full structured dump of the GUI state: timeline, tracks, clips, selection, panel.",
        guiObjSchema({}),
        [e](const QJsonObject&) -> McpToolResult {
            HDAW::GuiInspector inspector(e->getMainWindow());
            return jsonResult(inspector.snapshot());
        }
    });

    s.registerTool({
        "gui.get_clip_geometry",
        "Get scene-space position and size of clips on the timeline.",
        guiObjSchema({{"clipId", QJsonObject{{"type", "integer"},
            {"description", "Optional clip ID filter; omit for all clips"}}}}),
        [e](const QJsonObject& a) -> McpToolResult {
            HDAW::GuiInspector inspector(e->getMainWindow());
            int clipId = a.contains("clipId") ? a["clipId"].toInt() : -1;
            return jsonResult(inspector.clipGeometry(clipId));
        }
    });

    s.registerTool({
        "gui.get_track_layout",
        "Get track header geometry and state (name, y, height, mute/solo/arm).",
        guiObjSchema({}),
        [e](const QJsonObject&) -> McpToolResult {
            HDAW::GuiInspector inspector(e->getMainWindow());
            return jsonResult(inspector.trackLayout());
        }
    });

    s.registerTool({
        "gui.get_selection",
        "Get current selection state (selected track, clips, notes).",
        guiObjSchema({}),
        [e](const QJsonObject&) -> McpToolResult {
            HDAW::GuiInspector inspector(e->getMainWindow());
            return jsonResult(inspector.selectionState());
        }
    });

    s.registerTool({
        "gui.get_scroll",
        "Get timeline scroll position and zoom (pixelsPerSecond).",
        guiObjSchema({}),
        [e](const QJsonObject&) -> McpToolResult {
            HDAW::GuiInspector inspector(e->getMainWindow());
            return jsonResult(inspector.scrollState());
        }
    });

    s.registerTool({
        "gui.get_panel_state",
        "Get the active bottom-panel tab and tab list.",
        guiObjSchema({}),
        [e](const QJsonObject&) -> McpToolResult {
            HDAW::GuiInspector inspector(e->getMainWindow());
            return jsonResult(inspector.panelState());
        }
    });

    s.registerTool({
        "gui.get_piano_roll",
        "Get piano-roll contents (notes with positions) for loaded MIDI clips.",
        guiObjSchema({}),
        [e](const QJsonObject&) -> McpToolResult {
            HDAW::GuiInspector inspector(e->getMainWindow());
            return jsonResult(inspector.pianoRollState());
        }
    });

    s.registerTool({
        "gui.hit_test",
        "Hit-test a scene coordinate and return what item is there.",
        guiObjSchema({
            {"x", QJsonObject{{"type", "number"}, {"description", "Scene X coordinate"}}},
            {"y", QJsonObject{{"type", "number"}, {"description", "Scene Y coordinate"}}}
        }, {"x", "y"}),
        [e](const QJsonObject& a) -> McpToolResult {
            HDAW::GuiInspector inspector(e->getMainWindow());
            return jsonResult(inspector.hitTest(a["x"].toDouble(), a["y"].toDouble()));
        }
    });
}

} // namespace mcp
