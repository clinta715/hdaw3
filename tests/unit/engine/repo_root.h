#pragma once
#include <juce_core/juce_core.h>

// Repo-root helpers for test data. The INCLUDING translation unit must live
// exactly four directories below the checkout root (tests/unit/engine/<file>)
// -- the same assumption tests/unit/engine/render_harness.h already relies on.
// __FILE__ in a header expands to the INCLUDING file's path, and every user of
// this header is tests/unit/engine/*.cpp. No absolute root is stored, so the
// checkout can move drives without these tests mis-resolving.
inline juce::File hdawRepoRoot()
{
    return juce::File(juce::CharPointer_UTF8(__FILE__))
        .getParentDirectory()   // tests/unit/engine
        .getParentDirectory()   // tests/unit
        .getParentDirectory()   // tests
        .getParentDirectory();  // <repo root>
}

// Join a forward-slash relative path onto the derived repo root.
inline juce::File hdawRepoFile(const char* relativePath)
{
    return hdawRepoRoot().getChildFile(relativePath);
}
