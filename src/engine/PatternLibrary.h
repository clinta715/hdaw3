#pragma once
#include <juce_core/juce_core.h>
#include <mutex>
#include <vector>

namespace HDAW {

struct PatternPreset {
    int version = 1;
    juce::String name;
    juce::String description;
    juce::String category;       // trap, jazz, ambient, melodic, polyrhythm, user
    juce::StringArray tags;
    juce::String author;
    juce::String createdAt;      // ISO 8601
    juce::String style;          // Style enum name, e.g. "TrapHiHat"
    juce::String paramsJson;     // Base PhraseParams as JSON string
    juce::String styleParamsJson; // Style-specific params as JSON string
};

struct PatternIndexEntry {
    juce::String id;         // "factory/trap/dark-drill-bass"
    juce::String path;       // relative to patterns root
    juce::String name;
    juce::String style;
    juce::String category;
    juce::StringArray tags;
    juce::String source;     // "factory" or "user"
};

class PatternLibrary {
public:
    explicit PatternLibrary(const juce::File& patternsRoot);

    // CRUD
    bool savePattern(const PatternPreset& preset, juce::String& outError);
    bool loadPattern(const juce::String& id, PatternPreset& outPreset, juce::String& outError);
    bool deletePattern(const juce::String& id, juce::String& outError);

    // Browse
    std::vector<PatternIndexEntry> listPatterns(const juce::String& category = {},
                                                 const juce::String& style = {},
                                                 const juce::String& tag = {}) const;

    // Import/Export
    bool importPattern(const juce::String& jsonString, juce::String& outId, juce::String& outError);
    bool importPatternFile(const juce::File& file, juce::String& outId, juce::String& outError);
    bool exportPattern(const juce::String& id, juce::String& outJson, juce::String& outError);

    // Index management
    void rebuildIndex();
    bool isFactoryPattern(const juce::String& id) const;

    // Accessors
    juce::File getPatternsRoot() const { return root; }
    juce::File getUserPatternsDir() const { return root.getChildFile("user"); }
    juce::File getFactoryPatternsDir() const { return root.getChildFile("_factory"); }

private:
    juce::File root;
    mutable std::mutex mutex;
    std::vector<PatternIndexEntry> index;

    void ensureDirectoriesExist();
    juce::File indexFile() const { return root.getChildFile("index.json"); }
    juce::String sanitizeName(const juce::String& name) const;
    juce::String generateId(const juce::String& source, const juce::String& category,
                            const juce::String& filename) const;
    void writeIndex();
    void readIndex();
    void rebuildIndexUnlocked();
    bool validatePreset(const PatternPreset& preset, juce::String& outError) const;
};

} // namespace HDAW
