#include "engine/Generative.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

namespace HDAW
{

// ── SplitMix64 ──

uint64_t SplitMix64::nextU64()
{
    uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

double SplitMix64::nextFloat()
{
    return static_cast<double>(nextU64() >> 11) * (1.0 / 9007199254740992.0);
}

int SplitMix64::nextInt(int lo, int hi)
{
    if (lo >= hi) return lo;
    const uint64_t range = static_cast<uint64_t>(hi - lo) + 1ULL;
    return lo + static_cast<int>(nextU64() % range);
}

bool SplitMix64::nextBool(double p)
{
    if (p <= 0.0) return false;
    if (p >= 1.0) return true;
    return nextFloat() < p;
}

// ── Seed derivation ──

static uint64_t splitmixFinalize(uint64_t z)
{
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

uint64_t deriveSeed(uint64_t rootSeed, const std::string& ns)
{
    uint64_t h = 1469598103934665603ULL; // FNV-1a offset basis
    for (unsigned char c : ns)
    {
        h ^= c;
        h *= 1099511628211ULL;
    }
    h ^= rootSeed + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
    return splitmixFinalize(h);
}

SplitMix64 ReproState::rng(const std::vector<std::string>& parts) const
{
    std::string joined;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (i) joined += '|';
        joined += parts[i];
    }
    return SplitMix64(deriveSeed(root, joined));
}

SplitMix64 ReproState::rng(const std::string& a) const
{
    return rng(std::vector<std::string>{ a });
}

SplitMix64 ReproState::rng(const std::string& a, const std::string& b) const
{
    return rng(std::vector<std::string>{ a, b });
}

SplitMix64 ReproState::rng(const std::string& a, const std::string& b, const std::string& c) const
{
    return rng(std::vector<std::string>{ a, b, c });
}

// ── Euclidean rhythms ──

std::vector<int> euclideanSteps(int k, int n, int rot)
{
    if (n <= 0 || k <= 0) return {};
    if (k >= n)
    {
        std::vector<int> all(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) all[static_cast<size_t>(i)] = i;
        return all;
    }

    std::vector<int> hits;
    int bucket = 0;
    for (int step = 0; step < n; ++step)
    {
        bucket += k;
        if (bucket >= n)
        {
            hits.push_back(step);
            bucket -= n;
        }
    }

    if (rot != 0)
    {
        const int r = ((rot % n) + n) % n;
        for (int& h : hits) h = (h + r) % n;
        std::sort(hits.begin(), hits.end());
    }
    return hits;
}

std::vector<int> mapStepsToDivision(const std::vector<int>& hits, int sourceSteps, int division)
{
    if (sourceSteps <= 0 || division <= 0) return {};

    auto norm = [sourceSteps](int s) { return ((s % sourceSteps) + sourceSteps) % sourceSteps; };

    std::vector<int> out;
    out.reserve(hits.size());
    if (sourceSteps == division)
    {
        for (int s : hits) out.push_back(norm(s));
    }
    else if (division % sourceSteps == 0)
    {
        const int f = division / sourceSteps;
        for (int s : hits) out.push_back(norm(s) * f);
    }
    else
    {
        for (int s : hits)
            out.push_back(static_cast<int>(std::lround(norm(s) * static_cast<double>(division) / sourceSteps)));
    }

    for (int& x : out) x = std::clamp(x, 0, division - 1);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// ── Rhythm DSL parser ──

namespace
{

struct Segment
{
    int steps = 0;
    std::vector<int> hits;
};

bool isSpace(char c)  { return std::isspace(static_cast<unsigned char>(c)) != 0; }
bool isDigit(char c)  { return std::isdigit(static_cast<unsigned char>(c)) != 0; }

std::vector<Segment> parseSegments(const std::string& s, size_t& i);

int parseOptionalRepeat(const std::string& s, size_t& i)
{
    while (i < s.size() && isSpace(s[i])) ++i;
    if (i < s.size() && s[i] == 'x')
    {
        size_t j = i + 1;
        const size_t start = j;
        while (j < s.size() && isDigit(s[j])) ++j;
        if (j > start)
        {
            const int count = std::stoi(s.substr(start, j - start));
            i = j;
            return count < 1 ? 1 : count;
        }
        i = i + 1; // bare 'x' with no digits
        return 1;
    }
    return 1;
}

std::vector<Segment> parseGroup(const std::string& s, size_t& i)
{
    auto inner = parseSegments(s, i); // stops at ']' (unconsumed) or end
    if (i >= s.size() || s[i] != ']')
        throw std::invalid_argument("Unterminated group '[' in rhythm pattern");
    ++i; // consume ']'
    if (inner.empty())
        throw std::invalid_argument("Empty group '[]' in rhythm pattern");

    const int repeat = std::max(1, parseOptionalRepeat(s, i));
    std::vector<Segment> repeated;
    for (int r = 0; r < repeat; ++r)
        repeated.insert(repeated.end(), inner.begin(), inner.end());
    return repeated;
}

int parseInt(const std::string& s, size_t& i)
{
    while (i < s.size() && isSpace(s[i])) ++i;
    if (i >= s.size())
        throw std::invalid_argument("Expected integer in rhythm pattern");
    int sign = 1;
    if (s[i] == '+' || s[i] == '-')
    {
        if (s[i] == '-') sign = -1;
        ++i;
    }
    const size_t start = i;
    while (i < s.size() && isDigit(s[i])) ++i;
    if (i == start)
        throw std::invalid_argument("Expected digits in rhythm pattern");
    return sign * std::stoi(s.substr(start, i - start));
}

void consumeComma(const std::string& s, size_t& i)
{
    while (i < s.size() && isSpace(s[i])) ++i;
    if (i >= s.size() || s[i] != ',')
        throw std::invalid_argument("Expected ',' in E() pattern");
    ++i;
    while (i < s.size() && isSpace(s[i])) ++i;
}

Segment parseEuclid(const std::string& s, size_t& i)
{
    ++i; // consume 'E'
    while (i < s.size() && isSpace(s[i])) ++i;
    if (i >= s.size() || s[i] != '(')
        throw std::invalid_argument("Expected '(' after E in rhythm pattern");
    ++i; // consume '('

    const int k = parseInt(s, i);
    consumeComma(s, i);
    const int n = parseInt(s, i);

    int rot = 0;
    size_t peek = i;
    while (peek < s.size() && isSpace(s[peek])) ++peek;
    if (peek < s.size() && s[peek] == ',')
    {
        i = peek;
        consumeComma(s, i);
        rot = parseInt(s, i);
        if (rot < 0)
            throw std::invalid_argument("Negative rotation not allowed in E() pattern");
    }

    while (i < s.size() && isSpace(s[i])) ++i;
    if (i >= s.size() || s[i] != ')')
        throw std::invalid_argument("Expected ')' to close E( in rhythm pattern");
    ++i; // consume ')'

    if (n <= 0) throw std::invalid_argument("Euclidean n must be > 0");
    if (k <= 0) throw std::invalid_argument("Euclidean k must be > 0");

    return Segment{ n, euclideanSteps(k, n, rot) };
}

Segment segmentFromSequence(const std::string& seq)
{
    Segment seg;
    seg.steps = static_cast<int>(seq.size());
    for (int idx = 0; idx < seg.steps; ++idx)
        if (seq[static_cast<size_t>(idx)] == 'x')
            seg.hits.push_back(idx);
    return seg;
}

std::vector<Segment> parseSegments(const std::string& s, size_t& i)
{
    std::vector<Segment> out;
    while (i < s.size())
    {
        const char ch = s[i];
        if (isSpace(ch) || ch == '_') { ++i; continue; }
        if (ch == ']') break; // caller (group) consumes it
        if (ch == '[')
        {
            ++i; // consume '['
            auto g = parseGroup(s, i);
            out.insert(out.end(), g.begin(), g.end());
            continue;
        }
        if (ch == 'E')
        {
            out.push_back(parseEuclid(s, i));
            continue;
        }
        if (ch == 'x' || ch == '-')
        {
            std::string seq;
            while (i < s.size() && (s[i] == 'x' || s[i] == '-' || s[i] == '_'))
            {
                if (s[i] == 'x' || s[i] == '-') seq += s[i];
                ++i;
            }
            out.push_back(segmentFromSequence(seq));
            continue;
        }
        throw std::invalid_argument(std::string("Unexpected character '") + ch + "' in rhythm pattern");
    }
    return out;
}

void concatSegments(const std::vector<Segment>& segs, int& total, std::vector<int>& absHits)
{
    total = 0;
    absHits.clear();
    for (const auto& seg : segs)
    {
        if (seg.steps <= 0) continue;
        for (int h : seg.hits)
            if (h >= 0 && h < seg.steps)
                absHits.push_back(total + h);
        total += seg.steps;
    }
    std::sort(absHits.begin(), absHits.end());
    absHits.erase(std::unique(absHits.begin(), absHits.end()), absHits.end());
}

} // namespace

std::vector<int> expandToDivision(const std::string& pattern, int division)
{
    if (division <= 0) return {};

    size_t i = 0;
    auto segs = parseSegments(pattern, i);

    while (i < pattern.size() && isSpace(pattern[i])) ++i;
    if (i < pattern.size())
        throw std::invalid_argument("Unexpected trailing input in rhythm pattern");

    int total = 0;
    std::vector<int> absHits;
    concatSegments(segs, total, absHits);
    if (total <= 0) return {};

    return mapStepsToDivision(absHits, total, division);
}

// ── Micro-timing helpers ──

int humanizeInt(SplitMix64& rng, int value, int lo, int hi)
{
    if (lo > hi) std::swap(lo, hi);
    return value + rng.nextInt(lo, hi);
}

int applySwing(int tickInBar, int stepIndex, double swingPercent, int sixteenthTicks)
{
    if ((stepIndex & 1) == 0 || sixteenthTicks <= 0)
        return tickInBar;
    double amount = (swingPercent - 50.0) / 50.0;
    amount = std::clamp(amount, -1.0, 1.0);
    const int offset = static_cast<int>(std::lround(amount * sixteenthTicks * 0.5));
    return (std::max)(0, tickInBar + offset);
}

// ── Pitch-motion helpers ──

int weightedChoice(SplitMix64& rng, const std::vector<std::pair<int, double>>& candidates)
{
    if (candidates.empty()) return 0;
    double total = 0.0;
    for (const auto& [v, w] : candidates)
        total += (w > 0.0 ? w : 0.0);
    if (total <= 0.0) return candidates.front().first;

    const double r = rng.nextFloat() * total;
    double acc = 0.0;
    for (const auto& [v, w] : candidates)
    {
        acc += (w > 0.0 ? w : 0.0);
        if (r <= acc) return v;
    }
    return candidates.back().first;
}

int nextMarkovDegree(SplitMix64& rng, int current, int degreeCount)
{
    if (degreeCount <= 0) return 0;
    current = std::clamp(current, 0, degreeCount - 1);

    std::vector<std::pair<int, double>> cands;
    cands.push_back({ current, 0.40 });
    if (current - 1 >= 0)        cands.push_back({ current - 1, 0.23 });
    if (current + 1 < degreeCount) cands.push_back({ current + 1, 0.23 });
    if (degreeCount > 4)         cands.push_back({ 4, 0.08 });
    if (degreeCount > 6)         cands.push_back({ 6, 0.06 });

    return weightedChoice(rng, cands);
}

} // namespace HDAW
