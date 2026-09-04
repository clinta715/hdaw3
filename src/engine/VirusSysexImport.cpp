#include "VirusSysexImport.h"
#include <algorithm>
#include <cmath>

namespace HDAW {

namespace {

// ── Format constants (validated against the real preset library in slice 1) ──
constexpr size_t kHeaderLen   = 9;     // F0 00 20 33 01 <dev> 10 <bank> <prog>
constexpr size_t kPageLen     = 256;   // page A + page B inside a single dump
constexpr size_t kNameOff     = 240;   // page B params 112..121 = name chars
constexpr size_t kNameLen     = 10;
constexpr size_t kBcSingleLen = 267;   // F0 + 8-byte header + 256 data + cs + F7
constexpr size_t kTiBlockLen  = 524;   // F0 + 8-byte header + 512 data + cs + F7
constexpr size_t kTiPayloadLen = 512;

// Manufacturer header F0 00 20 33 01 (Access). Byte 6 == 0x10 selects a
// single-program dump vs. a bank/bulk transfer.
constexpr uint8_t kHeader[5] = { 0xF0, 0x00, 0x20, 0x33, 0x01 };
constexpr uint8_t kSingleCmd = 0x10;

bool hasVirusHeader(const uint8_t* data, size_t size)
{
    return size >= sizeof(kHeader)
        && data[0] == kHeader[0] && data[1] == kHeader[1] && data[2] == kHeader[2]
        && data[3] == kHeader[3] && data[4] == kHeader[4];
}

// Checksum over a page/payload plus the header's dev + 0x10 + bank + prog.
uint8_t checksumFor(const uint8_t* block, const uint8_t* payload, size_t payloadLen)
{
    int sum = static_cast<int>(block[5]) + 0x10 + static_cast<int>(block[7]) + static_cast<int>(block[8]);
    for (size_t i = 0; i < payloadLen; ++i)
        sum += payload[i];
    return static_cast<uint8_t>(sum & 0x7F);
}

// 10-char name at page[240:250]. Keep 0x20..0x7E chars (mirrors the Python
// `_name_from_data`, which filters 0x20 <= c < 0x7F), then right-trim spaces.
// A leading 0x7E '~' is kept.
std::string extractName(const uint8_t* page, size_t len)
{
    std::string name;
    if (len < kNameOff + kNameLen)
        return name;
    for (size_t i = kNameOff; i < kNameOff + kNameLen; ++i)
    {
        const uint8_t c = page[i];
        if (c >= 0x20 && c < 0x7F)
            name.push_back(static_cast<char>(c));
    }
    while (!name.empty() && name.back() == ' ')
        name.pop_back();
    return name;
}

// ── Virus byte -> sub_synth real-unit converters ──────────────────────────
// Virus bytes are 0..127. Conversions mirror timbre-lib/virus_patch.py's
// _CONVERTERS but emit REAL-UNIT values matching the sub_synth param defs in
// TrackFXSlot.h (getParamDefsForType("sub_synth"), params 0..23).

float virusNorm(uint8_t x)
{
    return static_cast<float>(x) / 127.0f;
}

// Osc wave select: Virus 0=Saw, 1=Square, 2=Triangle, 3=Sine, then higher
// indices are wavetable/PCM variants. sub_synth Waveform enum
// (SubtractiveSynthEngine.h): 0=Sine, 1=Saw, 2=Square, 3=Triangle. Map the
// four basic waves onto the sub_synth enum (saw→1, square→2, tri→3,
// sine→0); anything above wraps mod-4 to the same four.
float virusWave(uint8_t x)
{
    static constexpr int kMap[4] = { 1, 2, 3, 0 };
    return static_cast<float>(kMap[x % 4]);
}

// Osc2 Detune 0..127 -> bipolar cents; 64 = no detune. The Virus range is
// +/-100 cents (clamped to the def's +/-1200 by the write-side clamp).
float virusDetuneCents(uint8_t x)
{
    return static_cast<float>((static_cast<int>(x) - 64) * 100.0 / 64.0);
}

// Cutoff 0..127 -> 20 Hz..20 kHz, exponential (the Virus cutoff is roughly
// exponential over that range).
float virusCutoffHz(uint8_t x)
{
    const double t = static_cast<double>(x) / 127.0;
    return static_cast<float>(20.0 * std::pow(20000.0 / 20.0, t));
}

// Envelope times 0..127 -> 0.001 s..5 s, exponential (matches the def range
// for every sub_synth env-time param).
float virusEnvTimeSeconds(uint8_t x)
{
    const double t = static_cast<double>(x) / 127.0;
    return static_cast<float>(0.001 * std::pow(5.0 / 0.001, t));
}

// Saturation Curve 0=Off 1:Light 2:Soft 3:Middle 4:Hard 5:Digital 6:Shaper.
float virusDrive(uint8_t x)
{
    return std::min(static_cast<float>(x) / 6.0f, 1.0f);
}

// Subosc Shape 0=Square 1=Triangle. The Virus sub sits one octave below the
// oscillators — report -1 regardless of shape (documented behavior).
float virusSubOctave(uint8_t x)
{
    (void)x;
    return -1.0f;
}

// Key Mode 0=Poly 1..4=Mono1-4; mono modes imply legato behavior.
float virusLegato(uint8_t x)
{
    return (x != 0) ? 1.0f : 0.0f;
}

// Filter1 Mode 0:LP 1:HP 2:BP 3:BS -> sub_synth filter type (0..3).
float virusFilterType(uint8_t x)
{
    return static_cast<float>(std::min(static_cast<int>(x), 3));
}

// Filter1 Env Amount is bipolar (0..127, 64 = center; negative closes the
// filter). The sub_synth Filter Env Amount is 0..48 semitones (no negative),
// so mirror the Python bipolar_norm, scale onto [0,48] and clamp negatives.
float virusFilterEnvSemis(uint8_t x)
{
    const double bipolar = (static_cast<int>(x) - 64) / 64.0;
    return static_cast<float>(std::min(std::max(bipolar * 48.0, 0.0), 48.0));
}

// Patch Volume 0..127 -> 0..1.5 (def max).
float virusOutput(uint8_t x)
{
    return static_cast<float>(x) / 127.0f * 1.5f;
}

// Portamento Time 0..127 -> 0..5 s (def max).
float virusPortamento(uint8_t x)
{
    return static_cast<float>(x) / 127.0f * 5.0f;
}

// ── sub_synth param -> Virus source offset + converter ─────────────────────
// Offsets are page-A positions inside the 256-byte page A+B payload (same as
// virus_patch.py PARAM_OFFSETS). Params 0..22 map; 23 (Pitch Bend Range) is
// reserved and left unmapped.
struct SubSynthSource {
    int paramIndex;
    size_t offset;
    float (*convert)(uint8_t);
};

const SubSynthSource kSubSynthSources[] = {
    { 0,  19, virusWave },           // osc1_wave    (A19)
    { 1,  36, virusNorm },           // osc1_level   (A36 osc_mainvolume)
    { 2,  24, virusWave },           // osc2_wave    (A24)
    { 3,  33, virusNorm },           // osc2_level   (A33 osc_balance)
    { 4,  26, virusDetuneCents },    // osc2_detune  (A26)
    { 5,  34, virusNorm },           // sub_level    (A34 subosc_volume)
    { 6,  35, virusSubOctave },      // sub_octave   (A35 subosc_shape)
    { 7,  40, virusCutoffHz },       // cutoff       (A40)
    { 8,  42, virusNorm },           // resonance    (A42 filter1_resonance)
    { 9,  49, virusDrive },          // drive        (A49 saturation_curve)
    { 10, 59, virusEnvTimeSeconds }, // amp_attack   (A59)
    { 11, 60, virusEnvTimeSeconds }, // amp_decay    (A60)
    { 12, 61, virusNorm },           // amp_sustain  (A61)
    { 13, 63, virusEnvTimeSeconds }, // amp_release  (A63)
    { 14, 91, virusOutput },         // output       (A91 patch_volume)
    { 15, 94, virusLegato },         // legato       (A94 key_mode)
    { 16,  5, virusPortamento },     // portamento   (A5)
    { 17, 51, virusFilterType },     // filter_type  (A51 filter1_mode)
    { 18, 44, virusFilterEnvSemis }, // filter_env_amount (A44 filter1_envamt)
    { 19, 54, virusEnvTimeSeconds }, // filter_env_attack (A54)
    { 20, 55, virusEnvTimeSeconds }, // filter_env_decay  (A55)
    { 21, 56, virusNorm },           // filter_env_sustain (A56)
    { 22, 58, virusEnvTimeSeconds }, // filter_env_release (A58)
};

// Virus features with no sub_synth equivalent. Reported verbatim per patch —
// never silently dropped (mirrors virus_patch.py UNMAPPED_FEATURES).
const char* const kUnmappedFeatures[] = {
    "osc2_fm_amount",
    "ring_mod",
    "lfo1",
    "lfo2",
    "keytrack",
    "filter_slope_24db",
    "osc_sync",
    "fx_chorus",
    "fx_delay",
    "fx_reverb",
    "mod_matrix",
    "noise_level",
};

} // namespace

VirusPatch mapToSubSynth(const uint8_t* page, size_t len, const std::string& name)
{
    VirusPatch patch;
    patch.isValid = true;
    patch.name = name;

    for (const auto& s : kSubSynthSources)
    {
        if (s.offset >= len)
            continue; // Gate 9: never read out of range
        patch.mapped[s.paramIndex] = s.convert(page[s.offset]);
    }

    for (const char* feature : kUnmappedFeatures)
        patch.unmapped.emplace_back(feature);

    return patch;
}

std::optional<VirusPatch> parseBcSingle(const uint8_t* data, size_t size)
{
    if (size != kBcSingleLen)
        return std::nullopt;
    if (!hasVirusHeader(data, size))
        return std::nullopt;
    if (data[6] != kSingleCmd)
        return std::nullopt;
    if (data[size - 1] != 0xF7)
        return std::nullopt;

    const uint8_t* page = data + kHeaderLen;
    const uint8_t expected = checksumFor(data, page, kPageLen);
    if (data[kBcSingleLen - 2] != expected)
        return std::nullopt;

    VirusPatch patch = mapToSubSynth(page, kPageLen, extractName(page, kPageLen));
    patch.isValid = true;
    patch.bank = data[7];
    patch.program = data[8];
    return patch;
}

std::vector<VirusPatch> parseTiBank(const uint8_t* data, size_t size)
{
    std::vector<VirusPatch> patches;
    if (size == 0 || size % kTiBlockLen != 0)
        return patches;
    if (!hasVirusHeader(data, size))
        return patches;

    for (size_t off = 0; off < size; off += kTiBlockLen)
    {
        const uint8_t* block = data + off;
        // A corrupt block invalidates the whole bank: the loader must never
        // partially apply a bank with a bad checksum or structural error.
        if (!hasVirusHeader(block, kTiBlockLen))
            return {};
        if (block[6] != kSingleCmd)
            return {};
        if (block[kTiBlockLen - 1] != 0xF7)
            return {};
        const uint8_t* payload = block + kHeaderLen;
        const uint8_t expected = checksumFor(block, payload, kTiPayloadLen);
        if (block[kTiBlockLen - 2] != expected)
            return {};

        VirusPatch patch = mapToSubSynth(payload, kTiPayloadLen,
                                         extractName(payload, kTiPayloadLen));
        patch.isValid = true;
        patch.bank = block[7];
        patch.program = block[8];
        patches.push_back(std::move(patch));
    }
    return patches;
}

} // namespace HDAW