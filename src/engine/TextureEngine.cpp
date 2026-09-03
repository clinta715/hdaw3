#include "engine/TextureEngine.h"

namespace HDAW {

void TextureEngine::writeFxAccents(const std::string& fxRole, int bar, int keyRoot,
                                   int /*scaleMode*/, RoleCtx& riser, RoleCtx& down,
                                   const TextureStyle& style, int maxNotes)
{
    const double windowStart = bar * 4.0;
    const int riserPitch = diaRoot(keyRoot, 3);
    const int downPitch = diaRoot(keyRoot, 2);
    if (fxRole == "riser" && riser.track >= 0)
    {
        for (int i = 0; i < style.riserSteps; ++i)
            riser.add(windowStart + i * style.riserStepBeats, riserPitch,
                      style.riserVelBase + style.riserVelStep * i, style.riserNoteDur, maxNotes);
    }
    else if (fxRole == "down" && down.track >= 0)
        down.add(windowStart, downPitch, style.downVelocity, style.downDurBeats, maxNotes);
}

PsytranceAutomationPoint TextureEngine::filterSweepPoint(const std::string& role, int bar,
                                                         double value01, const TextureStyle& style)
{
    return { role, "filterCutoff", bar * 4.0, value01, style.filterSweepDurBeats };
}

} // namespace HDAW
