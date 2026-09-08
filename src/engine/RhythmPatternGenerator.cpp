#include "engine/RhythmPatternGenerator.h"
#include "engine/RhythmPatternBank.h"
#include "engine/Generative.h"
#include <algorithm>
#include <stdexcept>

std::vector<RhythmPatternGenerator::Note> RhythmPatternGenerator::generate(const Params& p)
{
    const int loop = p.grid * p.bars;
    if (loop <= 0)
        return {};

    const double beatsPerStep = 4.0 / static_cast<double>(std::max(1, p.grid));

    std::vector<bool> claimed(static_cast<size_t>(loop), false);
    std::vector<Note> out;

    auto emit = [&](int step, int pitch, int velocity, double duration) {
        if (step < 0 || step >= loop || claimed[static_cast<size_t>(step)])
            return;
        claimed[static_cast<size_t>(step)] = true;
        out.push_back({ step * beatsPerStep, pitch, velocity, duration });
    };

    auto emitPulse = [&](int hits, int rotation, int pitch, int velocity, double duration) {
        if (hits <= 0)
            return;
        const int rot = ((rotation % loop) + loop) % loop;
        for (int s : HDAW::euclideanSteps(hits, loop, rot))
            emit(s, pitch, velocity, duration);
    };

    if (!p.voices.empty()) {
        for (const auto& v : p.voices)
            emitPulse(v.hits, v.rotation, v.pitch, v.velocity, v.duration);
    } else {
        emitPulse(p.pulseA, p.rotationA, p.pitchA, p.velocityA, 0.2);
        emitPulse(p.pulseB, p.rotationB, p.pitchB, p.velocityB, 0.1);
    }
    if (!p.dsl.empty())
        for (int s : HDAW::expandToDivision(p.dsl, loop)) // may throw std::invalid_argument
            emit(s, p.dslPitch, p.dslVelocity, 0.1);

    std::sort(out.begin(), out.end(),
              [](const Note& a, const Note& b) { return a.startBeat < b.startBeat; });
    return out;
}

bool RhythmPatternGenerator::applyPhrase(Params& p, const std::string& id)
{
    const HDAW::RhythmicPhrase* ph = HDAW::findRhythmicPhrase(id.c_str());
    if (ph == nullptr)
        return false;
    p.grid      = ph->grid;
    p.bars      = ph->bars;
    p.dsl       = ph->dsl;
    p.dslPitch  = ph->pitch;
    p.pulseA    = 0;
    p.pulseB    = 0;
    p.voices.clear();
    return true;
}

bool RhythmPatternGenerator::applyPhraseByRole(Params& p, const std::string& role, int index)
{
    const HDAW::RhythmicPhrase* ph = HDAW::findRhythmicPhraseByRole(role.c_str(), index);
    if (ph == nullptr)
        return false;
    return applyPhrase(p, ph->id);
}

std::vector<RhythmPatternGenerator::Note> RhythmPatternGenerator::generatePhrase(const std::string& id)
{
    Params p;
    if (!applyPhrase(p, id))
        return {};
    return generate(p);
}

std::vector<RhythmPatternGenerator::Note> RhythmPatternGenerator::generatePhraseByRole(const std::string& role, int index)
{
    Params p;
    if (!applyPhraseByRole(p, role, index))
        return {};
    return generate(p);
}
