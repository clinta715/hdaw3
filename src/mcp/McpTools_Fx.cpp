#include "McpTools.h"
#include "McpTools_Private.h"
#include "McpServer.h"
#include "../engine/AudioEngine.h"

namespace mcp {

void registerFxTools(McpServer& s, AudioEngine* e)
{
    registerFxSlotTools(s, e);
    registerFxPresetTools(s, e);
    registerFxChainTools(s, e);
    registerSamplerTools(s, e);
    registerFmSynthTools(s, e);
    registerPsyFmTools(s, e);
}

} // namespace mcp
