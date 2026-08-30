// tests/unit/engine/library_clusterer_test.cpp
// LibraryClustererTest — deterministic clustering + nearest-neighbour over
// TimbreLib sidecar data (docs/plans/2026-08-25-library-clustering.md, G2).
// Synthetic in-memory fixtures only — no files on disk, no engine bootstrap.

#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include "engine/LibraryClusterer.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

// 20-dim dsp vector: plausible constants everywhere, 3 dims carry the family
// signal (centroid / mel_low / mel_high) — enough for z-scored separation.
std::vector<double> makeDsp(double centroid, double melLow, double melHigh) {
    std::vector<double> v(HDAW::kDspFeatureCount, 0.0);
    v[0] = 2.0;      // duration
    v[1] = 0.1;      // rms
    v[2] = 0.5;      // peak
    v[3] = 12.0;     // crest_dB
    v[4] = 0.1;      // zcr
    v[5] = centroid; // centroid
    v[6] = 800.0;    // bandwidth
    v[7] = 500.0;    // rolloff85
    v[8] = 2000.0;   // rolloff95
    v[9] = 0.05;     // flatness
    v[10] = 300.0;   // spectral_crest
    v[11] = 0.2;     // spec_irregularity
    v[12] = melLow;  // mel_low
    v[13] = 0.2;     // mel_mid
    v[14] = melHigh; // mel_high
    v[15] = 0.01;    // attack_s
    v[16] = 0.5;     // decay_s
    v[17] = 0.0;     // f0_hz
    v[18] = 0.0;     // tonal_fraction
    v[19] = 9.0;     // f0_sweep
    return v;
}

HDAW::ClusterItem item(const juce::String& name, const juce::String& tags,
                       const juce::String& description, std::vector<double> dsp = {}) {
    HDAW::ClusterItem it;
    it.name = name;
    it.path = "/lib/" + name;
    it.tags = tags;
    it.description = description;
    it.dsp = std::move(dsp);
    return it;
}

// Full deterministic serialization of a ClusterOutcome (fixed precision) —
// used for the byte-identical determinism check.
juce::String dump(const HDAW::ClusterOutcome& o) {
    juce::String s = o.method + "|" + juce::String(o.k) + "|" + o.note + "|U:";
    for (const auto& u : o.unassigned) s += u.name + "," + u.path + ";";
    s += "|C:";
    for (const auto& c : o.clusters) {
        s += c.id + "=" + c.label + "[";
        for (const auto& m : c.members)
            s += m.name + "@" + juce::String(m.similarity, 12) + ";";
        s += "]";
    }
    return s;
}

juce::String dump(const HDAW::RelatedResult& r) {
    juce::String s = r.method + "|" + (r.found ? "F" : "f") + (r.hasSeedSignal ? "S" : "s")
                   + "|" + r.seedName + "|R:";
    for (const auto& h : r.results)
        s += h.name + "@" + juce::String(h.similarity, 12) + ";";
    return s;
}

std::vector<juce::String> memberNames(const HDAW::Cluster& c) {
    std::vector<juce::String> names;
    for (const auto& m : c.members) names.push_back(m.name);
    return names;
}

bool contains(const std::vector<juce::String>& names, const juce::String& n) {
    return std::find(names.begin(), names.end(), n) != names.end();
}

// Three well-separated dsp families x 4 entries each (jitter keeps vectors
// distinct but tightly grouped).
std::vector<HDAW::ClusterItem> threeDspFamilies() {
    std::vector<HDAW::ClusterItem> items;
    for (int i = 0; i < 4; ++i)
        items.push_back(item("dark" + juce::String(i), "", "", makeDsp(150.0 + 5.0 * i, 0.75, 0.05)));
    for (int i = 0; i < 4; ++i)
        items.push_back(item("warm" + juce::String(i), "", "", makeDsp(3000.0 + 60.0 * i, 0.4, 0.3)));
    for (int i = 0; i < 4; ++i)
        items.push_back(item("bright" + juce::String(i), "", "", makeDsp(9000.0 + 150.0 * i, 0.05, 0.9)));
    return items;
}

} // namespace

TEST(LibraryClustererTest, DeterministicAcrossIdenticalCalls) {
    auto items = threeDspFamilies();
    auto a = HDAW::cluster(items, 3, HDAW::ClusterMethod::Hybrid);
    auto b = HDAW::cluster(items, 3, HDAW::ClusterMethod::Hybrid);
    EXPECT_EQ(dump(a), dump(b)) << "two identical cluster() calls must be byte-identical";

    auto ra = HDAW::relatedToItem(items, "/lib/dark1", HDAW::ClusterMethod::Dsp, 10);
    auto rb = HDAW::relatedToItem(items, "/lib/dark1", HDAW::ClusterMethod::Dsp, 10);
    EXPECT_EQ(dump(ra), dump(rb)) << "two identical relatedToItem() calls must be byte-identical";
}

TEST(LibraryClustererTest, DspThreeGroupSeparation) {
    auto items = threeDspFamilies();
    auto outcome = HDAW::cluster(items, 3, HDAW::ClusterMethod::Dsp);
    ASSERT_EQ(outcome.clusters.size(), 3u) << "k=3 over three tight dsp families";
    EXPECT_EQ(outcome.k, 3);
    EXPECT_TRUE(outcome.unassigned.empty());
    // Each cluster is exactly one family.
    for (const auto& c : outcome.clusters) {
        const auto names = memberNames(c);
        ASSERT_EQ(names.size(), 4u);
        const juce::String family = names[0].dropLastCharacters(1);
        for (const auto& n : names)
            EXPECT_TRUE(n.startsWith(family)) << "cluster " << c.id << " mixed families: " << n;
    }
    EXPECT_EQ(outcome.method, "dsp");
}

TEST(LibraryClustererTest, TextGroupingBySharedTags) {
    std::vector<HDAW::ClusterItem> items = {
        item("moody1", "dark moody pad", "A dark moody pad texture.", {}),
        item("moody2", "dark moody drone", "A dark moody drone bed.", {}),
        item("chime1", "bright chime bell", "A bright chime bell hit.", {}),
        item("chime2", "bright bell chime", "A bright bell chime ring.", {}),
        item("clang1", "metallic clang noise", "A metallic clang noise burst.", {}),
        item("clang2", "metallic noise clang", "A metallic noise clang hit.", {}),
    };
    auto outcome = HDAW::cluster(items, 3, HDAW::ClusterMethod::Text);
    ASSERT_EQ(outcome.clusters.size(), 3u);
    for (const auto& c : outcome.clusters) {
        const auto names = memberNames(c);
        ASSERT_EQ(names.size(), 2u);
        const juce::String family = names[0].dropLastCharacters(1);
        for (const auto& n : names)
            EXPECT_TRUE(n.startsWith(family)) << "text cluster mixed families: " << n;
    }
}

TEST(LibraryClustererTest, KRespectedAndClampedToEntryCount) {
    std::vector<HDAW::ClusterItem> items = {
        item("a", "", "", makeDsp(100, 0.8, 0.0)),
        item("b", "", "", makeDsp(2000, 0.5, 0.4)),
        item("c", "", "", makeDsp(5000, 0.3, 0.6)),
        item("d", "", "", makeDsp(9500, 0.0, 0.95)),
    };
    auto outcome = HDAW::cluster(items, 10, HDAW::ClusterMethod::Dsp);
    EXPECT_EQ(outcome.k, 4) << "k=10 over 4 distinct entries clamps to 4";
    ASSERT_EQ(outcome.clusters.size(), 4u);
    // Each cluster is a singleton with a "cN" id.
    for (size_t i = 0; i < outcome.clusters.size(); ++i) {
        EXPECT_EQ(outcome.clusters[i].id, "c" + juce::String((int)i + 1));
        EXPECT_EQ(outcome.clusters[i].members.size(), 1u);
    }
}

TEST(LibraryClustererTest, AutoKPicksBestSilhouette) {
    auto items = threeDspFamilies();
    auto outcome = HDAW::cluster(items, 0, HDAW::ClusterMethod::Dsp);
    EXPECT_EQ(outcome.k, 3) << "auto-k should discover the three dsp families";
    ASSERT_EQ(outcome.clusters.size(), 3u);
    // Deterministic: the same auto run repeats identically.
    auto again = HDAW::cluster(items, 0, HDAW::ClusterMethod::Dsp);
    EXPECT_EQ(dump(outcome), dump(again));
}

TEST(LibraryClustererTest, UnassignedNoSignalEntries) {
    std::vector<HDAW::ClusterItem> items = {
        item("texty1", "dark pad", "dark pad", {}),
        item("texty2", "dark pad drone", "dark pad drone", {}),
        item("empty1", "", "", {}), // no text, no dsp -> unassigned
    };
    auto outcome = HDAW::cluster(items, 2, HDAW::ClusterMethod::Hybrid);
    ASSERT_EQ(outcome.unassigned.size(), 1u);
    EXPECT_EQ(outcome.unassigned[0].name, "empty1");
    EXPECT_EQ(outcome.unassigned[0].path, "/lib/empty1");
    ASSERT_EQ(outcome.clusters.size(), 2u);
    int totalMembers = 0;
    for (const auto& c : outcome.clusters) totalMembers += (int)c.members.size();
    EXPECT_EQ(totalMembers, 2) << "unassigned entry must not appear in clusters";

    // Single-axis method: entries missing THAT axis are unassigned too.
    auto dspOnly = HDAW::cluster(items, 2, HDAW::ClusterMethod::Dsp);
    ASSERT_EQ(dspOnly.unassigned.size(), 3u) << "no entry carries dsp -> all unassigned";
    EXPECT_TRUE(dspOnly.clusters.empty());
}

TEST(LibraryClustererTest, HybridTextOnlyAndDspOnlyEntriesComparable) {
    std::vector<HDAW::ClusterItem> items = {
        item("t1", "dark moody pad", "", {}),                 // text only
        item("t2", "dark moody drone", "", {}),               // text only
        item("d1", "", "", makeDsp(150, 0.8, 0.02)),          // dsp only
        item("d2", "", "", makeDsp(155, 0.78, 0.03)),         // dsp only
        item("none", "", "", {}),                             // both missing
    };
    auto outcome = HDAW::cluster(items, 2, HDAW::ClusterMethod::Hybrid);
    ASSERT_EQ(outcome.unassigned.size(), 1u);
    EXPECT_EQ(outcome.unassigned[0].name, "none");
    EXPECT_EQ(outcome.k, 2) << "the two text entries and two dsp entries form the groups";
    int totalMembers = 0;
    for (const auto& c : outcome.clusters) totalMembers += (int)c.members.size();
    EXPECT_EQ(totalMembers, 4);
}

TEST(LibraryClustererTest, SingleEntryLibraryReturnsOneCluster) {
    std::vector<HDAW::ClusterItem> items = { item("only", "lone pad", "a lone pad", makeDsp(400, 0.5, 0.1)) };
    auto outcome = HDAW::cluster(items, 0, HDAW::ClusterMethod::Hybrid);
    ASSERT_EQ(outcome.clusters.size(), 1u) << "auto-k clamps to 1 when n == 1";
    EXPECT_EQ(outcome.k, 1);
    EXPECT_EQ(outcome.clusters[0].members.size(), 1u);
    EXPECT_EQ(outcome.clusters[0].members[0].name, "only");
    EXPECT_GT(outcome.clusters[0].members[0].similarity, 0.9)
        << "a singleton sits on its centroid — similarity ~ 1";
    EXPECT_TRUE(outcome.unassigned.empty());
}

TEST(LibraryClustererTest, EmptyLibraryReturnsEmptyResult) {
    std::vector<HDAW::ClusterItem> items;
    auto outcome = HDAW::cluster(items, 0, HDAW::ClusterMethod::Hybrid);
    EXPECT_EQ(outcome.k, 0);
    EXPECT_TRUE(outcome.clusters.empty());
    EXPECT_TRUE(outcome.unassigned.empty());

    auto r = HDAW::relatedToItem(items, "/lib/x", HDAW::ClusterMethod::Hybrid, 10);
    EXPECT_FALSE(r.found);
    auto rq = HDAW::relatedToQuery(items, "dark", HDAW::ClusterMethod::Hybrid, 10);
    EXPECT_FALSE(rq.hasSeedSignal);
}

TEST(LibraryClustererTest, RelatedToItemRanksTrueNeighborsFirst) {
    std::vector<HDAW::ClusterItem> items = {
        item("dark1", "", "", makeDsp(150, 0.75, 0.05)),
        item("dark2", "", "", makeDsp(152, 0.74, 0.06)),
        item("dark3", "", "", makeDsp(154, 0.73, 0.07)),
        item("bright1", "", "", makeDsp(9000, 0.05, 0.9)),
        item("bright2", "", "", makeDsp(9100, 0.04, 0.91)),
    };
    auto r = HDAW::relatedToItem(items, "/lib/dark1", HDAW::ClusterMethod::Dsp, 10);
    ASSERT_TRUE(r.found);
    ASSERT_TRUE(r.hasSeedSignal);
    EXPECT_EQ(r.seedName, "dark1");
    ASSERT_EQ(r.results.size(), 4u) << "seed excludes itself";
    EXPECT_EQ(r.results[0].name, "dark2");
    EXPECT_EQ(r.results[1].name, "dark3");
    EXPECT_TRUE(r.results[0].similarity > r.results[3].similarity)
        << "same-family neighbours must outrank the far family";
    for (const auto& h : r.results)
        EXPECT_NE(h.name, "dark1") << "the seed never appears in results";
    // Monotone ranking.
    for (size_t i = 1; i < r.results.size(); ++i)
        EXPECT_GE(r.results[i - 1].similarity, r.results[i].similarity);
}

TEST(LibraryClustererTest, RelatedToQueryMatchesTagWords) {
    std::vector<HDAW::ClusterItem> items = {
        item("pad1", "dark pad", "A dark sustained pad.", {}),
        item("drone1", "dark drone", "A dark drone bed.", {}),
        item("chime1", "bright chime", "A bright chime bell.", {}),
        item("chime2", "bright bell", "A bright bell ring.", {}),
    };
    auto r = HDAW::relatedToQuery(items, "dark", HDAW::ClusterMethod::Hybrid, 10);
    ASSERT_TRUE(r.hasSeedSignal);
    ASSERT_EQ(r.results.size(), 4u);
    // "dark" appears once in each dark-family doc -> the two tie on similarity
    // and the name-ascending tiebreak orders drone1 before pad1. Both must
    // outrank the bright family.
    EXPECT_EQ(r.results[0].name, "drone1");
    EXPECT_EQ(r.results[1].name, "pad1");
    EXPECT_GT(r.results[1].similarity, r.results[2].similarity)
        << "dark-family entries must outrank the bright family";
    EXPECT_FALSE(r.found) << "a query has no file seed";

    // A query of unknown words carries no signal.
    auto miss = HDAW::relatedToQuery(items, "zzz qqq", HDAW::ClusterMethod::Hybrid, 10);
    EXPECT_FALSE(miss.hasSeedSignal);
    EXPECT_TRUE(miss.results.empty());
}

TEST(LibraryClustererTest, RelatedLimitClampedAndSeedExcluded) {
    std::vector<HDAW::ClusterItem> items = {
        item("s", "", "", makeDsp(150, 0.75, 0.05)),
        item("n1", "", "", makeDsp(151, 0.75, 0.05)),
        item("n2", "", "", makeDsp(152, 0.75, 0.05)),
        item("n3", "", "", makeDsp(153, 0.75, 0.05)),
        item("n4", "", "", makeDsp(154, 0.75, 0.05)),
        item("n5", "", "", makeDsp(155, 0.75, 0.05)),
    };
    auto r2 = HDAW::relatedToItem(items, "/lib/s", HDAW::ClusterMethod::Dsp, 2);
    ASSERT_EQ(r2.results.size(), 2u) << "limit is respected";

    auto rDef = HDAW::relatedToItem(items, "/lib/s", HDAW::ClusterMethod::Dsp, 0);
    EXPECT_EQ(rDef.results.size(), 5u) << "limit <= 0 falls back to the default 10 (capped by candidates)";

    auto rHuge = HDAW::relatedToItem(items, "/lib/s", HDAW::ClusterMethod::Dsp, 1000);
    EXPECT_EQ(rHuge.results.size(), 5u) << "limit clamps to 100 (only 5 candidates)";
}

TEST(LibraryClustererTest, UnknownPathSeedReportsNotFound) {
    auto items = threeDspFamilies();
    auto r = HDAW::relatedToItem(items, "/lib/does-not-exist", HDAW::ClusterMethod::Dsp, 10);
    EXPECT_FALSE(r.found);
    EXPECT_FALSE(r.hasSeedSignal);
    EXPECT_TRUE(r.results.empty());

    auto noSignal = HDAW::relatedToItem(
        { item("bare", "", "", {}) }, "/lib/bare", HDAW::ClusterMethod::Hybrid, 10);
    EXPECT_TRUE(noSignal.found);
    EXPECT_FALSE(noSignal.hasSeedSignal) << "a seed without any signal cannot be ranked";
}

// ── MIDI symbolic vectors (plan 2026-08-28) ──────────────────────────────────
// The numeric axis is dimension-agnostic: the same 20-dim machinery accepts
// kMidiFeatureKeys vectors, and mismatched/empty widths are excluded to
// unassigned — never a crash, never implicit zero-padding.

namespace {

// 20-dim vector in kMidiFeatureKeys order; 3 dims carry the family signal
// (note_density / pitch_centroid / syncopation_fraction).
std::vector<double> makeMidi(double density, double pitchCentroid, double syncopation) {
    std::vector<double> v(HDAW::kMidiFeatureCount, 0.0);
    v[0] = 16.0;          // duration_beats
    v[1] = 160.0;         // note_count
    v[2] = density;       // note_density
    v[3] = 2.5;           // polyphony_mean
    v[4] = 6.0;           // polyphony_max
    v[5] = 36.0;          // pitch_min
    v[6] = 40.0;          // pitch_span
    v[7] = pitchCentroid; // pitch_centroid
    v[8] = 0.7;           // pitch_class_entropy
    v[9] = 0.9;           // scale_fit
    v[10] = 0.6;          // key_confidence
    v[11] = 3.0;          // interval_mean
    v[12] = 0.6;          // interval_entropy
    v[13] = 0.5;          // contour_up_ratio
    v[14] = 0.1;          // note_repetition_rate
    v[15] = 0.25;         // ioi_mean
    v[16] = 0.01;         // grid_deviation
    v[17] = syncopation;  // syncopation_fraction
    v[18] = 90.0;         // velocity_mean
    v[19] = 8.0;          // velocity_std
    return v;
}

// Three well-separated MIDI families x 4 entries: four-on-floor drums /
// offbeat bassline / slow pad chords.
std::vector<HDAW::ClusterItem> threeMidiFamilies() {
    std::vector<HDAW::ClusterItem> items;
    for (int i = 0; i < 4; ++i)
        items.push_back(item("drums" + juce::String(i), "", "", makeMidi(4.0, 52.0, 0.12 + 0.01 * i)));
    for (int i = 0; i < 4; ++i)
        items.push_back(item("bass" + juce::String(i), "", "", makeMidi(2.0, 45.0, 0.46 + 0.01 * i)));
    for (int i = 0; i < 4; ++i)
        items.push_back(item("pad" + juce::String(i), "", "", makeMidi(0.8, 72.0, 0.05 + 0.01 * i)));
    return items;
}

// "drums0" -> "drums" (strip the trailing index digit).
juce::String familyOf(const juce::String& name) {
    return name.substring(0, name.length() - 1);
}

} // namespace

TEST(LibraryClustererTest, MidiVectorsClusterByFamily) {
    auto items = threeMidiFamilies();
    auto out = HDAW::cluster(items, 3, HDAW::ClusterMethod::Dsp);
    ASSERT_EQ(out.clusters.size(), 3u);
    ASSERT_EQ(out.unassigned.size(), 0u);
    EXPECT_EQ(out.method, "dsp");
    std::vector<juce::String> families;
    for (const auto& c : out.clusters) {
        ASSERT_EQ(c.members.size(), 4u);
        const juce::String fam = familyOf(c.members[0].name);
        for (const auto& m : c.members) EXPECT_EQ(familyOf(m.name), fam);
        families.push_back(fam);
    }
    EXPECT_EQ(families.size(), 3u);
}

TEST(LibraryClustererTest, MismatchedVectorWidthsExcludedToUnassigned) {
    std::vector<HDAW::ClusterItem> items;
    items.push_back(item("a0", "", "", makeMidi(4.0, 52.0, 0.12)));
    items.push_back(item("a1", "", "", makeMidi(3.9, 54.0, 0.11)));
    items.push_back(item("a2", "", "", std::vector<double>(7, 1.0)));   // too short
    items.push_back(item("a3", "", "", std::vector<double>(21, 1.0)));  // too long
    items.push_back(item("a4", "", ""));                                 // empty

    auto out = HDAW::cluster(items, 1, HDAW::ClusterMethod::Dsp);
    ASSERT_EQ(out.clusters.size(), 1u);
    ASSERT_EQ(out.clusters[0].members.size(), 2u);
    EXPECT_EQ(out.unassigned.size(), 3u); // a2/a3 wrong width, a4 empty
}

TEST(LibraryClustererTest, AutoKWorksOnMidiVectors) {
    auto items = threeMidiFamilies();
    auto out = HDAW::cluster(items, 0, HDAW::ClusterMethod::Dsp);
    ASSERT_EQ(out.clusters.size(), 3u) << "auto-k must find the three midi families";
    for (const auto& c : out.clusters) {
        const juce::String fam = familyOf(c.members[0].name);
        for (const auto& m : c.members) EXPECT_EQ(familyOf(m.name), fam);
    }
}

TEST(LibraryClustererTest, RelatedToItemOnMidiVectorsRanksFamilyFirst) {
    auto items = threeMidiFamilies();
    auto r = HDAW::relatedToItem(items, "/lib/drums0", HDAW::ClusterMethod::Dsp, 10);
    ASSERT_TRUE(r.hasSeedSignal);
    ASSERT_GE(r.results.size(), 3u);
    for (size_t i = 0; i < 3; ++i)
        EXPECT_TRUE(r.results[i].name.startsWith("drums")) << "hit " << i << ": " << r.results[i].name;
    for (const auto& h : r.results) EXPECT_NE(h.name, "drums0") << "seed must be excluded";
}

TEST(LibraryClustererTest, TextAxisIndependentOfVectorWidth) {
    std::vector<HDAW::ClusterItem> items;
    for (int i = 0; i < 3; ++i)
        items.push_back(item("x" + juce::String(i), "techno driving offbeat", "", makeMidi(4.0, 52.0, 0.4)));
    for (int i = 0; i < 3; ++i)
        items.push_back(item("y" + juce::String(i), "ambient airy sustained", "", makeMidi(0.8, 72.0, 0.05)));
    auto out = HDAW::cluster(items, 2, HDAW::ClusterMethod::Text);
    ASSERT_EQ(out.clusters.size(), 2u);
    for (const auto& c : out.clusters) {
        const bool isX = c.members[0].name.startsWith("x");
        for (const auto& m : c.members) EXPECT_EQ(m.name.startsWith("x"), isX);
    }
}

