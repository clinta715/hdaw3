// src/engine/LibraryClusterer.h
#pragma once

// Pure, deterministic clustering + nearest-neighbour search over TimbreLib
// sidecar data (text: tags + description; dsp: the 20-key numeric `dsp`
// vector). Contract: docs/plans/2026-08-25-library-clustering.md.
//
// Rules (all deterministic, seed 42):
//   - Text axis: tokenize [a-z0-9']+ (lowercase, length > 1) over
//     tags + description; TF-IDF (idf = ln((1+N)/(1+df)) + 1), vocabulary
//     capped to the top 512 tokens by document frequency (tiebreak: token
//     ascending), L2-normalized.
//   - Dsp axis: z-score each of the 20 dims across the library (population
//     std; std == 0 -> the dim contributes all-zero), L2-normalized. A dsp
//     vector counts only when ALL 20 values are present and finite.
//   - Combined vector: hybrid = 0.5*text + 0.5*dsp (concatenated blocks);
//     method "text"/"dsp" uses only that block. A missing block contributes
//     zeros. Entries with NO signal for the chosen method are excluded to
//     `unassigned`.
//   - Distance: Euclidean on the combined vector. Reported similarity is
//     clamp(1 - dist/sqrt(2), 0, 1) — monotone in distance, bounded [0, 1].
//   - Clustering: k-means with k-means++ init (std::mt19937 seed 42),
//     max 100 iterations, empty cluster reseeded at the entry farthest from
//     its centroid. k > 0 -> clamped to [1, n]; k == 0 -> auto: try
//     k = 2..min(8, max(2, n/3)), keep the max mean silhouette (ties ->
//     smaller k).
//   - Output: clusters sorted by size desc then label asc, ids "c1".."cK";
//     members sorted by similarity desc then name asc. Label = highest
//     summed TF-IDF token across member texts (fallback "cluster N").
//
// No JUCE audio dependencies. Safe to call from any non-audio thread against
// an in-memory snapshot (caller copies entries under its own mutex).

#include <juce_core/juce_core.h>
#include <vector>

namespace HDAW {

// Sidecar `dsp` keys, fixed order (kDspFeatureKeys). A dsp vector is accepted
// only when ALL 20 values are present and finite — no partial vectors, no
// imputation (features stay empty).
inline constexpr int kDspFeatureCount = 20;
inline constexpr const char* kDspFeatureKeys[kDspFeatureCount] = {
    "duration", "rms", "peak", "crest_dB", "zcr", "centroid", "bandwidth",
    "rolloff85", "rolloff95", "flatness", "spectral_crest", "spec_irregularity",
    "mel_low", "mel_mid", "mel_high", "attack_s", "decay_s", "f0_hz",
    "tonal_fraction", "f0_sweep"
};

struct ClusterItem {
    juce::String name;
    juce::String path;
    juce::String tags;
    juce::String description;
    std::vector<double> dsp; // empty, or exactly kDspFeatureCount finite values
};

enum class ClusterMethod { Hybrid, Text, Dsp };

struct ClusterMember {
    juce::String name;
    juce::String path;
    juce::String tags;
    juce::String description;
    double similarity = 0.0; // similarity-to-centroid (related: to the seed)
};

struct Cluster {
    juce::String id;    // "c1".."cK"
    juce::String label; // top summed TF-IDF token, or "cluster N"
    std::vector<ClusterMember> members;
};

struct ClusterOutcome {
    juce::String method; // "hybrid" | "text" | "dsp"
    int k = 0;           // number of emitted clusters
    std::vector<Cluster> clusters;       // size desc, then label asc
    std::vector<ClusterMember> unassigned; // entries with no signal (name/path)
    juce::String note;   // set when n > 4000 (O(n^2) complexity warning)
};

struct RelatedHit {
    juce::String name;
    juce::String path;
    juce::String tags;
    juce::String description;
    double similarity = 0.0;
};

struct RelatedResult {
    juce::String method; // "hybrid" | "text" | "dsp"
    bool found = false;          // filePath seed exists among the items
    bool hasSeedSignal = false;  // seed/query vector carries usable signal
    juce::String seedName;       // valid when found
    juce::String seedPath;       // valid when found
    std::vector<RelatedHit> results; // similarity desc, then name asc
};

// Cluster `items` into k groups (k == 0 -> auto-k). Deterministic.
ClusterOutcome cluster(const std::vector<ClusterItem>& items, int k,
                       ClusterMethod method);

// Nearest neighbours of the entry whose path == filePath (seed excludes
// itself; entries without signal for the method are skipped). limit is
// clamped to [1, 100] (<= 0 -> 10).
RelatedResult relatedToItem(const std::vector<ClusterItem>& items,
                            const juce::String& filePath,
                            ClusterMethod method, int limit);

// Nearest neighbours of a text query (idf-weighted pseudo-vector on the text
// axis; the dsp block stays zero, so method Dsp returns no signal).
RelatedResult relatedToQuery(const std::vector<ClusterItem>& items,
                             const juce::String& query,
                             ClusterMethod method, int limit);

} // namespace HDAW
