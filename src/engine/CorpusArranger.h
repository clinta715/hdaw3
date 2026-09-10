#pragma once
// CorpusArranger — corpus-sampled arrangement grammar (v1 prototype).
// Structure axes (length/kick-entry/const-bass/late-novelty/breakdown) are
// sampled from the measured distribution snapshot (see
// tools/corpus_arrange/distributions.json + compositions/
// psytrance_corpus_fulltracks.tsv, n=533). Deterministic per seed.
// Keeps-alongside MarkovArranger: structure here, micro-patterns still come
// from HarmonyEngine (the same pitched-note engine Markov uses).
#include "engine/PsytranceMarkovGenerator.h"
#include "engine/HarmonyEngine.h"
#include <string>
#include <vector>

namespace HDAW {

enum class IntroMode { FourOnFloor, ShortIntro, MidIntro, LongIntro };

struct CorpusOptions {
    int seed = 0;
    int bars = 0;            // 0 => sample from length modes
    int lengthMode = -1;     // 0 short, 1 mid, 2 extended, -1 auto
    int introMode = -1;      // IntroMode, -1 auto
    int constBass = -1;      // -1 auto
    int lateNovelty = -1;    // -1 auto
    int breakdown = -1;      // -1 auto
    std::string noveltyRole; // "" => auto from pool
};

struct CorpusSection {
    std::string name;
    int barStart = 0, bars = 0;
    std::vector<std::string> roles;
    double density = 0.5;
};

struct CorpusPlan {
    std::string method = "corpusArranger";
    int version = 1;
    int seed = 0;
    int totalBars = 0;
    std::string lengthMode;
    std::string introMode;
    bool constBass = false, lateNovelty = false, breakdown = false;
    bool kicklessBreakdown = false;
    std::string noveltyRole;
    std::vector<CorpusSection> sections;
};

// Palette + key params feeding score generation (mirrors PsytranceMarkovParams).
struct CorpusParams {
    int keyRoot = 0, scaleMode = 1;
    int seed = 0, totalBars = 32;
    std::vector<int> progressionA, progressionB;
    CorpusOptions opts;
    int kick = -1, bass = -1, hat = -1, snare = -1, clap = -1, rim = -1;
    int arp = -1, stab = -1, pad = -1, riser = -1, down = -1, lead = -1;
    double melodyCorpusPhraseProb = 0.35; // probability a melodic 4-bar window uses MelodyPatternBank
};

class CorpusArranger {
public:
    static CorpusPlan samplePlan(const CorpusOptions& opts);
    static PsytranceMarkovScore generate(const CorpusParams& params);
    static const char* introName(IntroMode m);
};

} // namespace HDAW
