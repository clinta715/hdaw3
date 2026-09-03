#include "engine/PsytranceMarkovGenerator.h"
#include "engine/MarkovArranger.h"

namespace HDAW {

const char* PsytranceMarkovGenerator::actionName(MarkovAction a)
{
    switch (a) {
        case MarkovAction::Keep:              return "Keep";
        case MarkovAction::AddLayer:          return "AddLayer";
        case MarkovAction::RemoveLayer:       return "RemoveLayer";
        case MarkovAction::SwapPattern:       return "SwapPattern";
        case MarkovAction::FxHit:             return "FxHit";
        case MarkovAction::Breakbeat:         return "Breakbeat";
        case MarkovAction::FilterSweep:       return "FilterSweep";
        case MarkovAction::RhythmVariant:     return "RhythmVariant";
        case MarkovAction::ArpVariant:        return "ArpVariant";
        case MarkovAction::NoteLengthVariant: return "NoteLengthVariant";
        case MarkovAction::KeyChange:         return "KeyChange";
    }
    return "Keep";
}

PsytranceMarkovScore PsytranceMarkovGenerator::generate(const PsytranceMarkovParams& paramsIn)
{
    return MarkovArranger::run(paramsIn);
}

} // namespace HDAW
