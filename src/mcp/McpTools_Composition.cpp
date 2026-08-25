#include "McpTools.h"
#include "McpTools_Private.h"
#include "McpServer.h"
#include "../engine/AudioEngine.h"

namespace mcp {

void registerCompositionTools(McpServer& s, AudioEngine* e)
{
    registerTimingTools(s, e);
    registerGenerateTools(s, e);
    registerInstrumentTools(s, e);
    registerPatternTools(s, e);
}

} // namespace mcp
