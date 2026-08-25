#include "McpTools.h"
#include "McpTools_Private.h"
#include "McpServer.h"
#include "../engine/AudioEngine.h"

namespace mcp {

void registerFxTools(McpServer& s, AudioEngine* e)
{
    registerFxSlotTools(s, e);
    registerFxPresetTools(s, e);
    registerSamplerTools(s, e);
    registerFmSynthTools(s, e);
}

} // namespace mcp
