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

template <size_t N>
static std::span<const MidiFxParamDef> spanFromArray(const MidiFxParamDef (&arr)[N])
{
    return {arr, N};
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
        float val = static_cast<float>(
            slotTree.getProperty(juce::Identifier(defs[i].name), defs[i].defaultValue));
        float normalized = (defs[i].maxValue != defs[i].minValue)
            ? (val - defs[i].minValue) / (defs[i].maxValue - defs[i].minValue)
            : 0.0f;
        normalized = juce::jlimit(0.0f, 1.0f, normalized);
        paramValues_[i].store(normalized, std::memory_order_relaxed);
    }
}

} // namespace HDAW
