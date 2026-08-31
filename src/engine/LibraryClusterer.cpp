// src/engine/LibraryClusterer.cpp
#include "LibraryClusterer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <random>
#include <set>
#include <unordered_map>

namespace HDAW {
namespace {

// ── tokenization ─────────────────────────────────────────────────────────────

// [a-z0-9']+ over the lowercased text, tokens longer than one char.
// Non-ASCII bytes (>= 0x80) act as separators — deterministic, ASCII-only.
std::vector<std::string> tokenize(const juce::String& text) {
    std::vector<std::string> tokens;
    const std::string s = text.toLowerCase().toStdString();
    std::string cur;
    for (const unsigned char c : s) {
        const bool inClass = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '\'';
        if (inClass) {
            cur.push_back(static_cast<char>(c));
        } else {
            if (cur.size() > 1) tokens.push_back(cur);
            cur.clear();
        }
    }
    if (cur.size() > 1) tokens.push_back(cur);
    return tokens;
}

// ── vectorization ────────────────────────────────────────────────────────────

struct TextModel {
    std::vector<std::string> vocab;             // top 512 by df desc, token asc
    std::unordered_map<std::string, int> index; // token -> vocab index
    std::vector<double> idf;
    std::vector<std::vector<double>> vec;       // L2-normalized tf-idf rows
    std::vector<char> has;                      // row carries >= 1 vocab token
};

constexpr size_t kVocabCap = 512;

TextModel buildTextModel(const std::vector<ClusterItem>& items) {
    TextModel m;
    const size_t n = items.size();
    std::vector<std::vector<std::string>> docs(n);
    std::unordered_map<std::string, int> df;
    for (size_t i = 0; i < n; ++i) {
        docs[i] = tokenize(items[i].tags + " " + items[i].description);
        std::set<std::string> seen(docs[i].begin(), docs[i].end());
        for (const auto& t : seen) df[t] += 1;
    }
    // Vocabulary: top 512 tokens by document frequency, tiebreak token asc.
    std::vector<std::pair<int, std::string>> byDf;
    byDf.reserve(df.size());
    for (const auto& kv : df) byDf.emplace_back(kv.second, kv.first);
    std::sort(byDf.begin(), byDf.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first > b.first;
        return a.second < b.second;
    });
    const size_t vocabSize = std::min(byDf.size(), kVocabCap);
    m.vocab.resize(vocabSize);
    m.idf.assign(vocabSize, 0.0);
    for (size_t i = 0; i < vocabSize; ++i) {
        m.vocab[i] = byDf[i].second;
        m.index[m.vocab[i]] = static_cast<int>(i);
        // Smoothed idf (always > 0, sklearn-style) — deterministic.
        m.idf[i] = std::log((1.0 + static_cast<double>(n)) / (1.0 + static_cast<double>(byDf[i].first))) + 1.0;
    }
    m.vec.assign(n, std::vector<double>(vocabSize, 0.0));
    m.has.assign(n, 0);
    for (size_t i = 0; i < n; ++i) {
        auto& row = m.vec[i];
        for (const auto& t : docs[i]) {
            const auto it = m.index.find(t);
            if (it != m.index.end()) row[static_cast<size_t>(it->second)] += 1.0;
        }
        double norm = 0.0;
        for (size_t t = 0; t < vocabSize; ++t) {
            row[t] *= m.idf[t];
            norm += row[t] * row[t];
        }
        norm = std::sqrt(norm);
        if (norm > 0.0) {
            for (auto& v : row) v /= norm;
            m.has[i] = 1;
        }
    }
    return m;
}

struct DspModel {
    std::vector<std::vector<double>> vec; // z-scored, L2-normalized (or zero)
    std::vector<char> has;                // item carries a full finite dsp vector
};

DspModel buildDspModel(const std::vector<ClusterItem>& items) {
    DspModel m;
    const size_t n = items.size();
    m.has.assign(n, 0);
    m.vec.assign(n, std::vector<double>(kDspFeatureCount, 0.0));
    std::vector<std::vector<double>> raw(n);
    for (size_t i = 0; i < n; ++i) {
        if (items[i].dsp.size() != static_cast<size_t>(kDspFeatureCount)) continue;
        bool finite = true;
        for (const double d : items[i].dsp)
            if (!std::isfinite(d)) { finite = false; break; }
        if (finite) {
            raw[i] = items[i].dsp;
            m.has[i] = 1;
        }
    }
    // z-score each dim across the entries that HAVE dsp (population std).
    for (int d = 0; d < kDspFeatureCount; ++d) {
        double sum = 0.0;
        int cnt = 0;
        for (size_t i = 0; i < n; ++i)
            if (m.has[i]) { sum += raw[i][static_cast<size_t>(d)]; ++cnt; }
        if (cnt == 0) continue;
        const double mean = sum / static_cast<double>(cnt);
        double var = 0.0;
        for (size_t i = 0; i < n; ++i)
            if (m.has[i]) { const double dd = raw[i][static_cast<size_t>(d)] - mean; var += dd * dd; }
        const double stdev = std::sqrt(var / static_cast<double>(cnt));
        // std == 0 -> dim leaves an all-zero contribution (dropped from scaling)
        for (size_t i = 0; i < n; ++i)
            if (m.has[i]) m.vec[i][static_cast<size_t>(d)] = (stdev > 0.0) ? (raw[i][static_cast<size_t>(d)] - mean) / stdev : 0.0;
    }
    for (size_t i = 0; i < n; ++i) {
        if (!m.has[i]) continue;
        double norm = 0.0;
        for (const double v : m.vec[i]) norm += v * v;
        norm = std::sqrt(norm);
        if (norm > 0.0)
            for (auto& v : m.vec[i]) v /= norm;
    }
    return m;
}

double sqDist(const std::vector<double>& a, const std::vector<double>& b) {
    const size_t n = std::min(a.size(), b.size());
    double s = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double d = a[i] - b[i];
        s += d * d;
    }
    return s;
}

// Combined vector = concat(textBlock, dspBlock) with per-method weights:
// hybrid 0.5/0.5, single-axis 1.0 on the used block and zeros elsewhere.
std::vector<double> combine(const std::vector<double>& text,
                            const std::vector<double>& dsp,
                            ClusterMethod method) {
    std::vector<double> out(text.size() + static_cast<size_t>(kDspFeatureCount), 0.0);
    const double wText = (method == ClusterMethod::Hybrid) ? 0.5 : 1.0;
    const double wDsp = (method == ClusterMethod::Hybrid) ? 0.5 : 1.0;
    if (method != ClusterMethod::Dsp)
        for (size_t i = 0; i < text.size(); ++i) out[i] = wText * text[i];
    if (method != ClusterMethod::Text)
        for (int i = 0; i < kDspFeatureCount; ++i)
            out[text.size() + static_cast<size_t>(i)] = wDsp * dsp[static_cast<size_t>(i)];
    return out;
}

bool hasSignalForMethod(bool hasText, bool hasDsp, ClusterMethod method) {
    switch (method) {
        case ClusterMethod::Text: return hasText;
        case ClusterMethod::Dsp:  return hasDsp;
        default:                  return hasText || hasDsp;
    }
}

double similarityFromDist(double dist) {
    const double s = 1.0 - dist / std::sqrt(2.0);
    return std::clamp(s, 0.0, 1.0);
}

// ── k-means (k-means++ init, mt19937 seed 42) ────────────────────────────────

std::vector<int> runKMeans(const std::vector<std::vector<double>>& pts, int kRequested) {
    const int n = static_cast<int>(pts.size());
    if (n == 0) return {};
    int k = std::clamp(kRequested, 1, n);

    std::mt19937 rng(42);
    std::vector<std::vector<double>> centers;
    centers.reserve(static_cast<size_t>(k));
    std::vector<double> best2(static_cast<size_t>(n), 0.0);

    // k-means++ seeding. Raw mt19937 draws (no distribution objects) so the
    // sequence is fully specified by the standard.
    centers.push_back(pts[static_cast<size_t>(rng() % static_cast<std::uint32_t>(n))]);
    for (int i = 0; i < n; ++i) best2[static_cast<size_t>(i)] = sqDist(pts[static_cast<size_t>(i)], centers[0]);
    while (static_cast<int>(centers.size()) < k) {
        double total = 0.0;
        for (int i = 0; i < n; ++i) total += best2[static_cast<size_t>(i)];
        int next = -1;
        if (total <= 0.0) {
            // All remaining candidates duplicate a center — take the first
            // point that is not already a center (ascending, deterministic).
            for (int i = 0; i < n && next < 0; ++i) {
                bool isCenter = false;
                for (const auto& c : centers)
                    if (c == pts[static_cast<size_t>(i)]) { isCenter = true; break; }
                if (!isCenter) next = i;
            }
            if (next < 0) next = static_cast<int>(centers.size()) % n;
        } else {
            const double r = (static_cast<double>(rng()) / static_cast<double>(0xffffffffu)) * total;
            double cum = 0.0;
            for (int i = 0; i < n; ++i) {
                cum += best2[static_cast<size_t>(i)];
                if (cum > r) { next = i; break; }
            }
            if (next < 0) next = n - 1;
        }
        centers.push_back(pts[static_cast<size_t>(next)]);
        for (int i = 0; i < n; ++i) {
            const double d2 = sqDist(pts[static_cast<size_t>(i)], centers.back());
            if (d2 < best2[static_cast<size_t>(i)]) best2[static_cast<size_t>(i)] = d2;
        }
    }

    // Lloyd iterations (assignment ties keep the lowest cluster index).
    constexpr int kMaxIters = 100;
    const size_t dims = pts[0].size();
    std::vector<int> assign(static_cast<size_t>(n), -1);
    for (int iter = 0; iter < kMaxIters; ++iter) {
        bool changed = false;
        for (int i = 0; i < n; ++i) {
            int best = 0;
            double bd = sqDist(pts[static_cast<size_t>(i)], centers[0]);
            for (int c = 1; c < static_cast<int>(centers.size()); ++c) {
                const double d = sqDist(pts[static_cast<size_t>(i)], centers[static_cast<size_t>(c)]);
                if (d < bd) { bd = d; best = c; }
            }
            if (assign[static_cast<size_t>(i)] != best) {
                assign[static_cast<size_t>(i)] = best;
                changed = true;
            }
        }
        if (!changed && iter > 0) break;

        std::vector<int> counts(centers.size(), 0);
        std::vector<std::vector<double>> sums(centers.size(), std::vector<double>(dims, 0.0));
        for (int i = 0; i < n; ++i) {
            const auto c = static_cast<size_t>(assign[static_cast<size_t>(i)]);
            ++counts[c];
            for (size_t d = 0; d < dims; ++d) sums[c][d] += pts[static_cast<size_t>(i)][d];
        }
        for (size_t c = 0; c < centers.size(); ++c) {
            if (counts[c] > 0) {
                for (size_t d = 0; d < dims; ++d) centers[c][d] = sums[c][d] / static_cast<double>(counts[c]);
            } else {
                // Empty cluster -> reseed at the entry farthest from its own
                // centroid (ties keep the lowest index).
                int far = -1;
                double fd = -1.0;
                for (int i = 0; i < n; ++i) {
                    const double d = sqDist(pts[static_cast<size_t>(i)], centers[static_cast<size_t>(assign[static_cast<size_t>(i)])]);
                    if (d > fd) { fd = d; far = i; }
                }
                if (far >= 0) centers[c] = pts[static_cast<size_t>(far)];
            }
        }
    }
    return assign;
}

// Mean silhouette, O(n^2) time / O(n + k) space. Singletons score 0.
double meanSilhouette(const std::vector<std::vector<double>>& pts,
                      const std::vector<int>& assign, int k) {
    const int n = static_cast<int>(pts.size());
    if (n < 2 || k < 2) return 0.0;
    double total = 0.0;
    std::vector<double> sum(static_cast<size_t>(k), 0.0), cnt(static_cast<size_t>(k), 0.0);
    for (int i = 0; i < n; ++i) {
        std::fill(sum.begin(), sum.end(), 0.0);
        std::fill(cnt.begin(), cnt.end(), 0.0);
        for (int j = 0; j < n; ++j) {
            if (j == i) continue;
            const auto c = static_cast<size_t>(assign[static_cast<size_t>(j)]);
            sum[c] += std::sqrt(sqDist(pts[static_cast<size_t>(i)], pts[static_cast<size_t>(j)]));
            cnt[c] += 1.0;
        }
        const size_t own = static_cast<size_t>(assign[static_cast<size_t>(i)]);
        if (cnt[own] <= 0.0) continue; // singleton cluster -> s(i) = 0
        const double a = sum[own] / cnt[own];
        double b = -1.0;
        for (int c = 0; c < k; ++c) {
            if (c == own || cnt[static_cast<size_t>(c)] <= 0.0) continue;
            const double mv = sum[static_cast<size_t>(c)] / cnt[static_cast<size_t>(c)];
            if (b < 0.0 || mv < b) b = mv;
        }
        if (b < 0.0) continue; // only one populated cluster
        const double denom = std::max(a, b);
        if (denom > 0.0) total += (b - a) / denom;
    }
    return total / static_cast<double>(n);
}

juce::String methodName(ClusterMethod method) {
    switch (method) {
        case ClusterMethod::Text: return "text";
        case ClusterMethod::Dsp:  return "dsp";
        default:                  return "hybrid";
    }
}

// Shared ranking core for relatedToItem / relatedToQuery: distance from a
// seed vector to every signal-carrying item, similarity desc then name asc.
std::vector<RelatedHit> rankBySeed(const std::vector<ClusterItem>& items,
                                   const TextModel& text, const DspModel& dsp,
                                   ClusterMethod method, int excludeIndex,
                                   const std::vector<double>& seedVec, int limit) {
    if (limit <= 0) limit = 10;
    limit = std::clamp(limit, 1, 100);
    std::vector<RelatedHit> hits;
    for (size_t i = 0; i < items.size(); ++i) {
        if (static_cast<int>(i) == excludeIndex) continue;
        if (!hasSignalForMethod(text.has[i] != 0, dsp.has[i] != 0, method)) continue;
        RelatedHit h;
        h.name = items[i].name;
        h.path = items[i].path;
        h.tags = items[i].tags;
        h.description = items[i].description;
        h.similarity = similarityFromDist(
            std::sqrt(sqDist(combine(text.vec[i], dsp.vec[i], method), seedVec)));
        hits.push_back(std::move(h));
    }
    std::sort(hits.begin(), hits.end(), [](const RelatedHit& a, const RelatedHit& b) {
        if (a.similarity != b.similarity) return a.similarity > b.similarity;
        return a.name < b.name;
    });
    if (static_cast<int>(hits.size()) > limit)
        hits.resize(static_cast<size_t>(limit));
    return hits;
}

ClusterMember toUnassigned(const ClusterItem& item) {
    ClusterMember m;
    m.name = item.name;
    m.path = item.path;
    m.tags = item.tags;
    m.description = item.description;
    m.similarity = 0.0;
    return m;
}

} // namespace

// ── public API ───────────────────────────────────────────────────────────────

ClusterOutcome cluster(const std::vector<ClusterItem>& items, int k,
                       ClusterMethod method) {
    ClusterOutcome out;
    out.method = methodName(method);
    const int n = static_cast<int>(items.size());
    if (n == 0) {
        out.k = 0;
        return out; // empty library -> empty result, no crash
    }
    if (n > 4000)
        out.note = juce::String("n=") + juce::String(n)
                 + ": pairwise distances are O(n^2); clustering may be slow";

    const TextModel text = buildTextModel(items);
    const DspModel dsp = buildDspModel(items);

    std::vector<int> idx; // signal-carrying item indices, ascending
    idx.reserve(items.size());
    for (int i = 0; i < n; ++i) {
        if (hasSignalForMethod(text.has[static_cast<size_t>(i)] != 0,
                               dsp.has[static_cast<size_t>(i)] != 0, method))
            idx.push_back(i);
        else
            out.unassigned.push_back(toUnassigned(items[static_cast<size_t>(i)]));
    }
    if (idx.empty()) {
        out.k = 0;
        return out; // every entry lacks signal for the method
    }

    std::vector<std::vector<double>> pts;
    pts.reserve(idx.size());
    for (const int i : idx)
        pts.push_back(combine(text.vec[static_cast<size_t>(i)],
                              dsp.vec[static_cast<size_t>(i)], method));
    const int m = static_cast<int>(pts.size());

    std::vector<int> assign;
    if (k > 0) {
        assign = runKMeans(pts, std::clamp(k, 1, m));
    } else {
        // auto-k: 2..min(8, max(2, m/3)), max mean silhouette, ties -> smaller k
        const int kmax = std::min(8, std::max(2, m / 3));
        double bestScore = -2.0;
        int bestK = 1;
        for (int kk = 2; kk <= kmax; ++kk) {
            const auto a = runKMeans(pts, kk);
            const double s = meanSilhouette(pts, a, kk);
            if (s > bestScore) { bestScore = s; assign = a; bestK = kk; }
        }
        if (assign.empty()) assign = runKMeans(pts, 1); // m == 1 (kmax clamps)
    }

    // Group points per cluster (map -> ascending cluster index).
    std::map<int, std::vector<int>> byCluster;
    for (int p = 0; p < m; ++p) byCluster[assign[static_cast<size_t>(p)]].push_back(p);

    struct RawCluster {
        std::vector<int> members; // point indices
        juce::String label;
    };
    std::vector<RawCluster> raw;
    for (const auto& kv : byCluster) {
        RawCluster rc;
        rc.members = kv.second;
        // Label: highest summed tf-idf token across the member texts
        // (ascending scan keeps the first max — lowest vocab index).
        std::vector<double> summed(text.vocab.size(), 0.0);
        for (const int p : rc.members) {
            const auto& row = text.vec[static_cast<size_t>(idx[static_cast<size_t>(p)])];
            for (size_t t = 0; t < row.size(); ++t) summed[t] += row[t];
        }
        int bestT = -1;
        double bestV = 0.0;
        for (size_t t = 0; t < summed.size(); ++t)
            if (summed[t] > bestV) { bestV = summed[t]; bestT = static_cast<int>(t); }
        rc.label = (bestT >= 0) ? juce::String(text.vocab[static_cast<size_t>(bestT)])
                                : juce::String("cluster ") + juce::String(raw.size() + 1);
        raw.push_back(std::move(rc));
    }

    for (const auto& rc : raw) {
        std::vector<double> cent(pts[0].size(), 0.0);
        for (const int p : rc.members)
            for (size_t d = 0; d < cent.size(); ++d)
                cent[d] += pts[static_cast<size_t>(p)][d];
        for (auto& v : cent) v /= static_cast<double>(rc.members.size());

        Cluster cl;
        cl.label = rc.label;
        for (const int p : rc.members) {
            const auto& item = items[static_cast<size_t>(idx[static_cast<size_t>(p)])];
            ClusterMember mem;
            mem.name = item.name;
            mem.path = item.path;
            mem.tags = item.tags;
            mem.description = item.description;
            mem.similarity = similarityFromDist(
                std::sqrt(sqDist(pts[static_cast<size_t>(p)], cent)));
            cl.members.push_back(std::move(mem));
        }
        std::sort(cl.members.begin(), cl.members.end(),
                  [](const ClusterMember& a, const ClusterMember& b) {
                      if (a.similarity != b.similarity) return a.similarity > b.similarity;
                      return a.name < b.name;
                  });
        out.clusters.push_back(std::move(cl));
    }
    std::sort(out.clusters.begin(), out.clusters.end(),
              [](const Cluster& a, const Cluster& b) {
                  if (a.members.size() != b.members.size()) return a.members.size() > b.members.size();
                  return a.label < b.label;
              });
    for (size_t i = 0; i < out.clusters.size(); ++i)
        out.clusters[i].id = "c" + juce::String(static_cast<int>(i) + 1);
    out.k = static_cast<int>(out.clusters.size());
    return out;
}

RelatedResult relatedToItem(const std::vector<ClusterItem>& items,
                            const juce::String& filePath,
                            ClusterMethod method, int limit) {
    RelatedResult r;
    r.method = methodName(method);
    const int n = static_cast<int>(items.size());
    int seed = -1;
    // Normalize the query path through juce::File for case-insensitive,
    // separator-agnostic comparison (handoff 2026-08-30 §5 — "entry not
    // found" when cluster output path used different case/separator).
    const juce::File queryFile(filePath);
    for (int i = 0; i < n; ++i)
    {
        const juce::File entryFile(items[static_cast<size_t>(i)].path);
        if (entryFile == queryFile) { seed = i; break; }
    }
    if (seed < 0) return r; // found = false
    r.found = true;
    r.seedName = items[static_cast<size_t>(seed)].name;
    r.seedPath = items[static_cast<size_t>(seed)].path;

    const TextModel text = buildTextModel(items);
    const DspModel dsp = buildDspModel(items);
    if (!hasSignalForMethod(text.has[static_cast<size_t>(seed)] != 0,
                            dsp.has[static_cast<size_t>(seed)] != 0, method))
        return r; // found but the seed carries no comparable signal

    r.hasSeedSignal = true;
    const auto seedVec = combine(text.vec[static_cast<size_t>(seed)],
                                 dsp.vec[static_cast<size_t>(seed)], method);
    r.results = rankBySeed(items, text, dsp, method, seed, seedVec, limit);
    return r;
}

RelatedResult relatedToQuery(const std::vector<ClusterItem>& items,
                             const juce::String& query,
                             ClusterMethod method, int limit) {
    RelatedResult r;
    r.method = methodName(method);
    if (method == ClusterMethod::Dsp)
        return r; // a text query has no dsp axis — caller must reject

    const TextModel text = buildTextModel(items);
    const DspModel dsp = buildDspModel(items);

    // idf-weighted pseudo-vector on the text axis, L2-normalized.
    std::vector<double> pseudo(text.vocab.size(), 0.0);
    for (const auto& t : tokenize(query)) {
        const auto it = text.index.find(t);
        if (it != text.index.end()) pseudo[static_cast<size_t>(it->second)] += 1.0;
    }
    double norm = 0.0;
    for (size_t t = 0; t < pseudo.size(); ++t) {
        pseudo[t] *= text.idf[t];
        norm += pseudo[t] * pseudo[t];
    }
    norm = std::sqrt(norm);
    if (norm <= 0.0)
        return r; // query matched no indexed term — no usable signal
    for (auto& v : pseudo) v /= norm;

    r.hasSeedSignal = true;
    const auto seedVec = combine(pseudo,
                                 std::vector<double>(static_cast<size_t>(kDspFeatureCount), 0.0),
                                 method);
    r.results = rankBySeed(items, text, dsp, method, -1, seedVec, limit);
    return r;
}

} // namespace HDAW
