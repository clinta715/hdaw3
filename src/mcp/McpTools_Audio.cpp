#include "McpTools.h"
#include "McpTools_Private.h"
#include "McpServer.h"
#include "../engine/AudioEngine.h"

namespace mcp {

void registerAudioDomain(McpServer& s, AudioEngine* e)
{
    registerAudioReadTools(s, e);
    registerFxTools(s, e);
    registerMidiFxTools(s, e);
    registerAutomationTools(s, e);
    registerSendTools(s, e);
    registerEnvelopeTools(s, e);
}

} // namespace mcp
