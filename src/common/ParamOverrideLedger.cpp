#include "ParamOverrideLedger.h"

namespace HDAW {

juce::String formatParamOverride(int index, float normalizedValue)
{
    return juce::String(index) + "=" + juce::String(normalizedValue, 6);
}

std::vector<ParamOverridePair> parseParamOverrides(const juce::String& ledger)
{
    std::vector<ParamOverridePair> out;
    if (ledger.isEmpty())
        return out;

    // Ledger format: "idx=val;idx=val" (normalized 0..1).
    for (const auto& tok : juce::StringArray::fromTokens(ledger, ";", ""))
    {
        const int eq = tok.indexOf("=");
        if (eq <= 0)
            continue; // malformed token — the writers only emit idx=val pairs
        const int index = tok.upToFirstOccurrenceOf("=", false, false).getIntValue();
        if (index < 0)
            continue;
        out.emplace_back(index, static_cast<float>(
            tok.fromFirstOccurrenceOf("=", false, false).getDoubleValue()));
    }
    return out;
}

juce::String mergeParamOverride(const juce::String& ledger, int index,
                                float normalizedValue)
{
    if (index < 0)
        return ledger;

    juce::String out;
    bool written = false;
    for (const auto& pair : parseParamOverrides(ledger))
    {
        if (pair.first == index)
        {
            if (written)
                continue; // drop later duplicates — merge collapses them
            out << (out.isEmpty() ? juce::String() : juce::String(";"))
                << formatParamOverride(index, normalizedValue);
            written = true;
            continue;
        }
        out << (out.isEmpty() ? juce::String() : juce::String(";"))
            << formatParamOverride(pair.first, pair.second);
    }

    if (!written)
        out << (out.isEmpty() ? juce::String() : juce::String(";"))
            << formatParamOverride(index, normalizedValue);
    return out;
}

juce::String removeParamOverride(const juce::String& ledger, int index)
{
    juce::String out;
    for (const auto& pair : parseParamOverrides(ledger))
    {
        if (pair.first == index)
            continue;
        out << (out.isEmpty() ? juce::String() : juce::String(";"))
            << formatParamOverride(pair.first, pair.second);
    }
    return out;
}

} // namespace HDAW
