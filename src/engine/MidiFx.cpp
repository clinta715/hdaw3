#include "MidiFx.h"

namespace HDAW {

// ── Param defs tables ──────────────────────────────────────────────

static const MidiFxParamDef arpeggiatorParams[] = {
    {0, "rate",     0.25f, 0.01f, 2.0f},
    {1, "pattern",  0.0f,  0.0f,  5.0f},
    {2, "octaves",  1.0f,  1.0f,  4.0f},
    {3, "gate",     0.5f,  0.1f,  1.0f},
    {4, "velocity", 100.f, 1.0f, 127.f},
};

static const MidiFxParamDef velocityScalerParams[] = {
    {0, "factor", 1.0f, 0.0f, 2.0f},
};

static const MidiFxParamDef chorderParams[] = {
    {0, "chordType", 0.0f, 0.0f, 17.0f},
};

static const MidiFxParamDef scaleQuantizeParams[] = {
    {0, "root",      0.0f, 0.0f, 11.0f},
    {1, "scaleType", 0.0f, 0.0f, 12.0f},
};

static const MidiFxParamDef noteLengthScalerParams[] = {
    {0, "factor", 1.0f, 0.1f, 4.0f},
};

static const MidiFxParamDef transposeParams[] = {
    {0, "semitones", 0.0f, -24.0f, 24.0f},
};

static const MidiFxParamDef keyFilterParams[] = {
    {0, "root",      0.0f, 0.0f, 11.0f},
    {1, "scaleType", 0.0f, 0.0f, 12.0f},
};

static const MidiFxParamDef velocityCurveParams[] = {
    {0, "curveType",   0.0f, 0.0f, 3.0f},
    {1, "curveAmount", 0.5f, 0.0f, 1.0f},
};

static const MidiFxParamDef noteChanceParams[] = {
    {0, "noteChance", 1.0f, 0.0f, 1.0f},
};

static const MidiFxParamDef midiDelayParams[] = {
    {0, "delayBeats", 0.25f, 0.0f, 4.0f},
    {1, "feedback",   0.0f,  0.0f, 0.95f},
    {2, "mix",        0.5f,  0.0f, 1.0f},
};

static const MidiFxParamDef humanizeParams[] = {
    {0, "humanizeTiming",   0.0f, 0.0f, 0.1f},
    {1, "humanizeVelocity", 0.0f, 0.0f, 1.0f},
    {2, "humanizePitch",    0.0f, 0.0f, 1.0f},
};

static const MidiFxParamDef strumParams[] = {
    {0, "strumTime",      0.02f, 0.0f, 0.2f},
    {1, "strumDirection", 0.0f,  0.0f, 2.0f},
};

// acid_step: 12 globals (0..11) then step{N}Note/Accent/Slide/Rest at
// 12 + (N-1)*4 + {0,1,2,3}. 76 params; every index <= 99 (pid = 1000 + slot*100 + idx).
#define HDAW_ACID_STEP_DEFS(N, I) \
    {(I),     "step" #N "Note",   0.0f, -24.0f, 24.0f}, \
    {(I) + 1, "step" #N "Accent", 0.0f,   0.0f,  1.0f}, \
    {(I) + 2, "step" #N "Slide",  0.0f,   0.0f,  1.0f}, \
    {(I) + 3, "step" #N "Rest",   0.0f,   0.0f,  1.0f}

static const MidiFxParamDef acidStepParams[] = {
    {0,  "rate",           0.25f, 0.01f,   2.0f},
    {1,  "steps",          16.0f, 1.0f,   16.0f},
    {2,  "octave",         0.0f, -2.0f,    2.0f},
    {3,  "gate",           0.5f,  0.05f,   1.0f},
    {4,  "velocity",       96.0f, 1.0f,  127.0f},
    {5,  "accentVelocity", 118.0f, 1.0f, 127.0f},
    {6,  "accentAmount",   110.0f, 0.0f, 127.0f},
    {7,  "swing",          0.0f,  0.0f,    0.6f},
    {8,  "slideMode",      0.0f,  0.0f,    1.0f},
    {9,  "direction",      0.0f,  0.0f,    2.0f},
    {10, "latch",          0.0f,  0.0f,    1.0f},
    {11, "baseNote",       36.0f, 0.0f,  127.0f},
    HDAW_ACID_STEP_DEFS(1, 12),  HDAW_ACID_STEP_DEFS(2, 16),
    HDAW_ACID_STEP_DEFS(3, 20),  HDAW_ACID_STEP_DEFS(4, 24),
    HDAW_ACID_STEP_DEFS(5, 28),  HDAW_ACID_STEP_DEFS(6, 32),
    HDAW_ACID_STEP_DEFS(7, 36),  HDAW_ACID_STEP_DEFS(8, 40),
    HDAW_ACID_STEP_DEFS(9, 44),  HDAW_ACID_STEP_DEFS(10, 48),
    HDAW_ACID_STEP_DEFS(11, 52), HDAW_ACID_STEP_DEFS(12, 56),
    HDAW_ACID_STEP_DEFS(13, 60), HDAW_ACID_STEP_DEFS(14, 64),
    HDAW_ACID_STEP_DEFS(15, 68), HDAW_ACID_STEP_DEFS(16, 72),
};
#undef HDAW_ACID_STEP_DEFS
static_assert(sizeof(acidStepParams) / sizeof(acidStepParams[0]) == AcidStep::kNumParams,
              "acid_step param table must have exactly 76 entries");
static_assert(AcidStep::kNumParams <= 100, "MIDI-FX pid space is 1000 + slot*100 + index (index <= 99)");

// Legacy tree-key aliases: def name -> the property id pre-2026-10-04 projects
// (and addMidiFxSlot before this change) stored. Read-only compatibility; see the
// contract note in MidiFx.h.
struct MidiFxLegacyKey { const char* defName; const char* treeId; };

static std::span<const MidiFxLegacyKey> legacyKeysForType(const juce::String& type)
{
    static const MidiFxLegacyKey arp[] = { { "rate", "arpRate" }, { "pattern", "arpPattern" },
                                           { "octaves", "arpOctaves" }, { "gate", "arpGate" } };
    static const MidiFxLegacyKey vel[] = { { "factor", "velFactor" } };
    static const MidiFxLegacyKey scale[] = { { "root", "scaleRoot" } };
    static const MidiFxLegacyKey len[] = { { "factor", "lengthFactor" } };
    static const MidiFxLegacyKey kf[] = { { "root", "keyFilterRoot" }, { "scaleType", "keyFilterScale" } };
    static const MidiFxLegacyKey delay[] = { { "feedback", "delayFeedback" }, { "mix", "delayMix" } };
    if (type == "arpeggiator") return { arp, (size_t) juce::numElementsInArray(arp) };
    if (type == "velocity")    return { vel, (size_t) juce::numElementsInArray(vel) };
    if (type == "scale")       return { scale, (size_t) juce::numElementsInArray(scale) };
    if (type == "notelength")  return { len, (size_t) juce::numElementsInArray(len) };
    if (type == "keyfilter")   return { kf, (size_t) juce::numElementsInArray(kf) };
    if (type == "mididelay")   return { delay, (size_t) juce::numElementsInArray(delay) };
    return {};
}

template <size_t N>
static std::span<const MidiFxParamDef> spanFromArray(const MidiFxParamDef (&arr)[N])
{
    return {arr, N};
}

juce::Identifier midiFxTreeKeyForParam(const juce::String& type, const juce::String& paramName)
{
    const auto defs = getMidiFxParamDefs(type);
    bool known = false;
    for (const auto& d : defs)
        if (paramName == d.name) { known = true; break; }
    if (!known)
        return {};

    for (const auto& legacy : legacyKeysForType(type))
        if (paramName == legacy.defName)
            return juce::Identifier(legacy.treeId);

    return juce::Identifier(paramName);
}

bool isKnownMidiFxType(const juce::String& type)
{
    if (type == "multinote") return true; // known, but publishes no numeric params
    return !getMidiFxParamDefs(type).empty();
}

juce::String midiFxParamNameList(const juce::String& type)
{
    // Built directly into a String: no intermediate StringArray. (This path is a
    // REFUSAL message - it must never be the thing that faults, and an ASan run
    // pointed at the local StringArray's construction here.)
    juce::String out;
    for (const auto& d : getMidiFxParamDefs(type))
        out += (out.isEmpty() ? juce::String() : juce::String(", ")) + juce::String(d.name);
    return out.isEmpty() ? juce::String("(none)") : out;
}

juce::String midiFxTypeList()
{
    // Keep in step with getMidiFxParamDefs + the multinote special case; this is
    // what addMidiFxSlot names when it refuses an unknown type.
    return "arpeggiator, velocity, chord, scale, notelength, transpose, keyfilter, "
           "multinote, velocitycurve, notechance, mididelay, humanize, strum, acid_step";
}

std::span<const MidiFxParamDef> getMidiFxParamDefs(const juce::String& type)
{
    if (type == "arpeggiator")    return spanFromArray(arpeggiatorParams);
    if (type == "velocity")       return spanFromArray(velocityScalerParams);
    if (type == "chord")          return spanFromArray(chorderParams);
    if (type == "scale")          return spanFromArray(scaleQuantizeParams);
    if (type == "notelength")     return spanFromArray(noteLengthScalerParams);
    if (type == "transpose")      return spanFromArray(transposeParams);
    if (type == "keyfilter")      return spanFromArray(keyFilterParams);
    if (type == "multinote")      return {};
    if (type == "velocitycurve")  return spanFromArray(velocityCurveParams);
    if (type == "notechance")     return spanFromArray(noteChanceParams);
    if (type == "mididelay")      return spanFromArray(midiDelayParams);
    if (type == "humanize")       return spanFromArray(humanizeParams);
    if (type == "strum")          return spanFromArray(strumParams);
    if (type == "acid_step")      return spanFromArray(acidStepParams);
    return {};
}

int getMidiFxParamCount(const juce::String& type)
{
    return static_cast<int>(getMidiFxParamDefs(type).size());
}

// ── MidiFxSlot implementation ──────────────────────────────────────

MidiFxSlot::MidiFxSlot(std::unique_ptr<MidiEffect> effect, juce::String type)
    : effect_(std::move(effect)), slotType_(std::move(type))
{
    initParamCache();
}

void MidiFxSlot::initParamCache()
{
    auto defs = getMidiFxParamDefs(slotType_);
    numParams_ = static_cast<int>(defs.size());
    if (numParams_ > 0)
    {
        paramValues_ = std::make_unique<std::atomic<float>[]>(numParams_);
        paramDirty_ = std::make_unique<std::atomic<bool>[]>(numParams_);
        for (int i = 0; i < numParams_; ++i)
        {
            paramValues_[i].store(defs[i].defaultValue, std::memory_order_relaxed);
            paramDirty_[i].store(false, std::memory_order_relaxed);
            cachedParamInfo_.push_back({defs[i].name, defs[i].index,
                                        defs[i].defaultValue,
                                        defs[i].minValue, defs[i].maxValue});
        }
    }
}

void MidiFxSlot::setAutomationParam(int paramIndex, float normalizedValue)
{
    if (paramIndex >= 0 && paramIndex < numParams_)
    {
        paramValues_[paramIndex].store(normalizedValue, std::memory_order_relaxed);
        paramDirty_[paramIndex].store(true, std::memory_order_relaxed);
    }
}

float MidiFxSlot::getAutomationParam(int paramIndex) const
{
    if (paramIndex >= 0 && paramIndex < numParams_)
        return paramValues_[paramIndex].load(std::memory_order_relaxed);
    return 0.0f;
}

void MidiFxSlot::applyAutomation()
{
    if (!effect_ || numParams_ == 0) return;
    for (int i = 0; i < numParams_; ++i)
    {
        if (!paramDirty_[i].load(std::memory_order_relaxed))
            continue;
        paramDirty_[i].store(false, std::memory_order_relaxed);
        float normalized = paramValues_[i].load(std::memory_order_relaxed);
        const auto& def = cachedParamInfo_[i];
        float denormalized = def.minValue + normalized * (def.maxValue - def.minValue);
        applyToEffect(i, denormalized);
    }
}

void MidiFxSlot::applyToEffect(int paramIndex, float value)
{
    if (auto* arp = dynamic_cast<Arpeggiator*>(effect_.get()))
    {
        switch (paramIndex) {
            case 0: arp->rate = static_cast<double>(value); break;
            case 1: arp->pattern = static_cast<int>(std::round(value)); break;
            case 2: arp->octaves = static_cast<int>(std::round(value)); break;
            case 3: arp->gate = static_cast<double>(value); break;
            case 4: arp->velocity = static_cast<int>(std::round(value)); break;
        }
        return;
    }
    if (auto* v = dynamic_cast<VelocityScaler*>(effect_.get()))
    {
        if (paramIndex == 0) v->factor = static_cast<double>(value);
        return;
    }
    if (auto* c = dynamic_cast<Chorder*>(effect_.get()))
    {
        if (paramIndex == 0) c->chordType = static_cast<int>(std::round(value));
        return;
    }
    if (auto* sq = dynamic_cast<ScaleQuantize*>(effect_.get()))
    {
        switch (paramIndex) {
            case 0: sq->root = static_cast<int>(std::round(value)); break;
            case 1: sq->scaleType = static_cast<int>(std::round(value)); break;
        }
        return;
    }
    if (auto* nl = dynamic_cast<NoteLengthScaler*>(effect_.get()))
    {
        if (paramIndex == 0) nl->factor = static_cast<double>(value);
        return;
    }
    if (auto* t = dynamic_cast<Transpose*>(effect_.get()))
    {
        if (paramIndex == 0) t->semitones = static_cast<int>(std::round(value));
        return;
    }
    if (auto* kf = dynamic_cast<KeyFilter*>(effect_.get()))
    {
        switch (paramIndex) {
            case 0: kf->root = static_cast<int>(std::round(value)); break;
            case 1: kf->scaleType = static_cast<int>(std::round(value)); break;
        }
        return;
    }
    if (auto* vc = dynamic_cast<VelocityCurve*>(effect_.get()))
    {
        switch (paramIndex) {
            case 0: vc->curveType = static_cast<int>(std::round(value)); break;
            case 1: vc->curveAmount = static_cast<double>(value); break;
        }
        return;
    }
    if (auto* nc = dynamic_cast<NoteChance*>(effect_.get()))
    {
        if (paramIndex == 0) nc->noteChance = static_cast<double>(value);
        return;
    }
    if (auto* md = dynamic_cast<MidiDelay*>(effect_.get()))
    {
        switch (paramIndex) {
            case 0: md->delayBeats = static_cast<double>(value); break;
            case 1: md->feedback = static_cast<double>(value); break;
            case 2: md->mix = static_cast<double>(value); break;
        }
        return;
    }
    if (auto* h = dynamic_cast<Humanize*>(effect_.get()))
    {
        switch (paramIndex) {
            case 0: h->humanizeTiming = static_cast<double>(value); break;
            case 1: h->humanizeVelocity = static_cast<double>(value); break;
            case 2: h->humanizePitch = static_cast<double>(value); break;
        }
        return;
    }
    if (auto* acid = dynamic_cast<AcidStep*>(effect_.get()))
    {
        acid->setParam(paramIndex, value);
        return;
    }
    if (auto* s = dynamic_cast<Strum*>(effect_.get()))
    {
        switch (paramIndex) {
            case 0: s->strumTime = static_cast<double>(value); break;
            case 1: s->strumDirection = static_cast<int>(std::round(value)); break;
        }
        return;
    }
}

void MidiFxSlot::loadParamsFromTree(const juce::ValueTree& slotTree)
{
    auto defs = getMidiFxParamDefs(slotType_);
    for (int i = 0; i < numParams_ && i < static_cast<int>(defs.size()); ++i)
    {
        // Read through the shared name->key map: the six legacy types store
        // prefixed ids (arpRate, velFactor, ...), so reading the bare def name
        // returned the default and the loaded value never reached the cache.
        // The bare def name is still accepted as a FALLBACK: the pre-2026-10-04
        // set_midi_fx_param wrote that key (it just never reached the effect), so
        // old projects may carry it and it costs one extra lookup to honour.
        const juce::Identifier mappedKey = midiFxTreeKeyForParam(slotType_, defs[i].name);
        float val = static_cast<float>(
            slotTree.getProperty(mappedKey,
                slotTree.getProperty(juce::Identifier(defs[i].name), defs[i].defaultValue)));
        float normalized = (defs[i].maxValue != defs[i].minValue)
            ? (val - defs[i].minValue) / (defs[i].maxValue - defs[i].minValue)
            : 0.0f;
        normalized = juce::jlimit(0.0f, 1.0f, normalized);
        paramValues_[i].store(normalized, std::memory_order_relaxed);
    }
}

} // namespace HDAW
