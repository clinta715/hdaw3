#include "McpTools.h"
#include "McpTools_Private.h"
#include "McpServer.h"
#include "../engine/AudioEngine.h"

namespace mcp {

void registerProjectDomain(McpServer& s, AudioEngine* e)
{
    registerReadTools(s, e);
    registerTrackTools(s, e);
    registerClipTools(s, e);
    registerNoteTools(s, e);
    registerCcTools(s, e);
    registerCompositionTools(s, e);
    registerArrangerTools(s, e);
    registerProjectSaveLoadTools(s, e);
    registerModulationTools(s, e);
}

} // namespace mcp
