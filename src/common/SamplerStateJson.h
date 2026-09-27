#pragma once
// B3: the ONE sampler-state payload shaper for sampler_get_state (MCP),
// sampler.getState (sampler namespace) and read.getSamplerState (read
// namespace) — all three emit this exact object by construction (AGENTS.md
// parity rule; the three hand-rolled copies had already drifted).
//
// hasSound is the LIVE decoded-sound check (SamplerEngine::currentSound());
// hasSampleFile is the property-only signal a non-empty sampleFile gives.
// The pair is what unmasks a staged-but-silent slot (lesson 33: the
// silent-hats incident read "green" on a property-only hasSound).
#include "ReadModel.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace HDAW {

inline QJsonObject samplerStateJson(const SamplerStateSnapshot& s)
{
    QJsonObject obj;
    obj["sampleFile"] = QString::fromStdString(s.sampleFile);
    obj["mode"] = QString::fromStdString(s.mode);
    obj["rootNote"] = s.rootNote;
    obj["transpose"] = s.transpose;
    obj["mono"] = s.mono;
    obj["playReverse"] = s.playReverse;
    QJsonObject env;
    env["attack"] = static_cast<double>(s.attack);
    env["hold"] = static_cast<double>(s.hold);
    env["decay"] = static_cast<double>(s.decay);
    env["sustain"] = static_cast<double>(s.sustain);
    env["release"] = static_cast<double>(s.release);
    obj["envelope"] = env;
    obj["sampleStart"] = static_cast<double>(s.sampleStart);
    obj["sampleEnd"] = static_cast<double>(s.sampleEnd);
    obj["glide"] = static_cast<double>(s.glide);
    obj["hasSound"] = s.hasSound;
    obj["hasSampleFile"] = s.hasSampleFile;
    obj["activeVoices"] = s.activeVoices;
    obj["sliceMode"] = QString::fromStdString(s.sliceMode);
    obj["sliceGrid"] = s.sliceGrid;
    obj["sliceSensitivity"] = s.sliceSensitivity;
    QJsonArray slicePoints;
    for (float p : s.slicePoints)
        slicePoints.append(static_cast<double>(p));
    obj["slicePoints"] = slicePoints;
    obj["keyRangeLow"] = s.keyRangeLow;
    obj["keyRangeHigh"] = s.keyRangeHigh;
    return obj;
}

} // namespace HDAW
