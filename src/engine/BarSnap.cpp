#include "BarSnap.h"
#include "../engine/AudioEngine.h"
#include "../engine/TransportManager.h"
#include <cmath>

namespace HDAW
{
    double snapToBarBoundary(double timeSeconds, AudioEngine& engine)
    {
        if (timeSeconds <= 0.0)
            return timeSeconds;

        double bpm = engine.getTransportManager().getBpmAtTime(timeSeconds);
        if (bpm <= 0.0)
            return timeSeconds;

        const int beatsPerBar = 4;
        double secondsPerBar = (60.0 / bpm) * beatsPerBar;
        if (secondsPerBar <= 0.0)
            return timeSeconds;

        double barPos = timeSeconds / secondsPerBar;
        double roundedBar = std::round(barPos);
        double snapped = roundedBar * secondsPerBar;

        double sampleDuration = 1.0 / 44100.0;
        if (std::abs(snapped - timeSeconds) < sampleDuration)
            return timeSeconds;

        return snapped;
    }
}
