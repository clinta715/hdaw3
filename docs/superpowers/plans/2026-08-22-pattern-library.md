# Pattern Library & Extended Generation Styles — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add 15 new phrase generation styles and a JSON-based pattern library (user presets, factory presets, external import/export) to HDAW's phrase/pattern generator.

**Architecture:** Extend the existing `PhraseGenerator` enum with 15 new styles (indices 10–24). Add a `PatternLibrary` class that manages JSON pattern files on disk (`%APPDATA%/HDAW/patterns/`). Expose library operations via RPC and MCP. Add a preset browser sidebar to the frontend `PhraseGeneratorDialog`.

**Tech Stack:** C++17, JUCE 8, Qt 6 (RPC), React 19 + TypeScript (frontend), JSON (nlohmann or QJson), gtest, Vitest, Playwright.

---

## File Map

| File | Action | Responsibility |
|------|--------|---------------|
| `src/engine/PatternLibrary.h` | **Create** | PatternLibrary class declaration |
| `src/engine/PatternLibrary.cpp` | **Create** | Save/load/delete/list/import/export + index management |
| `src/engine/PhraseGenerator.h` | **Modify** | Add 15 Style enum values, StyleParams structs, `getStyleParamsSchema()` |
| `src/engine/PhraseGenerator.cpp` | **Modify** | Implement 15 generation functions, `styleName()` extension, `getStyleParamsSchema()` |
| `src/frontend/router/Router_Composition.cpp` | **Modify** | Add 7 new RPC methods, extend `getStyleNames` to index 24 |
| `src/mcp/McpTools_Project.cpp` | **Modify** | Add 6 new MCP tools |
| `CMakeLists.txt` | **Modify** | Add `PatternLibrary.cpp` to sources |
| `tests/CMakeLists.txt` | **Modify** | Add new test files |
| `tests/unit/engine/pattern_library_test.cpp` | **Create** | PatternLibrary unit tests |
| `tests/unit/engine/phrase_generator_new_styles_test.cpp` | **Create** | Tests for 15 new styles |
| `frontend/src/components/PresetBrowser.tsx` | **Create** | Preset browser sidebar component |
| `frontend/src/components/PresetBrowser.css` | **Create** | Preset browser styles |
| `frontend/src/components/PhraseGeneratorDialog.tsx` | **Modify** | Integrate preset browser, dynamic style controls |
| `frontend/src/components/PhraseGeneratorDialog.css` | **Modify** | Layout adjustments for sidebar |
| `patterns/_factory/` | **Create** | Factory preset JSON files (15–20 presets) |

---

## Task 1: PatternLibrary Class — Header

**Files:**
- Create: `src/engine/PatternLibrary.h`

- [ ] **Step 1: Create the PatternLibrary header**

```cpp
#pragma once
#include <juce_core/juce_core.h>
#include <string>
#include <vector>
#include <cstdint>
#include <optional>

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
    std::vector<PatternIndexEntry> index;

    void ensureDirectoriesExist();
    juce::File indexFile() const { return root.getChildFile("index.json"); }
    juce::String sanitizeName(const juce::String& name) const;
    juce::String generateId(const juce::String& source, const juce::String& category,
                            const juce::String& filename) const;
    void writeIndex();
    void readIndex();
    bool validatePreset(const PatternPreset& preset, juce::String& outError) const;
};

} // namespace HDAW
```

- [ ] **Step 2: Commit**

```bash
git add src/engine/PatternLibrary.h
git commit -m "feat: add PatternLibrary header with JSON pattern preset support"
```

---

## Task 2: PatternLibrary Class — Implementation

**Files:**
- Create: `src/engine/PatternLibrary.cpp`
- Modify: `CMakeLists.txt` (add `src/engine/PatternLibrary.cpp` to sources, near line 120)

- [ ] **Step 1: Add PatternLibrary.cpp to CMakeLists.txt**

In `CMakeLists.txt`, after the line `src/engine/PhraseGenerator.cpp` (line 120), add:

```cpp
    src/engine/PatternLibrary.cpp
```

- [ ] **Step 2: Implement PatternLibrary.cpp**

```cpp
#include "PatternLibrary.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <algorithm>

namespace HDAW {

PatternLibrary::PatternLibrary(const juce::File& patternsRoot)
    : root(patternsRoot)
{
    ensureDirectoriesExist();
    readIndex();
}

void PatternLibrary::ensureDirectoriesExist()
{
    root.createDirectory();
    root.getChildFile("_factory").createDirectory();
    root.getChildFile("user").createDirectory();
}

juce::String PatternLibrary::sanitizeName(const juce::String& name) const
{
    juce::String result = name.trim();
    // Remove invalid filename characters
    const juce::String invalid = "<>:\"/\\|?*";
    for (auto c : invalid)
        result = result.replace juce::String::charToString(c), "");
    return result.substring(0, 64);
}

juce::String PatternLibrary::generateId(const juce::String& source,
                                         const juce::String& category,
                                         const juce::String& filename) const
{
    return source + "/" + category + "/" + filename;
}

bool PatternLibrary::validatePreset(const PatternPreset& preset, juce::String& outError) const
{
    if (preset.name.isEmpty() || preset.name.length() > 64)
    {
        outError = "name must be 1-64 characters";
        return false;
    }
    if (preset.style.isEmpty())
    {
        outError = "style is required";
        return false;
    }
    if (preset.version > 1)
    {
        outError = "unsupported version: " + juce::String(preset.version);
        return false;
    }
    if (preset.tags.size() > 10)
    {
        outError = "maximum 10 tags allowed";
        return false;
    }
    return true;
}

bool PatternLibrary::savePattern(const PatternPreset& preset, juce::String& outError)
{
    if (!validatePreset(preset, outError))
        return false;

    juce::String sanitized = sanitizeName(preset.name);
    if (sanitized.isEmpty())
    {
        outError = "invalid name after sanitization";
        return false;
    }

    juce::String category = preset.category.isEmpty() ? "user" : preset.category;
    juce::File file = root.getChildFile("user").getChildFile(category);
    file.createDirectory();
    file = file.getChildFile(sanitized + ".json");

    // Build JSON
    QJsonObject obj;
    obj["version"] = preset.version;
    obj["name"] = preset.name.toStdString();
    obj["description"] = preset.description.toStdString();
    obj["category"] = preset.category.toStdString();
    obj["author"] = preset.author.toStdString();
    obj["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
    obj["style"] = preset.style.toStdString();

    QJsonArray tagsArr;
    for (const auto& tag : preset.tags)
        tagsArr.append(tag.toStdString());
    obj["tags"] = tagsArr;

    // Parse params JSON strings into objects
    QJsonDocument paramsDoc = QJsonDocument::fromJson(preset.paramsJson.toUtf8());
    if (paramsDoc.isObject())
        obj["params"] = paramsDoc.object();

    QJsonDocument styleDoc = QJsonDocument::fromJson(preset.styleParamsJson.toUtf8());
    if (styleDoc.isObject())
        obj["styleParams"] = styleDoc.object();

    QJsonDocument doc(obj);
    file.replaceWithText(doc.toJson().constData());

    rebuildIndex();
    return true;
}

bool PatternLibrary::loadPattern(const juce::String& id, PatternPreset& outPreset, juce::String& outError)
{
    // Find in index
    for (const auto& entry : index)
    {
        if (entry.id == id)
        {
            juce::File file = root.getChildFile(entry.path);
            if (!file.existsAsFile())
            {
                outError = "pattern file not found: " + entry.path;
                return false;
            }

            QJsonDocument doc = QJsonDocument::fromJson(file.loadFileAsString().toUtf8());
            if (!doc.isObject())
            {
                outError = "invalid JSON in pattern file";
                return false;
            }

            QJsonObject obj = doc.object();
            outPreset.version = obj["version"].toInt(1);
            outPreset.name = QString::fromStdString(obj["name"].toString().toStdString()).trimmed();
            outPreset.description = QString::fromStdString(obj["description"].toString().toStdString());
            outPreset.category = QString::fromStdString(obj["category"].toString().toStdString());
            outPreset.style = QString::fromStdString(obj["style"].toString().toStdString());
            outPreset.author = QString::fromStdString(obj["author"].toString().toStdString());
            outPreset.createdAt = QString::fromStdString(obj["createdAt"].toString().toStdString());

            outPreset.tags.clear();
            for (const auto& tag : obj["tags"].toArray())
                outPreset.tags.append(QString::fromStdString(tag.toString().toStdString()));

            outPreset.paramsJson = QString::fromUtf8(QJsonDocument(obj["params"].toObject()).toJson());
            outPreset.styleParamsJson = QString::fromUtf8(QJsonDocument(obj["styleParams"].toObject()).toJson());

            return true;
        }
    }
    outError = "pattern not found: " + id;
    return false;
}

bool PatternLibrary::deletePattern(const juce::String& id, juce::String& outError)
{
    if (isFactoryPattern(id))
    {
        outError = "cannot delete factory patterns";
        return false;
    }

    for (auto it = index.begin(); it != index.end(); ++it)
    {
        if (it->id == id)
        {
            juce::File file = root.getChildFile(it->path);
            if (file.existsAsFile())
                file.deleteFile();
            index.erase(it);
            writeIndex();
            return true;
        }
    }
    outError = "pattern not found: " + id;
    return false;
}

std::vector<PatternIndexEntry> PatternLibrary::listPatterns(
    const juce::String& category, const juce::String& style, const juce::String& tag) const
{
    std::vector<PatternIndexEntry> result;
    for (const auto& entry : index)
    {
        if (!category.isEmpty() && entry.category != category)
            continue;
        if (!style.isEmpty() && entry.style != style)
            continue;
        if (!tag.isEmpty() && !entry.tags.contains(tag))
            continue;
        result.push_back(entry);
    }
    return result;
}

bool PatternLibrary::importPattern(const juce::String& jsonString, juce::String& outId, juce::String& outError)
{
    QJsonDocument doc = QJsonDocument::fromJson(jsonString.toUtf8());
    if (!doc.isObject())
    {
        outError = "invalid JSON";
        return false;
    }

    QJsonObject obj = doc.object();
    PatternPreset preset;
    preset.version = obj["version"].toInt(1);
    preset.name = QString::fromStdString(obj["name"].toString().toStdString());
    preset.description = QString::fromStdString(obj["description"].toString().toStdString());
    preset.category = QString::fromStdString(obj["category"].toString().toStdString());
    preset.style = QString::fromStdString(obj["style"].toString().toStdString());
    preset.author = QString::fromStdString(obj["author"].toString().toStdString());

    for (const auto& tag : obj["tags"].toArray())
        preset.tags.append(QString::fromStdString(tag.toString().toStdString()));

    preset.paramsJson = QString::fromUtf8(QJsonDocument(obj["params"].toObject()).toJson());
    preset.styleParamsJson = QString::fromUtf8(QJsonDocument(obj["styleParams"].toObject()).toJson());

    if (!validatePreset(preset, outError))
        return false;

    if (!savePattern(preset, outError))
        return false;

    // Find the ID from the freshly rebuilt index
    juce::String sanitized = sanitizeName(preset.name);
    juce::String category = preset.category.isEmpty() ? "user" : preset.category;
    outId = generateId("user", category, sanitized);
    return true;
}

bool PatternLibrary::importPatternFile(const juce::File& file, juce::String& outId, juce::String& outError)
{
    if (!file.existsAsFile())
    {
        outError = "file not found: " + file.getFullPathName();
        return false;
    }
    return importPattern(file.loadFileAsString(), outId, outError);
}

bool PatternLibrary::exportPattern(const juce::String& id, juce::String& outJson, juce::String& outError)
{
    PatternPreset preset;
    if (!loadPattern(id, preset, outError))
        return false;

    QJsonObject obj;
    obj["version"] = preset.version;
    obj["name"] = preset.name.toStdString();
    obj["description"] = preset.description.toStdString();
    obj["category"] = preset.category.toStdString();
    obj["style"] = preset.style.toStdString();
    obj["author"] = preset.author.toStdString();
    obj["createdAt"] = preset.createdAt.toStdString();

    QJsonArray tagsArr;
    for (const auto& tag : preset.tags)
        tagsArr.append(tag.toStdString());
    obj["tags"] = tagsArr;

    QJsonDocument paramsDoc = QJsonDocument::fromJson(preset.paramsJson.toUtf8());
    if (paramsDoc.isObject())
        obj["params"] = paramsDoc.object();

    QJsonDocument styleDoc = QJsonDocument::fromJson(preset.styleParamsJson.toUtf8());
    if (styleDoc.isObject())
        obj["styleParams"] = styleDoc.object();

    outJson = QString::fromUtf8(QJsonDocument(obj).toJson());
    return true;
}

void PatternLibrary::rebuildIndex()
{
    index.clear();

    // Scan factory patterns
    juce::File factoryDir = root.getChildFile("_factory");
    if (factoryDir.isDirectory())
    {
        for (const auto& categoryDir : juce::RangedDirectoryIterator(factoryDir, false, "*", juce::File::findDirectories))
        {
            juce::String category = categoryDir.getFilename();
            for (const auto& file : juce::RangedDirectoryIterator(categoryDir.getFile(), false, "*.json"))
            {
                PatternIndexEntry entry;
                entry.id = generateId("factory", category, file.getFile().getFileNameWithoutExtension());
                entry.path = "_factory/" + category + "/" + file.getFilename();
                entry.source = "factory";

                // Read metadata from file
                QJsonDocument doc = QJsonDocument::fromJson(file.getFile().loadFileAsString().toUtf8());
                if (doc.isObject())
                {
                    QJsonObject obj = doc.object();
                    entry.name = QString::fromStdString(obj["name"].toString().toStdString());
                    entry.style = QString::fromStdString(obj["style"].toString().toStdString());
                    for (const auto& tag : obj["tags"].toArray())
                        entry.tags.append(QString::fromStdString(tag.toString().toStdString()));
                }
                if (entry.name.isEmpty())
                    entry.name = file.getFile().getFileNameWithoutExtension();
                index.push_back(entry);
            }
        }
    }

    // Scan user patterns
    juce::File userDir = root.getChildFile("user");
    if (userDir.isDirectory())
    {
        for (const auto& categoryDir : juce::RangedDirectoryIterator(userDir, false, "*", juce::File::findDirectories))
        {
            juce::String category = categoryDir.getFilename();
            for (const auto& file : juce::RangedDirectoryIterator(categoryDir.getFile(), false, "*.json"))
            {
                PatternIndexEntry entry;
                entry.id = generateId("user", category, file.getFile().getFileNameWithoutExtension());
                entry.path = "user/" + category + "/" + file.getFilename();
                entry.source = "user";

                QJsonDocument doc = QJsonDocument::fromJson(file.getFile().loadFileAsString().toUtf8());
                if (doc.isObject())
                {
                    QJsonObject obj = doc.object();
                    entry.name = QString::fromStdString(obj["name"].toString().toStdString());
                    entry.style = QString::fromStdString(obj["style"].toString().toStdString());
                    for (const auto& tag : obj["tags"].toArray())
                        entry.tags.append(QString::fromStdString(tag.toString().toStdString()));
                }
                if (entry.name.isEmpty())
                    entry.name = file.getFile().getFileNameWithoutExtension();
                index.push_back(entry);
            }
        }
    }

    writeIndex();
}

bool PatternLibrary::isFactoryPattern(const juce::String& id) const
{
    return id.startsWith("factory/");
}

void PatternLibrary::writeIndex()
{
    QJsonObject obj;
    obj["version"] = 1;
    obj["generatedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();

    QJsonArray patternsArr;
    for (const auto& entry : index)
    {
        QJsonObject entryObj;
        entryObj["id"] = entry.id.toStdString();
        entryObj["path"] = entry.path.toStdString();
        entryObj["name"] = entry.name.toStdString();
        entryObj["style"] = entry.style.toStdString();
        entryObj["category"] = entry.category.toStdString();
        entryObj["source"] = entry.source.toStdString();

        QJsonArray tagsArr;
        for (const auto& tag : entry.tags)
            tagsArr.append(tag.toStdString());
        entryObj["tags"] = tagsArr;

        patternsArr.append(entryObj);
    }
    obj["patterns"] = patternsArr;

    QJsonDocument doc(obj);
    indexFile().replaceWithText(doc.toJson().constData());
}

void PatternLibrary::readIndex()
{
    index.clear();
    if (!indexFile().existsAsFile())
    {
        rebuildIndex();
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(indexFile().loadFileAsString().toUtf8());
    if (!doc.isObject())
    {
        rebuildIndex();
        return;
    }

    QJsonObject obj = doc.object();
    for (const auto& entryVal : obj["patterns"].toArray())
    {
        QJsonObject entryObj = entryVal.toObject();
        PatternIndexEntry entry;
        entry.id = QString::fromStdString(entryObj["id"].toString().toStdString());
        entry.path = QString::fromStdString(entryObj["path"].toString().toStdString());
        entry.name = QString::fromStdString(entryObj["name"].toString().toStdString());
        entry.style = QString::fromStdString(entryObj["style"].toString().toStdString());
        entry.category = QString::fromStdString(entryObj["category"].toString().toStdString());
        entry.source = QString::fromStdString(entryObj["source"].toString().toStdString());

        for (const auto& tag : entryObj["tags"].toArray())
            entry.tags.append(QString::fromStdString(tag.toString().toStdString()));

        index.push_back(entry);
    }
}

} // namespace HDAW
```

- [ ] **Step 3: Verify it compiles**

Run: `cmake --build build --config Debug --target HDAW_lib`
Expected: Build succeeds

- [ ] **Step 4: Commit**

```bash
git add src/engine/PatternLibrary.cpp CMakeLists.txt
git commit -m "feat: implement PatternLibrary with save/load/delete/list/import/export"
```

---

## Task 3: PatternLibrary Unit Tests

**Files:**
- Create: `tests/unit/engine/pattern_library_test.cpp`
- Modify: `tests/CMakeLists.txt` (add test file after line 71)

- [ ] **Step 1: Add test file to tests/CMakeLists.txt**

After the line `unit/engine/rhythm_pattern_generator_test.cpp` (line 71), add:

```cpp
    unit/engine/pattern_library_test.cpp
```

- [ ] **Step 2: Write the test file**

```cpp
#include <gtest/gtest.h>
#include "PatternLibrary.h"
#include <juce_core/juce_core.h>

class PatternLibraryTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Use a temp directory for test isolation
        tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("hdaw_pattern_lib_test_" + juce::String(juce::Time::getMillisecondCounter()));
        tempDir.createDirectory();
        lib = std::make_unique<HDAW::PatternLibrary>(tempDir);
    }

    void TearDown() override
    {
        lib.reset();
        tempDir.deleteRecursively();
    }

    HDAW::PatternPreset makeTestPreset(const juce::String& name, const juce::String& style = "Standard")
    {
        HDAW::PatternPreset p;
        p.name = name;
        p.style = style;
        p.category = "test";
        p.tags = {"test", "unit"};
        p.paramsJson = R"({"scaleRoot":0,"scaleMode":0,"lowNote":48,"highNote":84,"minVelocity":60,"maxVelocity":110,"seed":42,"lengthBeats":4.0,"density":8,"noteDuration":0.5})";
        p.styleParamsJson = "{}";
        return p;
    }

    std::unique_ptr<HDAW::PatternLibrary> lib;
    juce::File tempDir;
};

TEST_F(PatternLibraryTest, SaveAndLoadRoundtrip)
{
    auto preset = makeTestPreset("My Test Pattern", "Arpeggio");
    juce::String error;
    ASSERT_TRUE(lib->savePattern(preset, error)) << error.toStdString();

    // Load it back
    juce::String id = "user/test/My Test Pattern";
    HDAW::PatternPreset loaded;
    ASSERT_TRUE(lib->loadPattern(id, loaded, error)) << error.toStdString();
    EXPECT_EQ(loaded.name, "My Test Pattern");
    EXPECT_EQ(loaded.style, "Arpeggio");
    EXPECT_EQ(loaded.category, "test");
}

TEST_F(PatternLibraryTest, ListPatterns)
{
    lib->savePattern(makeTestPreset("Pattern A", "Standard"), juce::String());
    lib->savePattern(makeTestPreset("Pattern B", "Arpeggio"), juce::String());

    auto all = lib->listPatterns();
    EXPECT_EQ(all.size(), 2u);

    auto filtered = lib->listPatterns({}, "Arpeggio");
    EXPECT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0].style, "Arpeggio");
}

TEST_F(PatternLibraryTest, DeletePattern)
{
    lib->savePattern(makeTestPreset("To Delete"), juce::String());
    juce::String id = "user/test/To Delete";

    juce::String error;
    ASSERT_TRUE(lib->deletePattern(id, error)) << error.toStdString();

    HDAW::PatternPreset loaded;
    EXPECT_FALSE(lib->deletePattern(id, error));
}

TEST_F(PatternLibraryTest, FactoryPatternCannotBeDeleted)
{
    // Create a fake factory pattern
    juce::File factoryDir = tempDir.getChildFile("_factory/test");
    factoryDir.createDirectory();
    juce::File factoryFile = factoryDir.getChildFile("factory-pattern.json");
    factoryFile.replaceWithText(R"({"version":1,"name":"Factory One","style":"Standard","category":"test","tags":["factory"]})");

    lib->rebuildIndex();

    juce::String error;
    EXPECT_FALSE(lib->deletePattern("factory/test/factory-pattern", error));
    EXPECT_EQ(error, "cannot delete factory patterns");
}

TEST_F(PatternLibraryTest, ImportJsonString)
{
    juce::String json = R"({
        "version": 1,
        "name": "Imported Pattern",
        "style": "BassLine",
        "category": "user",
        "tags": ["import"],
        "params": {"scaleRoot":0,"scaleMode":1},
        "styleParams": {}
    })";

    juce::String id, error;
    ASSERT_TRUE(lib->importPattern(json, id, error)) << error.toStdString();
    EXPECT_TRUE(id.startsWith("user/"));

    HDAW::PatternPreset loaded;
    ASSERT_TRUE(lib->loadPattern(id, loaded, error));
    EXPECT_EQ(loaded.name, "Imported Pattern");
}

TEST_F(PatternLibraryTest, ImportInvalidJsonFails)
{
    juce::String id, error;
    EXPECT_FALSE(lib->importPattern("not json", id, error));
    EXPECT_FALSE(error.isEmpty());
}

TEST_F(PatternLibraryTest, ExportReturnsValidJson)
{
    lib->savePattern(makeTestPreset("Export Me"), juce::String());
    juce::String id = "user/test/Export Me";

    juce::String json, error;
    ASSERT_TRUE(lib->exportPattern(id, json, error)) << error.toStdString();

    // Verify it's valid JSON
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    EXPECT_TRUE(doc.isObject());
    EXPECT_EQ(doc.object()["name"].toString().toStdString(), "Export Me");
}

TEST_F(PatternLibraryTest, IndexRebuildsOnSave)
{
    lib->savePattern(makeTestPreset("First"), juce::String());
    EXPECT_EQ(lib->listPatterns().size(), 1u);

    lib->savePattern(makeTestPreset("Second"), juce::String());
    EXPECT_EQ(lib->listPatterns().size(), 2u);
}

TEST_F(PatternLibraryTest, SanitizedNameTruncates)
{
    juce::String longName(100, 'A');
    auto preset = makeTestPreset(longName);
    juce::String error;
    ASSERT_TRUE(lib->savePattern(preset, error));
    // The file should exist with sanitized name
    auto patterns = lib->listPatterns();
    ASSERT_EQ(patterns.size(), 1u);
    EXPECT_LE(patterns[0].name.length(), 64);
}
```

- [ ] **Step 3: Build and run the tests**

Run: `cmake --build build --config Debug --target hdaw_tests; if ($?) { build\Debug\hdaw_tests.exe --gtest_filter=PatternLibraryTest.* }`
Expected: All 8 tests pass

- [ ] **Step 4: Commit**

```bash
git add tests/unit/engine/pattern_library_test.cpp tests/CMakeLists.txt
git commit -m "test: PatternLibrary save/load/delete/import/export unit tests"
```

---

## Task 4: Extend Style Enum + styleName + getStyleNames RPC

**Files:**
- Modify: `src/engine/PhraseGenerator.h` (add enum values, StyleParams structs)
- Modify: `src/engine/PhraseGenerator.cpp` (extend styleName(), add stubs)
- Modify: `src/frontend/router/Router_Composition.cpp` (extend getStyleNames loop)

- [ ] **Step 1: Extend the Style enum in PhraseGenerator.h**

Replace the existing enum block (lines 41–52):

```cpp
    enum Style {
        Standard = 0,
        Arpeggio,
        BassLine,
        ChordStab,
        Pad,
        Lead,
        RandomWalk,
        Buildup,
        Euclidean,
        Percussion,
        // New styles (10-24)
        TrapHiHat,        // 10
        DrillBass,        // 11
        Counterpoint,     // 12
        WalkingBass,      // 13
        SwingComping,     // 14
        MarkovMelody,     // 15
        EvolvingTexture,  // 16
        Aleatoric,        // 17
        ScalarRun,        // 18
        ChordToneSeq,     // 19
        CallResponse,     // 20
        PhaseShift,       // 21
        AdditiveRhythm,   // 22
        MinimalistLoop,   // 23
        Layered,          // 24
        NumStyles
    };
```

- [ ] **Step 2: Modify PhraseParams to include style-specific params**

In `PhraseGenerator.h`, modify `PhraseParams` to carry style-specific params:

```cpp
    struct PhraseParams : BaseParams {
        Style style = Standard;
        double lengthBeats = 4.0;
        int density = 8;
        double noteDuration = 0.5;

        // Style-specific params (populated from RPC/loadPattern, defaults used otherwise)
        TrapHiHatParams trapHiHat;
        DrillBassParams drillBass;
        CounterpointParams counterpoint;
        WalkingBassParams walkingBass;
        SwingCompingParams swingComping;
        MarkovMelodyParams markovMelody;
        EvolvingTextureParams evolvingTexture;
        AleatoricParams aleatoric;
        ScalarRunParams scalarRun;
        ChordToneSeqParams chordToneSeq;
        CallResponseParams callResponse;
        PhaseShiftParams phaseShift;
        AdditiveRhythmParams additiveRhythm;
        MinimalistLoopParams minimalistLoop;
    };
```

Note: The StyleParams structs referenced here are defined in Step 3 below. Move Step 3 before this step if needed, or forward-declare the structs.

- [ ] **Step 3: Add StyleParams structs after PhraseParams in PhraseGenerator.h**

After the `PhraseParams` struct (after line 71), add:

```cpp
    // ── Style-specific params ──
    struct TrapHiHatParams {
        int rollDensity = 4;         // 2-8
        double velocityDecay = 0.7;  // 0.1-1.0
        double ratchetChance = 0.3;  // 0.0-1.0
    };

    struct DrillBassParams {
        double glideDuration = 0.15; // beats
        double slideIntensity = 0.8; // 0.0-1.0
        bool sustainTail = true;
        double displacement = 0.5;   // 0.0-1.0
    };

    struct CounterpointParams {
        int voiceCount = 2;          // 2-4
        int species = 2;             // 1-5
        int intervalConstraint = 1;  // 0=parallel, 1=contrary, 2=oblique, 3=free
    };

    struct WalkingBassParams {
        bool approachNotes = true;
        double ghostNotes = 0.1;     // 0.0-1.0
        double chromaticism = 0.3;   // 0.0-1.0
    };

    struct SwingCompingParams {
        int swingPercent = 65;       // 50-75
        int compPattern = 0;         // 0-3
        int voicingSpread = 1;       // 0-2
    };

    struct MarkovMelodyParams {
        int rhythmGrid = 16;         // 4/8/16/32
        int stateCount = 7;          // 3-12
    };

    struct EvolvingTextureParams {
        int layerCount = 4;          // 2-8
        double driftSpeed = 0.5;     // beats per semitone
        double densitySwell = 0.5;   // 0.0-1.0
    };

    struct AleatoricParams {
        double constraintTightness = 0.5; // 0.0-1.0
        double rhythmVariety = 0.7;       // 0.0-1.0
        double restProbability = 0.2;     // 0.0-1.0
    };

    struct ScalarRunParams {
        int direction = 0;           // 0=up, 1=down, 2=bounce
        int octaveSpan = 2;          // 1-4
        int runSpeed = 16;           // 4/8/16
    };

    struct ChordToneSeqParams {
        int approachType = 1;        // 0-3
        int patternShape = 0;        // 0-3
    };

    struct CallResponseParams {
        int phraseLength = 4;        // beats
        double responseVariation = 0.5;
        double restBeats = 1.0;
    };

    struct PhaseShiftParams {
        int voice1Grid = 8;          // 4/8/16
        int voice2Grid = 6;          // 4/8/16
        double phaseRate = 0.3;
    };

    struct AdditiveRhythmParams {
        juce::String grouping = "3+3+2";
        int subdivision = 8;         // 4/8/16
    };

    struct MinimalistLoopParams {
        int cellLength = 6;          // 3-12
        double mutationRate = 0.2;   // 0.0-1.0
        int phaseOffset = 0;         // 0-11
    };

    // Get schema for a style's params (for dynamic UI rendering)
    struct ParamField {
        juce::String name;
        juce::String type;  // "int", "float", "bool"
        double min = 0;
        double max = 0;
        double defaultVal = 0;
        juce::String label;
    };
    static std::vector<ParamField> getStyleParamsSchema(Style style);
```

- [ ] **Step 3: Extend styleName() in PhraseGenerator.cpp**

In `PhraseGenerator.cpp`, find the `styleName` function (search for `styleName`). Extend the switch to cover all new styles. The existing function likely uses a switch or array — add cases for indices 10–24:

```cpp
const char* PhraseGenerator::styleName(Style s)
{
    switch (s)
    {
        case Standard:      return "Standard";
        case Arpeggio:      return "Arpeggio";
        case BassLine:      return "BassLine";
        case ChordStab:     return "ChordStab";
        case Pad:           return "Pad";
        case Lead:          return "Lead";
        case RandomWalk:    return "RandomWalk";
        case Buildup:       return "Buildup";
        case Euclidean:     return "Euclidean";
        case Percussion:    return "Percussion";
        case TrapHiHat:     return "TrapHiHat";
        case DrillBass:     return "DrillBass";
        case Counterpoint:  return "Counterpoint";
        case WalkingBass:   return "WalkingBass";
        case SwingComping:  return "SwingComping";
        case MarkovMelody:  return "MarkovMelody";
        case EvolvingTexture: return "EvolvingTexture";
        case Aleatoric:     return "Aleatoric";
        case ScalarRun:     return "ScalarRun";
        case ChordToneSeq:  return "ChordToneSeq";
        case CallResponse:  return "CallResponse";
        case PhaseShift:    return "PhaseShift";
        case AdditiveRhythm: return "AdditiveRhythm";
        case MinimalistLoop: return "MinimalistLoop";
        case Layered:       return "Layered";
        default:            return "Unknown";
    }
}
```

- [ ] **Step 4: Add stub generatePhrase cases for new styles**

In `PhraseGenerator::generatePhrase`, add cases for each new style that return an empty vector (to be implemented in later tasks). This ensures compilation:

```cpp
case TrapHiHat:
case DrillBass:
case Counterpoint:
case WalkingBass:
case SwingComping:
case MarkovMelody:
case EvolvingTexture:
case Aleatoric:
case ScalarRun:
case ChordToneSeq:
case CallResponse:
case PhaseShift:
case AdditiveRhythm:
case MinimalistLoop:
case Layered:
    return {}; // Stub — implemented in Tasks 7-10
```

- [ ] **Step 5: Extend getStyleNames RPC in Router_Composition.cpp**

Change line 62 from:
```cpp
for (int i = 0; i <= static_cast<int>(PhraseGenerator::Euclidean); ++i) {
```
to:
```cpp
for (int i = 0; i < static_cast<int>(PhraseGenerator::NumStyles); ++i) {
```

- [ ] **Step 6: Extend style string mapping in generatePhrase RPC**

In `Router_Composition.cpp`, the `generatePhrase` handler (line 89+) has a chain of `if/else if` for style string matching. Add the new style strings after the existing ones. Find the last `else if` (likely for "Percussion") and add:

```cpp
else if (styleStr == "TrapHiHat")      style = PhraseGenerator::TrapHiHat;
else if (styleStr == "DrillBass")      style = PhraseGenerator::DrillBass;
else if (styleStr == "Counterpoint")   style = PhraseGenerator::Counterpoint;
else if (styleStr == "WalkingBass")    style = PhraseGenerator::WalkingBass;
else if (styleStr == "SwingComping")   style = PhraseGenerator::SwingComping;
else if (styleStr == "MarkovMelody")   style = PhraseGenerator::MarkovMelody;
else if (styleStr == "EvolvingTexture") style = PhraseGenerator::EvolvingTexture;
else if (styleStr == "Aleatoric")      style = PhraseGenerator::Aleatoric;
else if (styleStr == "ScalarRun")      style = PhraseGenerator::ScalarRun;
else if (styleStr == "ChordToneSeq")   style = PhraseGenerator::ChordToneSeq;
else if (styleStr == "CallResponse")   style = PhraseGenerator::CallResponse;
else if (styleStr == "PhaseShift")     style = PhraseGenerator::PhaseShift;
else if (styleStr == "AdditiveRhythm") style = PhraseGenerator::AdditiveRhythm;
else if (styleStr == "MinimalistLoop") style = PhraseGenerator::MinimalistLoop;
else if (styleStr == "Layered")        style = PhraseGenerator::Layered;
```

- [ ] **Step 7: Extend MCP style enum in McpTools_Project.cpp**

In `McpTools_Project.cpp`, find the `generate_phrase` tool registration (around line 992) where the style enum is defined. Extend the `QJsonArray` to include all new style names:

```cpp
{"enum", QJsonArray{"Standard","Arpeggio","BassLine","ChordStab","Pad","Lead","RandomWalk","Buildup","Euclidean","Percussion",
    "TrapHiHat","DrillBass","Counterpoint","WalkingBass","SwingComping",
    "MarkovMelody","EvolvingTexture","Aleatoric","ScalarRun","ChordToneSeq",
    "CallResponse","PhaseShift","AdditiveRhythm","MinimalistLoop","Layered"}}
```

Also find the `kStyleMap` (string-to-enum mapping) in the same file and add entries for all new styles.

- [ ] **Step 8: Add getStyleParamsSchema implementation**

In `PhraseGenerator.cpp`, implement the schema function:

```cpp
std::vector<PhraseGenerator::ParamField> PhraseGenerator::getStyleParamsSchema(Style style)
{
    switch (style)
    {
        case TrapHiHat:
            return {
                {"rollDensity", "int", 2, 8, 4, "Roll Density"},
                {"velocityDecay", "float", 0.1, 1.0, 0.7, "Velocity Decay"},
                {"ratchetChance", "float", 0.0, 1.0, 0.3, "Ratchet Chance"}
            };
        case DrillBass:
            return {
                {"glideDuration", "float", 0.05, 0.5, 0.15, "Glide Duration"},
                {"slideIntensity", "float", 0.0, 1.0, 0.8, "Slide Intensity"},
                {"sustainTail", "bool", 0, 1, 1, "Sustain Tail"},
                {"displacement", "float", 0.0, 1.0, 0.5, "Displacement"}
            };
        case Counterpoint:
            return {
                {"voiceCount", "int", 2, 4, 2, "Voice Count"},
                {"species", "int", 1, 5, 2, "Species"},
                {"intervalConstraint", "int", 0, 3, 1, "Interval Constraint"}
            };
        case WalkingBass:
            return {
                {"approachNotes", "bool", 0, 1, 1, "Approach Notes"},
                {"ghostNotes", "float", 0.0, 1.0, 0.1, "Ghost Notes"},
                {"chromaticism", "float", 0.0, 1.0, 0.3, "Chromaticism"}
            };
        case SwingComping:
            return {
                {"swingPercent", "int", 50, 75, 65, "Swing %"},
                {"compPattern", "int", 0, 3, 0, "Comp Pattern"},
                {"voicingSpread", "int", 0, 2, 1, "Voicing Spread"}
            };
        case MarkovMelody:
            return {
                {"rhythmGrid", "int", 4, 32, 16, "Rhythm Grid"},
                {"stateCount", "int", 3, 12, 7, "State Count"}
            };
        case EvolvingTexture:
            return {
                {"layerCount", "int", 2, 8, 4, "Layer Count"},
                {"driftSpeed", "float", 0.1, 2.0, 0.5, "Drift Speed"},
                {"densitySwell", "float", 0.0, 1.0, 0.5, "Density Swell"}
            };
        case Aleatoric:
            return {
                {"constraintTightness", "float", 0.0, 1.0, 0.5, "Constraint Tightness"},
                {"rhythmVariety", "float", 0.0, 1.0, 0.7, "Rhythm Variety"},
                {"restProbability", "float", 0.0, 1.0, 0.2, "Rest Probability"}
            };
        case ScalarRun:
            return {
                {"direction", "int", 0, 2, 0, "Direction"},
                {"octaveSpan", "int", 1, 4, 2, "Octave Span"},
                {"runSpeed", "int", 4, 16, 16, "Run Speed"}
            };
        case ChordToneSeq:
            return {
                {"approachType", "int", 0, 3, 1, "Approach Type"},
                {"patternShape", "int", 0, 3, 0, "Pattern Shape"}
            };
        case CallResponse:
            return {
                {"phraseLength", "int", 2, 8, 4, "Phrase Length"},
                {"responseVariation", "float", 0.0, 1.0, 0.5, "Response Variation"},
                {"restBeats", "float", 0.0, 2.0, 1.0, "Rest Beats"}
            };
        case PhaseShift:
            return {
                {"voice1Grid", "int", 4, 16, 8, "Voice 1 Grid"},
                {"voice2Grid", "int", 4, 16, 6, "Voice 2 Grid"},
                {"phaseRate", "float", 0.1, 1.0, 0.3, "Phase Rate"}
            };
        case AdditiveRhythm:
            return {
                {"subdivision", "int", 4, 16, 8, "Subdivision"}
                // grouping is a string, handled separately in UI
            };
        case MinimalistLoop:
            return {
                {"cellLength", "int", 3, 12, 6, "Cell Length"},
                {"mutationRate", "float", 0.0, 1.0, 0.2, "Mutation Rate"},
                {"phaseOffset", "int", 0, 11, 0, "Phase Offset"}
            };
        default:
            return {};
    }
}
```

- [ ] **Step 9: Verify it compiles**

Run: `cmake --build build --config Debug --target HDAW_lib; if ($?) { cmake --build build --config Debug --target hdaw_tests }`
Expected: Build succeeds (stubs return empty vectors)

- [ ] **Step 10: Commit**

```bash
git add src/engine/PhraseGenerator.h src/engine/PhraseGenerator.cpp src/frontend/router/Router_Composition.cpp src/mcp/McpTools_Project.cpp
git commit -m "feat: extend Style enum with 15 new styles (stubs) + getStyleParamsSchema + RPC/MCP wiring"
```

---

## Task 5: Pattern Library RPC Methods

**Files:**
- Modify: `src/frontend/router/Router_Composition.cpp`

- [ ] **Step 1: Add PatternLibrary include and instance**

At the top of `Router_Composition.cpp`, add:

```cpp
#include "../../engine/PatternLibrary.h"
```

In the `dispatchComposition` function, after the existing `auto& ag = engine.getAudioGraphCommands();` line, add:

```cpp
    // Pattern library (initialized lazily)
    static HDAW::PatternLibrary patternLib(
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("HDAW").getChildFile("patterns"));
```

- [ ] **Step 2: Modify generatePhrase handler to accept styleParams**

In the existing `generatePhrase` handler (around line 89), after parsing the style string, add parsing for `styleParams`. The handler already constructs a `PhraseParams p` — after that block, add:

```cpp
        // Parse style-specific params from styleParams JSON object
        if (o.contains("styleParams")) {
            QJsonObject sp = o["styleParams"].toObject();
            switch (style) {
                case PhraseGenerator::TrapHiHat:
                    p.trapHiHat.rollDensity = sp.contains("rollDensity") ? sp["rollDensity"].toInt() : p.trapHiHat.rollDensity;
                    p.trapHiHat.velocityDecay = sp.contains("velocityDecay") ? sp["velocityDecay"].toDouble() : p.trapHiHat.velocityDecay;
                    p.trapHiHat.ratchetChance = sp.contains("ratchetChance") ? sp["ratchetChance"].toDouble() : p.trapHiHat.ratchetChance;
                    break;
                case PhraseGenerator::DrillBass:
                    p.drillBass.glideDuration = sp.contains("glideDuration") ? sp["glideDuration"].toDouble() : p.drillBass.glideDuration;
                    p.drillBass.slideIntensity = sp.contains("slideIntensity") ? sp["slideIntensity"].toDouble() : p.drillBass.slideIntensity;
                    p.drillBass.sustainTail = sp.contains("sustainTail") ? sp["sustainTail"].toBool() : p.drillBass.sustainTail;
                    p.drillBass.displacement = sp.contains("displacement") ? sp["displacement"].toDouble() : p.drillBass.displacement;
                    break;
                case PhraseGenerator::Counterpoint:
                    p.counterpoint.voiceCount = sp.contains("voiceCount") ? sp["voiceCount"].toInt() : p.counterpoint.voiceCount;
                    p.counterpoint.species = sp.contains("species") ? sp["species"].toInt() : p.counterpoint.species;
                    p.counterpoint.intervalConstraint = sp.contains("intervalConstraint") ? sp["intervalConstraint"].toInt() : p.counterpoint.intervalConstraint;
                    break;
                case PhraseGenerator::WalkingBass:
                    p.walkingBass.approachNotes = sp.contains("approachNotes") ? sp["approachNotes"].toBool() : p.walkingBass.approachNotes;
                    p.walkingBass.ghostNotes = sp.contains("ghostNotes") ? sp["ghostNotes"].toDouble() : p.walkingBass.ghostNotes;
                    p.walkingBass.chromaticism = sp.contains("chromaticism") ? sp["chromaticism"].toDouble() : p.walkingBass.chromaticism;
                    break;
                case PhraseGenerator::SwingComping:
                    p.swingComping.swingPercent = sp.contains("swingPercent") ? sp["swingPercent"].toInt() : p.swingComping.swingPercent;
                    p.swingComping.compPattern = sp.contains("compPattern") ? sp["compPattern"].toInt() : p.swingComping.compPattern;
                    p.swingComping.voicingSpread = sp.contains("voicingSpread") ? sp["voicingSpread"].toInt() : p.swingComping.voicingSpread;
                    break;
                case PhraseGenerator::MarkovMelody:
                    p.markovMelody.rhythmGrid = sp.contains("rhythmGrid") ? sp["rhythmGrid"].toInt() : p.markovMelody.rhythmGrid;
                    p.markovMelody.stateCount = sp.contains("stateCount") ? sp["stateCount"].toInt() : p.markovMelody.stateCount;
                    break;
                case PhraseGenerator::EvolvingTexture:
                    p.evolvingTexture.layerCount = sp.contains("layerCount") ? sp["layerCount"].toInt() : p.evolvingTexture.layerCount;
                    p.evolvingTexture.driftSpeed = sp.contains("driftSpeed") ? sp["driftSpeed"].toDouble() : p.evolvingTexture.driftSpeed;
                    p.evolvingTexture.densitySwell = sp.contains("densitySwell") ? sp["densitySwell"].toDouble() : p.evolvingTexture.densitySwell;
                    break;
                case PhraseGenerator::Aleatoric:
                    p.aleatoric.constraintTightness = sp.contains("constraintTightness") ? sp["constraintTightness"].toDouble() : p.aleatoric.constraintTightness;
                    p.aleatoric.rhythmVariety = sp.contains("rhythmVariety") ? sp["rhythmVariety"].toDouble() : p.aleatoric.rhythmVariety;
                    p.aleatoric.restProbability = sp.contains("restProbability") ? sp["restProbability"].toDouble() : p.aleatoric.restProbability;
                    break;
                case PhraseGenerator::ScalarRun:
                    p.scalarRun.direction = sp.contains("direction") ? sp["direction"].toInt() : p.scalarRun.direction;
                    p.scalarRun.octaveSpan = sp.contains("octaveSpan") ? sp["octaveSpan"].toInt() : p.scalarRun.octaveSpan;
                    p.scalarRun.runSpeed = sp.contains("runSpeed") ? sp["runSpeed"].toInt() : p.scalarRun.runSpeed;
                    break;
                case PhraseGenerator::ChordToneSeq:
                    p.chordToneSeq.approachType = sp.contains("approachType") ? sp["approachType"].toInt() : p.chordToneSeq.approachType;
                    p.chordToneSeq.patternShape = sp.contains("patternShape") ? sp["patternShape"].toInt() : p.chordToneSeq.patternShape;
                    break;
                case PhraseGenerator::CallResponse:
                    p.callResponse.phraseLength = sp.contains("phraseLength") ? sp["phraseLength"].toInt() : p.callResponse.phraseLength;
                    p.callResponse.responseVariation = sp.contains("responseVariation") ? sp["responseVariation"].toDouble() : p.callResponse.responseVariation;
                    p.callResponse.restBeats = sp.contains("restBeats") ? sp["restBeats"].toDouble() : p.callResponse.restBeats;
                    break;
                case PhraseGenerator::PhaseShift:
                    p.phaseShift.voice1Grid = sp.contains("voice1Grid") ? sp["voice1Grid"].toInt() : p.phaseShift.voice1Grid;
                    p.phaseShift.voice2Grid = sp.contains("voice2Grid") ? sp["voice2Grid"].toInt() : p.phaseShift.voice2Grid;
                    p.phaseShift.phaseRate = sp.contains("phaseRate") ? sp["phaseRate"].toDouble() : p.phaseShift.phaseRate;
                    break;
                case PhraseGenerator::AdditiveRhythm:
                    if (sp.contains("grouping")) p.additiveRhythm.grouping = sp["grouping"].toString().toStdString();
                    p.additiveRhythm.subdivision = sp.contains("subdivision") ? sp["subdivision"].toInt() : p.additiveRhythm.subdivision;
                    break;
                case PhraseGenerator::MinimalistLoop:
                    p.minimalistLoop.cellLength = sp.contains("cellLength") ? sp["cellLength"].toInt() : p.minimalistLoop.cellLength;
                    p.minimalistLoop.mutationRate = sp.contains("mutationRate") ? sp["mutationRate"].toDouble() : p.minimalistLoop.mutationRate;
                    p.minimalistLoop.phaseOffset = sp.contains("phaseOffset") ? sp["phaseOffset"].toInt() : p.minimalistLoop.phaseOffset;
                    break;
                default:
                    break;
            }
        }
```

- [ ] **Step 3: Add the 7 new RPC handlers**

Add these after the existing `getStyleParams` handler (or after `getStyleNames`), before the mutations section:

```cpp
    // --- Pattern Library ---

    if (m == "listPatterns") {
        juce::String category = o["category"].toString().toStdString();
        juce::String style = o["style"].toString().toStdString();
        juce::String tag = o["tag"].toString().toStdString();
        auto entries = patternLib.listPatterns(category, style, tag);
        QJsonArray arr;
        for (const auto& e : entries) {
            QJsonArray tags;
            for (const auto& t : e.tags) tags.append(t.toStdString());
            arr.append(QJsonObject{
                {"id", e.id.toStdString()},
                {"name", e.name.toStdString()},
                {"style", e.style.toStdString()},
                {"category", e.category.toStdString()},
                {"tags", tags},
                {"source", e.source.toStdString()}
            });
        }
        return { false, arr };
    }

    if (m == "savePattern") {
        HDAW::PatternPreset preset;
        preset.name = o["name"].toString().toStdString();
        preset.description = o["description"].toString().toStdString();
        preset.category = o["category"].toString("user").toStdString();
        preset.style = o["style"].toString().toStdString();
        preset.author = o["author"].toString("User").toStdString();
        if (o.contains("tags")) {
            for (const auto& t : o["tags"].toArray())
                preset.tags.append(t.toString().toStdString());
        }
        preset.paramsJson = QString::fromUtf8(QJsonDocument(o["params"].toObject()).toJson());
        preset.styleParamsJson = QString::fromUtf8(QJsonDocument(o["styleParams"].toObject()).toJson());

        juce::String error;
        if (!patternLib.savePattern(preset, error))
            return makeError(-32603, error.toStdString());
        juce::String id = "user/" + (preset.category.isEmpty() ? juce::String("user") : preset.category)
                          + "/" + preset.name;
        return { false, QJsonObject{{"id", id.toStdString()}, {"success", true}} };
    }

    if (m == "loadPattern") {
        juce::String id = o["id"].toString().toStdString();
        HDAW::PatternPreset preset;
        juce::String error;
        if (!patternLib.loadPattern(id, preset, error))
            return makeError(-32603, error.toStdString());

        QJsonDocument paramsDoc = QJsonDocument::fromJson(preset.paramsJson.toUtf8());
        QJsonDocument styleDoc = QJsonDocument::fromJson(preset.styleParamsJson.toUtf8());

        QJsonArray tags;
        for (const auto& t : preset.tags) tags.append(t.toStdString());

        return { false, QJsonObject{
            {"name", preset.name.toStdString()},
            {"style", preset.style.toStdString()},
            {"params", paramsDoc.object()},
            {"styleParams", styleDoc.object()},
            {"description", preset.description.toStdString()},
            {"tags", tags}
        }};
    }

    if (m == "deletePattern") {
        juce::String id = o["id"].toString().toStdString();
        juce::String error;
        if (!patternLib.deletePattern(id, error))
            return makeError(-32603, error.toStdString());
        return { false, QJsonObject{{"success", true}} };
    }

    if (m == "importPattern") {
        juce::String json = o["json"].toString().toStdString();
        juce::String id, error;
        if (!patternLib.importPattern(json, id, error))
            return makeError(-32603, error.toStdString());
        return { false, QJsonObject{{"id", id.toStdString()}, {"success", true}} };
    }

    if (m == "exportPattern") {
        juce::String id = o["id"].toString().toStdString();
        juce::String json, error;
        if (!patternLib.exportPattern(id, json, error))
            return makeError(-32603, error.toStdString());
        return { false, QJsonObject{{"json", json.toStdString()}} };
    }

    if (m == "getStyleParams") {
        juce::String styleStr = o["style"].toString().toStdString();
        PhraseGenerator::Style style = PhraseGenerator::Standard;
        // Map string to enum (reuse the same chain from generatePhrase)
        if (styleStr == "TrapHiHat") style = PhraseGenerator::TrapHiHat;
        else if (styleStr == "DrillBass") style = PhraseGenerator::DrillBass;
        // ... (all other mappings)
        auto fields = PhraseGenerator::getStyleParamsSchema(style);
        QJsonArray arr;
        for (const auto& f : fields) {
            arr.append(QJsonObject{
                {"name", f.name.toStdString()},
                {"type", f.type.toStdString()},
                {"min", f.min},
                {"max", f.max},
                {"default", f.defaultVal},
                {"label", f.label.toStdString()}
            });
        }
        return { false, QJsonObject{{"fields", arr}} };
    }
```

- [ ] **Step 3: Verify it compiles**

Run: `cmake --build build --config Debug --target HDAW_lib`
Expected: Build succeeds

- [ ] **Step 4: Commit**

```bash
git add src/frontend/router/Router_Composition.cpp
git commit -m "feat: add pattern library RPC methods (list/save/load/delete/import/export/getStyleParams)"
```

---

## Task 6: Pattern Library MCP Tools

**Files:**
- Modify: `src/mcp/McpTools_Project.cpp`

- [ ] **Step 1: Add PatternLibrary include**

At the top of `McpTools_Project.cpp`, add:

```cpp
#include "../engine/PatternLibrary.h"
```

- [ ] **Step 2: Register the 6 MCP tools**

Add these after the existing `generate_arrangement` tool registration (around line 1200). Use the same `s.registerTool({...})` pattern as the existing tools:

```cpp
    // --- Pattern Library MCP Tools ---

    s.registerTool({"list_patterns", "Browse the pattern library. Returns saved pattern presets that can be loaded into the phrase generator.",
        objSchema({{"category", QJsonObject{{"type","string"}}},
                  {"style",    QJsonObject{{"type","string"}}},
                  {"tag",      QJsonObject{{"type","string"}}}},
                 {}),
        [e](const QJsonObject& a) -> McpToolResult {
            static HDAW::PatternLibrary lib(
                juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                    .getChildFile("HDAW").getChildFile("patterns"));
            auto entries = lib.listPatterns(
                a.value("category").toString().toStdString(),
                a.value("style").toString().toStdString(),
                a.value("tag").toString().toStdString());
            QJsonArray arr;
            for (const auto& e : entries) {
                QJsonArray tags;
                for (const auto& t : e.tags) tags.append(t.toStdString());
                arr.append(QJsonObject{{"id", e.id.toStdString()},
                                       {"name", e.name.toStdString()},
                                       {"style", e.style.toStdString()},
                                       {"category", e.category.toStdString()},
                                       {"tags", tags},
                                       {"source", e.source.toStdString()}});
            }
            return McpToolResult::json(QJsonDocument(arr).toJson().constData());
        }});

    s.registerTool({"save_pattern", "Save generation parameters as a reusable pattern preset.",
        objSchema({{"name",        QJsonObject{{"type","string"}}},
                  {"style",       QJsonObject{{"type","string"}}},
                  {"params",      QJsonObject{{"type","object"}}},
                  {"styleParams", QJsonObject{{"type","object"}}},
                  {"description", QJsonObject{{"type","string"}}},
                  {"tags",        QJsonObject{{"type","array"},{"items",QJsonObject{{"type","string"}}}}},
                  {"category",    QJsonObject{{"type","string"}}}},
                 {"name","style","params"}),
        [e](const QJsonObject& a) -> McpToolResult {
            static HDAW::PatternLibrary lib(
                juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                    .getChildFile("HDAW").getChildFile("patterns"));
            HDAW::PatternPreset preset;
            preset.name = a.value("name").toString().toStdString();
            preset.style = a.value("style").toString().toStdString();
            preset.category = a.value("category").toString("user").toStdString();
            preset.description = a.value("description").toString().toStdString();
            preset.author = "User";
            if (a.contains("tags"))
                for (const auto& t : a.value("tags").toArray())
                    preset.tags.append(t.toString().toStdString());
            preset.paramsJson = QString::fromUtf8(QJsonDocument(a.value("params").toObject()).toJson());
            preset.styleParamsJson = QString::fromUtf8(QJsonDocument(a.value("styleParams").toObject()).toJson());
            juce::String error;
            if (!lib.savePattern(preset, error))
                return McpToolResult::text(error.toStdString(), true);
            return McpToolResult::text("Pattern saved: " + preset.name.toStdString());
        }});

    s.registerTool({"load_pattern", "Load a pattern preset's parameters into the phrase generator.",
        objSchema({{"id", QJsonObject{{"type","string"}}}}, {"id"}),
        [e](const QJsonObject& a) -> McpToolResult {
            static HDAW::PatternLibrary lib(
                juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                    .getChildFile("HDAW").getChildFile("patterns"));
            HDAW::PatternPreset preset;
            juce::String error;
            if (!lib.loadPattern(a.value("id").toString().toStdString(), preset, error))
                return McpToolResult::text(error.toStdString(), true);
            QJsonDocument paramsDoc = QJsonDocument::fromJson(preset.paramsJson.toUtf8());
            QJsonDocument styleDoc = QJsonDocument::fromJson(preset.styleParamsJson.toUtf8());
            QJsonObject result{{"name", preset.name.toStdString()},
                               {"style", preset.style.toStdString()},
                               {"params", paramsDoc.object()},
                               {"styleParams", styleDoc.object()}};
            return McpToolResult::json(QJsonDocument(result).toJson().constData());
        }});

    s.registerTool({"delete_pattern", "Delete a user-created pattern preset. Factory presets cannot be deleted.",
        objSchema({{"id", QJsonObject{{"type","string"}}}}, {"id"}),
        [e](const QJsonObject& a) -> McpToolResult {
            static HDAW::PatternLibrary lib(
                juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                    .getChildFile("HDAW").getChildFile("patterns"));
            juce::String error;
            if (!lib.deletePattern(a.value("id").toString().toStdString(), error))
                return McpToolResult::text(error.toStdString(), true);
            return McpToolResult::text("Pattern deleted.");
        }});

    s.registerTool({"import_pattern", "Import a JSON pattern file or string into the pattern library.",
        objSchema({{"json", QJsonObject{{"type","string"}}}}, {"json"}),
        [e](const QJsonObject& a) -> McpToolResult {
            static HDAW::PatternLibrary lib(
                juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                    .getChildFile("HDAW").getChildFile("patterns"));
            juce::String id, error;
            if (!lib.importPattern(a.value("json").toString().toStdString(), id, error))
                return McpToolResult::text(error.toStdString(), true);
            return McpToolResult::text("Pattern imported: " + id.toStdString());
        }});

    s.registerTool({"export_pattern", "Export a pattern preset as a JSON string.",
        objSchema({{"id", QJsonObject{{"type","string"}}}}, {"id"}),
        [e](const QJsonObject& a) -> McpToolResult {
            static HDAW::PatternLibrary lib(
                juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                    .getChildFile("HDAW").getChildFile("patterns"));
            juce::String json, error;
            if (!lib.exportPattern(a.value("id").toString().toStdString(), json, error))
                return McpToolResult::text(error.toStdString(), true);
            return McpToolResult::text(json.toStdString());
        }});
```

- [ ] **Step 3: Verify it compiles**

Run: `cmake --build build --config Debug --target HDAW_lib`
Expected: Build succeeds

- [ ] **Step 4: Commit**

```bash
git add src/mcp/McpTools_Project.cpp
git commit -m "feat: add pattern library MCP tools (list/save/load/delete/import/export)"
```

---

## Task 7: Implement New Styles — Batch 1 (Trap/Drill + Classical/Jazz)

**Files:**
- Modify: `src/engine/PhraseGenerator.cpp` (replace stub cases with real generation logic)

- [ ] **Step 1: Implement TrapHiHat style**

Replace the `case TrapHiHat:` stub in `generatePhrase` with:

```cpp
case TrapHiHat:
{
    // 32nd-note rolls with ratchet bursts
    ScopedRng scoped(makeRng(params.seed));
    auto& rng = current();
    auto scale = buildScalePitches(params.scaleRoot, params.scaleMode, params.lowNote, params.highNote);
    if (scale.empty()) return {};

    TrapHiHatParams sp = params.trapHiHat;
    std::vector<GeneratedNote> notes;

    double stepDuration = params.lengthBeats / (params.density * 4.0); // 32nd note grid
    for (int i = 0; i < params.density; ++i)
    {
        double baseTime = i * stepDuration * 4.0;
        int pitch = scale[rng.nextInt(0, (int)scale.size() - 1)];
        int vel = rng.nextInt(params.minVelocity, params.maxVelocity);

        // Main hit
        notes.push_back({baseTime, pitch, vel, stepDuration * 0.8});

        // Ratchet burst
        if (rng.nextFloat() < sp.ratchetChance)
        {
            int rolls = rng.nextInt(2, sp.rollDensity);
            for (int r = 1; r < rolls; ++r)
            {
                double ratchetTime = baseTime + r * stepDuration;
                int ratchetVel = (int)(vel * std::pow(sp.velocityDecay, r));
                if (ratchetVel >= params.minVelocity)
                    notes.push_back({ratchetTime, pitch, ratchetVel, stepDuration * 0.6});
            }
        }
    }
    std::sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a.startBeat < b.startBeat; });
    return notes;
}
```

- [ ] **Step 2: Implement DrillBass style**

```cpp
case DrillBass:
{
    ScopedRng scoped(makeRng(params.seed));
    auto& rng = current();
    auto scale = buildScalePitches(params.scaleRoot, params.scaleMode, params.lowNote, params.highNote);
    if (scale.empty()) return {};

    DrillBassParams sp = params.drillBass;
    std::vector<GeneratedNote> notes;

    double stepDuration = params.lengthBeats / params.density;
    for (int i = 0; i < params.density; ++i)
    {
        double baseTime = i * stepDuration;
        // Prefer root and fifth
        int rootIdx = 0;
        int fifthIdx = std::min((int)scale.size() - 1, 4);
        int pitch;
        if (rng.nextFloat() < 0.45)
            pitch = scale[rootIdx];
        else if (rng.nextFloat() < 0.5)
            pitch = scale[fifthIdx];
        else
            pitch = scale[rng.nextInt(0, (int)scale.size() - 1)];

        // Force into low range
        while (pitch > params.highNote) pitch -= 12;
        while (pitch < params.lowNote) pitch += 12;

        double dur = stepDuration * (sp.sustainTail ? 0.95 : 0.6);
        int vel = rng.nextInt(params.minVelocity, params.maxVelocity);
        notes.push_back({baseTime, pitch, vel, dur});
    }
    std::sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a.startBeat < b.startBeat; });
    return notes;
}
```

- [ ] **Step 3: Implement WalkingBass style**

```cpp
case WalkingBass:
{
    ScopedRng scoped(makeRng(params.seed));
    auto& rng = current();
    auto scale = buildScalePitches(params.scaleRoot, params.scaleMode, params.lowNote, params.highNote);
    if (scale.empty()) return {};

    WalkingBassParams sp = params.walkingBass;
    std::vector<GeneratedNote> notes;

    double stepDuration = params.lengthBeats / params.density;
    int currentDegree = 0;

    for (int i = 0; i < params.density; ++i)
    {
        double beat = i * stepDuration;
        int degree;

        if (i % 4 == 0)
        {
            // Downbeat: target chord tone (root or fifth)
            degree = (rng.nextFloat() < 0.6) ? 0 : 4;
        }
        else if (sp.approachNotes && rng.nextFloat() < 0.6)
        {
            // Approach note: chromatic step toward next downbeat
            int nextDownbeatDegree = 0;
            int stepsToGo = 4 - (i % 4);
            degree = nextDownbeatDegree - stepsToGo;
            if (degree < 0) degree += (int)scale.size();
        }
        else
        {
            // Stepwise motion
            degree = currentDegree + rng.nextInt(-1, 1);
            if (sp.chromaticism > rng.nextFloat())
                degree += (rng.nextFloat() < 0.5) ? -1 : 1; // chromatic neighbor
        }

        degree = ((degree % (int)scale.size()) + (int)scale.size()) % (int)scale.size();
        int pitch = scale[degree];

        double dur = stepDuration * 0.85;
        int vel = rng.nextInt(params.minVelocity, params.maxVelocity);
        if (sp.ghostNotes > rng.nextFloat())
            vel = (int)(vel * 0.5);

        notes.push_back({beat, pitch, vel, dur});
        currentDegree = degree;
    }
    std::sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a.startBeat < b.startBeat; });
    return notes;
}
```

- [ ] **Step 4: Implement Counterpoint style**

```cpp
case Counterpoint:
{
    ScopedRng scoped(makeRng(params.seed));
    auto& rng = current();
    auto scale = buildScalePitches(params.scaleRoot, params.scaleMode, params.lowNote, params.highNote);
    if (scale.empty()) return {};

    CounterpointParams sp = params.counterpoint;
    std::vector<GeneratedNote> notes;

    double beatDuration = params.lengthBeats / (params.density / sp.voiceCount);
    int notesPerVoice = params.density / sp.voiceCount;

    for (int v = 0; v < sp.voiceCount; ++v)
    {
        int voiceOffset = v * 12; // Each voice an octave apart
        int currentDegree = rng.nextInt(0, (int)scale.size() - 1);

        for (int i = 0; i < notesPerVoice; ++i)
        {
            double beat = i * beatDuration;
            int degree;

            if (sp.intervalConstraint == 0) // Parallel
                degree = currentDegree;
            else if (sp.intervalConstraint == 1) // Contrary
                degree = ((int)scale.size() - 1) - currentDegree;
            else if (sp.intervalConstraint == 2) // Oblique
                degree = (i % 2 == 0) ? currentDegree : currentDegree + rng.nextInt(-1, 1);
            else // Free
                degree = currentDegree + rng.nextInt(-2, 2);

            degree = ((degree % (int)scale.size()) + (int)scale.size()) % (int)scale.size();
            int pitch = scale[degree] + voiceOffset;

            int vel = rng.nextInt(params.minVelocity, params.maxVelocity);
            notes.push_back({beat + v * beatDuration * 0.25, pitch, vel, beatDuration * 0.9});
            currentDegree = degree;
        }
    }
    std::sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a.startBeat < b.startBeat; });
    return notes;
}
```

- [ ] **Step 5: Implement SwingComping style**

```cpp
case SwingComping:
{
    ScopedRng scoped(makeRng(params.seed));
    auto& rng = current();
    auto scale = buildScalePitches(params.scaleRoot, params.scaleMode, params.lowNote, params.highNote);
    if (scale.empty()) return {};

    SwingCompingParams sp = params.swingComping;
    std::vector<GeneratedNote> notes;

    double beatDuration = params.lengthBeats / params.density;
    double swingOffset = (sp.swingPercent - 50) / 100.0 * beatDuration * 0.5;

    // Comp patterns (beat positions within a 4-beat bar, as fractions)
    std::vector<std::vector<double>> patterns = {
        {0.0, 1.5 + swingOffset, 2.0, 3.0 + swingOffset},        // Charleston
        {0.5 + swingOffset, 1.0, 2.5 + swingOffset, 3.5},        // Shifted beat
        {0.0, 2.0},                                                // Sparse
        {0.0, 0.5 + swingOffset, 1.0, 1.5 + swingOffset, 2.0, 2.5 + swingOffset, 3.0, 3.5 + swingOffset} // Dense
    };

    const auto& compBeats = patterns[sp.compPattern % patterns.size()];

    // Build chord from scale (root, third, fifth)
    int root = scale[0];
    int third = scale[std::min(2, (int)scale.size() - 1)];
    int fifth = scale[std::min(4, (int)scale.size() - 1)];
    std::vector<int> chord = {root, third, fifth};

    for (double barBeat = 0.0; barBeat < params.lengthBeats; barBeat += 4.0)
    {
        for (double beat : compBeats)
        {
            if (barBeat + beat >= params.lengthBeats) break;

            int vel = rng.nextInt(params.minVelocity, params.maxVelocity);
            for (int p : chord)
            {
                int pitch = p;
                while (pitch < params.lowNote) pitch += 12;
                while (pitch > params.highNote) pitch -= 12;
                notes.push_back({barBeat + beat, pitch, vel, beatDuration * 0.3});
            }
        }
    }
    std::sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a.startBeat < b.startBeat; });
    return notes;
}
```

- [ ] **Step 6: Verify compilation**

Run: `cmake --build build --config Debug --target HDAW_lib`
Expected: Build succeeds

- [ ] **Step 7: Write batch 1 tests**

Add to `tests/unit/engine/phrase_generator_new_styles_test.cpp` (see Task 9 for the full file — add these tests first):

```cpp
TEST(PhraseGeneratorNewStyles, TrapHiHatGeneratesNotes)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::TrapHiHat;
    p.seed = 42;
    p.lengthBeats = 4.0;
    p.density = 8;
    auto notes = PhraseGenerator::generatePhrase(p);
    EXPECT_FALSE(notes.empty());
    for (const auto& n : notes)
    {
        EXPECT_GE(n.startBeat, 0.0);
        EXPECT_LE(n.startBeat, p.lengthBeats);
        EXPECT_GE(n.velocity, 1);
        EXPECT_LE(n.velocity, 127);
    }
}

TEST(PhraseGeneratorNewStyles, DrillBassGeneratesNotes)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::DrillBass;
    p.seed = 42;
    p.lengthBeats = 8.0;
    p.density = 6;
    p.lowNote = 36;
    p.highNote = 48;
    auto notes = PhraseGenerator::generatePhrase(p);
    EXPECT_FALSE(notes.empty());
    for (const auto& n : notes)
        EXPECT_GE(n.noteNumber, 24); // Should be in bass range
}

TEST(PhraseGeneratorNewStyles, WalkingBassGeneratesNotes)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::WalkingBass;
    p.seed = 42;
    p.lengthBeats = 4.0;
    p.density = 16;
    auto notes = PhraseGenerator::generatePhrase(p);
    EXPECT_FALSE(notes.empty());
}

TEST(PhraseGeneratorNewStyles, SwingCompingGeneratesChords)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::SwingComping;
    p.seed = 42;
    p.lengthBeats = 8.0;
    p.density = 16;
    auto notes = PhraseGenerator::generatePhrase(p);
    EXPECT_FALSE(notes.empty());
    // Should have multiple notes at the same time (chords)
}
```

- [ ] **Step 8: Commit**

```bash
git add src/engine/PhraseGenerator.cpp
git commit -m "feat: implement TrapHiHat, DrillBass, WalkingBass, Counterpoint, SwingComping styles"
```

---

## Task 8: Implement New Styles — Batch 2 (Generative/Ambient + Melodic)

**Files:**
- Modify: `src/engine/PhraseGenerator.cpp`

- [ ] **Step 1: Implement MarkovMelody style**

```cpp
case MarkovMelody:
{
    ScopedRng scoped(makeRng(params.seed));
    auto& rng = current();
    auto scale = buildScalePitches(params.scaleRoot, params.scaleMode, params.lowNote, params.highNote);
    if (scale.empty()) return {};

    MarkovMelodyParams sp = params.markovMelody;
    std::vector<GeneratedNote> notes;

    double stepDuration = params.lengthBeats / params.density;
    int currentDegree = rng.nextInt(0, std::min(sp.stateCount, (int)scale.size()) - 1);

    for (int i = 0; i < params.density; ++i)
    {
        double beat = i * stepDuration;
        int nextDegree = HDAW::nextMarkovDegree(rng, currentDegree, std::min(sp.stateCount, (int)scale.size()));
        int pitch = scale[nextDegree % scale.size()];

        // Octave placement
        while (pitch < params.lowNote) pitch += 12;
        while (pitch > params.highNote) pitch -= 12;

        double dur = stepDuration * (0.5 + rng.nextFloat() * 0.5);
        int vel = rng.nextInt(params.minVelocity, params.maxVelocity);
        notes.push_back({beat, pitch, vel, dur});
        currentDegree = nextDegree;
    }
    std::sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a.startBeat < b.startBeat; });
    return notes;
}
```

- [ ] **Step 2: Implement EvolvingTexture style**

```cpp
case EvolvingTexture:
{
    ScopedRng scoped(makeRng(params.seed));
    auto& rng = current();
    auto scale = buildScalePitches(params.scaleRoot, params.scaleMode, params.lowNote, params.highNote);
    if (scale.empty()) return {};

    EvolvingTextureParams sp = params.evolvingTexture;
    std::vector<GeneratedNote> notes;

    int totalNotes = sp.layerCount * params.density;
    double noteDuration = params.lengthBeats / params.density;

    for (int i = 0; i < sp.layerCount; ++i)
    {
        int startDegree = rng.nextInt(0, (int)scale.size() - 1);
        double startBeat = rng.nextFloat() * params.lengthBeats * 0.3;

        for (int j = 0; j < params.density / sp.layerCount + 1; ++j)
        {
            double beat = startBeat + j * noteDuration * (1.0 + sp.densitySwell * 0.5);
            if (beat >= params.lengthBeats) break;

            int degree = (startDegree + j) % (int)scale.size();
            int pitch = scale[degree];
            while (pitch < params.lowNote) pitch += 12;
            while (pitch > params.highNote) pitch -= 12;

            double dur = noteDuration * (2.0 + rng.nextFloat() * 4.0);
            int vel = rng.nextInt(params.minVelocity, params.maxVelocity);
            notes.push_back({beat, pitch, vel, dur});
        }
    }
    std::sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a.startBeat < b.startBeat; });
    return notes;
}
```

- [ ] **Step 3: Implement Aleatoric style**

```cpp
case Aleatoric:
{
    ScopedRng scoped(makeRng(params.seed));
    auto& rng = current();
    auto scale = buildScalePitches(params.scaleRoot, params.scaleMode, params.lowNote, params.highNote);
    if (scale.empty()) return {};

    AleatoricParams sp = params.aleatoric;
    std::vector<GeneratedNote> notes;

    double gridStep = params.lengthBeats / params.density;

    for (int i = 0; i < params.density * 2; ++i) // Over-generate then filter
    {
        double beat = rng.nextFloat() * params.lengthBeats;
        if (beat >= params.lengthBeats) continue;

        // Rest probability
        if (rng.nextFloat() < sp.restProbability) continue;

        // Pitch constrained by tightness
        int pitchIdx;
        if (sp.constraintTightness > rng.nextFloat())
            pitchIdx = rng.nextInt((int)scale.size() / 3, (int)scale.size() * 2 / 3);
        else
            pitchIdx = rng.nextInt(0, (int)scale.size() - 1);

        int pitch = scale[pitchIdx % scale.size()];
        while (pitch < params.lowNote) pitch += 12;
        while (pitch > params.highNote) pitch -= 12;

        double dur = gridStep * (0.25 + rng.nextFloat() * sp.rhythmVariety * 1.75);
        int vel = rng.nextInt(params.minVelocity, params.maxVelocity);
        notes.push_back({beat, pitch, vel, dur});
    }
    std::sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a.startBeat < b.startBeat; });
    return notes;
}
```

- [ ] **Step 4: Implement ScalarRun style**

```cpp
case ScalarRun:
{
    ScopedRng scoped(makeRng(params.seed));
    auto& rng = current();
    auto scale = buildScalePitches(params.scaleRoot, params.scaleMode, params.lowNote, params.highNote);
    if (scale.empty()) return {};

    ScalarRunParams sp = params.scalarRun;
    std::vector<GeneratedNote> notes;

    double stepDuration = params.lengthBeats / params.density;
    int direction = sp.direction; // 0=up, 1=down, 2=bounce
    bool goingUp = true;
    int currentIdx = (direction == 1) ? (int)scale.size() - 1 : 0;
    int rangeSpan = sp.octaveSpan * (int)scale.size();

    for (int i = 0; i < params.density; ++i)
    {
        double beat = i * stepDuration;
        int pitch = scale[currentIdx % scale.size()];

        while (pitch < params.lowNote) pitch += 12;
        while (pitch > params.highNote) pitch -= 12;

        double dur = stepDuration * 0.9;
        int vel = rng.nextInt(params.minVelocity, params.maxVelocity);
        notes.push_back({beat, pitch, vel, dur});

        // Advance
        if (direction == 2) // bounce
        {
            if (goingUp) { currentIdx++; if (currentIdx >= rangeSpan) { goingUp = false; currentIdx--; } }
            else { currentIdx--; if (currentIdx < 0) { goingUp = true; currentIdx++; } }
        }
        else if (direction == 0) // up
            currentIdx++;
        else // down
            currentIdx--;
    }
    std::sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a.startBeat < b.startBeat; });
    return notes;
}
```

- [ ] **Step 5: Implement ChordToneSeq style**

```cpp
case ChordToneSeq:
{
    ScopedRng scoped(makeRng(params.seed));
    auto& rng = current();
    auto scale = buildScalePitches(params.scaleRoot, params.scaleMode, params.lowNote, params.highNote);
    if (scale.empty()) return {};

    ChordToneSeqParams sp = params.chordToneSeq;
    std::vector<GeneratedNote> notes;

    // Use first 4 scale degrees as chord tones
    std::vector<int> chordTones = {0, 2, 4, 6}; // I, iii, V, vii
    for (auto& ct : chordTones)
        ct = scale[ct % scale.size()];

    double stepDuration = params.lengthBeats / params.density;
    int currentIdx = 0;

    for (int i = 0; i < params.density; ++i)
    {
        double beat = i * stepDuration;

        // Advance through chord tones
        if (sp.patternShape == 0) currentIdx = i % chordTones.size(); // ascending
        else if (sp.patternShape == 1) currentIdx = (chordTones.size() - 1) - (i % chordTones.size()); // descending
        else if (sp.patternShape == 2) // up-down
        {
            int cycle = (chordTones.size() * 2 - 2);
            int pos = i % cycle;
            currentIdx = (pos < (int)chordTones.size()) ? pos : (cycle - pos);
        }
        else currentIdx = rng.nextInt(0, (int)chordTones.size() - 1); // random

        int pitch = chordTones[currentIdx % chordTones.size()];

        // Approach notes
        if (sp.approachType == 1) // chromatic
            pitch += rng.nextInt(-1, 1);
        else if (sp.approachType == 2) // scalar
        {
            int deg = rng.nextInt(-1, 1);
            pitch = scale[((currentIdx + deg) % (int)scale.size() + (int)scale.size()) % scale.size()];
        }

        while (pitch < params.lowNote) pitch += 12;
        while (pitch > params.highNote) pitch -= 12;

        double dur = stepDuration * 0.8;
        int vel = rng.nextInt(params.minVelocity, params.maxVelocity);
        notes.push_back({beat, pitch, vel, dur});
    }
    std::sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a.startBeat < b.startBeat; });
    return notes;
}
```

- [ ] **Step 6: Implement CallResponse style**

```cpp
case CallResponse:
{
    ScopedRng scoped(makeRng(params.seed));
    auto& rng = current();
    auto scale = buildScalePitches(params.scaleRoot, params.scaleMode, params.lowNote, params.highNote);
    if (scale.empty()) return {};

    CallResponseParams sp = params.callResponse;
    std::vector<GeneratedNote> notes;

    int phrases = (int)(params.lengthBeats / (sp.phraseLength * 2 + sp.restBeats));
    if (phrases < 1) phrases = 1;

    for (int p = 0; p < phrases; ++p)
    {
        double callStart = p * (sp.phraseLength * 2 + sp.restBeats);
        double responseStart = callStart + sp.phraseLength;

        // Call: generate a short melody
        int callNotes = rng.nextInt(2, 4);
        for (int i = 0; i < callNotes; ++i)
        {
            double beat = callStart + i * (sp.phraseLength / callNotes);
            int pitch = scale[rng.nextInt(0, (int)scale.size() - 1)];
            while (pitch < params.lowNote) pitch += 12;
            while (pitch > params.highNote) pitch -= 12;
            double dur = sp.phraseLength / callNotes * 0.8;
            int vel = rng.nextInt(params.minVelocity, params.maxVelocity);
            notes.push_back({beat, pitch, vel, dur});
        }

        // Response: variation of the call
        int responseNoteCount = rng.nextInt(2, 4);
        for (int i = 0; i < responseNoteCount; ++i)
        {
            double beat = responseStart + i * (sp.phraseLength / responseNoteCount);
            int pitch = scale[rng.nextInt(0, (int)scale.size() - 1)];
            // Apply variation: shift some notes
            if (rng.nextFloat() < sp.responseVariation)
                pitch = scale[rng.nextInt(0, (int)scale.size() - 1)];
            while (pitch < params.lowNote) pitch += 12;
            while (pitch > params.highNote) pitch -= 12;
            double dur = sp.phraseLength / responseNoteCount * 0.8;
            int vel = rng.nextInt(params.minVelocity, params.maxVelocity);
            notes.push_back({beat, pitch, vel, dur});
        }
    }
    std::sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a.startBeat < b.startBeat; });
    return notes;
}
```

- [ ] **Step 7: Verify compilation**

Run: `cmake --build build --config Debug --target HDAW_lib`
Expected: Build succeeds

- [ ] **Step 8: Commit**

```bash
git add src/engine/PhraseGenerator.cpp
git commit -m "feat: implement MarkovMelody, EvolvingTexture, Aleatoric, ScalarRun, ChordToneSeq, CallResponse styles"
```

---

## Task 9: Implement New Styles — Batch 3 (Polyrhythm + Minimalism)

**Files:**
- Modify: `src/engine/PhraseGenerator.cpp`
- Create: `tests/unit/engine/phrase_generator_new_styles_test.cpp`

- [ ] **Step 1: Implement PhaseShift style**

```cpp
case PhaseShift:
{
    ScopedRng scoped(makeRng(params.seed));
    auto& rng = current();
    auto scale = buildScalePitches(params.scaleRoot, params.scaleMode, params.lowNote, params.highNote);
    if (scale.empty()) return {};

    PhaseShiftParams sp = params.phaseShift;
    std::vector<GeneratedNote> notes;

    double beatsPerBar = 4.0;
    int totalSteps1 = (int)(params.lengthBeats / beatsPerBar * sp.voice1Grid);
    int totalSteps2 = (int)(params.lengthBeats / beatsPerBar * sp.voice2Grid);

    // Voice 1
    auto hits1 = HDAW::euclideanSteps(totalSteps1 / 4, totalSteps1);
    int pitch1 = scale[0];
    while (pitch1 < params.lowNote) pitch1 += 12;
    while (pitch1 > params.highNote) pitch1 -= 12;

    for (int h : hits1)
    {
        double beat = h * (params.lengthBeats / totalSteps1);
        if (beat < params.lengthBeats)
            notes.push_back({beat, pitch1, rng.nextInt(params.minVelocity, params.maxVelocity), 0.25});
    }

    // Voice 2
    auto hits2 = HDAW::euclideanSteps(totalSteps2 / 4, totalSteps2);
    int pitch2 = scale[std::min(4, (int)scale.size() - 1)];
    while (pitch2 < params.lowNote) pitch2 += 12;
    while (pitch2 > params.highNote) pitch2 -= 12;

    for (int h : hits2)
    {
        double beat = h * (params.lengthBeats / totalSteps2);
        if (beat < params.lengthBeats)
            notes.push_back({beat, pitch2, rng.nextInt(params.minVelocity, params.maxVelocity), 0.25});
    }

    std::sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a.startBeat < b.startBeat; });
    return notes;
}
```

- [ ] **Step 2: Implement AdditiveRhythm style**

```cpp
case AdditiveRhythm:
{
    ScopedRng scoped(makeRng(params.seed));
    auto& rng = current();
    auto scale = buildScalePitches(params.scaleRoot, params.scaleMode, params.lowNote, params.highNote);
    if (scale.empty()) return {};

    AdditiveRhythmParams sp = params.additiveRhythm;
    std::vector<GeneratedNote> notes;

    // Parse grouping string "3+3+2"
    std::vector<int> groups;
    juce::StringArray parts = juce::StringArray::fromTokens(sp.grouping, "+", "");
    for (const auto& part : parts)
        groups.push_back(part.getIntValue());

    if (groups.empty()) groups = {3, 3, 2}; // fallback

    int totalSteps = 0;
    for (int g : groups) totalSteps += g;

    double stepDuration = params.lengthBeats / totalSteps;
    int stepIdx = 0;
    int pitchIdx = 0;

    for (int rep = 0; rep < (int)std::ceil((double)params.density / totalSteps) + 1; ++rep)
    {
        for (int g : groups)
        {
            for (int s = 0; s < g; ++s)
            {
                double beat = stepIdx * stepDuration;
                if (beat >= params.lengthBeats) break;

                int pitch = scale[pitchIdx % scale.size()];
                while (pitch < params.lowNote) pitch += 12;
                while (pitch > params.highNote) pitch -= 12;

                int vel = (s == 0) ? rng.nextInt(params.maxVelocity - 20, params.maxVelocity)
                                   : rng.nextInt(params.minVelocity, params.maxVelocity - 20);

                notes.push_back({beat, pitch, vel, stepDuration * 0.7});
                stepIdx++;
                pitchIdx++;
            }
        }
    }
    std::sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a.startBeat < b.startBeat; });
    return notes;
}
```

- [ ] **Step 3: Implement MinimalistLoop style**

```cpp
case MinimalistLoop:
{
    ScopedRng scoped(makeRng(params.seed));
    auto& rng = current();
    auto scale = buildScalePitches(params.scaleRoot, params.scaleMode, params.lowNote, params.highNote);
    if (scale.empty()) return {};

    MinimalistLoopParams sp = params.minimalistLoop;
    std::vector<GeneratedNote> notes;

    // Generate initial cell
    std::vector<int> cellPitches;
    std::vector<double> cellDurations;
    for (int i = 0; i < sp.cellLength; ++i)
    {
        cellPitches.push_back(scale[rng.nextInt(0, (int)scale.size() - 1)]);
        cellDurations.push_back(0.5 + rng.nextFloat() * 1.0);
    }

    int repetitions = (int)(params.lengthBeats / (sp.cellLength * 0.75));
    double cellDuration = 0;
    for (double d : cellDurations) cellDuration += d;

    for (int rep = 0; rep < repetitions; ++rep)
    {
        double repStart = rep * cellDuration;
        for (int i = 0; i < sp.cellLength; ++i)
        {
            double beat = repStart;
            for (int j = 0; j < i; ++j) beat += cellDurations[j];

            if (beat >= params.lengthBeats) break;

            int pitch = cellPitches[i];
            // Mutation
            if (rng.nextFloat() < sp.mutationRate)
            {
                pitch = scale[rng.nextInt(0, (int)scale.size() - 1)];
                cellPitches[i] = pitch; // mutate the cell for next rep
            }

            while (pitch < params.lowNote) pitch += 12;
            while (pitch > params.highNote) pitch -= 12;

            double dur = cellDurations[i] * 0.9;
            int vel = rng.nextInt(params.minVelocity, params.maxVelocity);
            notes.push_back({beat, pitch, vel, dur});
        }
    }
    std::sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a.startBeat < b.startBeat; });
    return notes;
}
```

- [ ] **Step 4: Implement Layered stub (returns empty for now)**

```cpp
case Layered:
    // TODO: implement composite layering in a future iteration
    return {};
```

- [ ] **Step 5: Create the test file**

Create `tests/unit/engine/phrase_generator_new_styles_test.cpp` with tests for all 15 new styles (determinism + non-empty output for each):

```cpp
#include <gtest/gtest.h>
#include "engine/PhraseGenerator.h"
#include <vector>

namespace
{
void expectSameNotes(const std::vector<PhraseGenerator::GeneratedNote>& a,
                     const std::vector<PhraseGenerator::GeneratedNote>& b)
{
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i)
    {
        EXPECT_DOUBLE_EQ(a[i].startBeat, b[i].startBeat) << "note " << i;
        EXPECT_EQ(a[i].noteNumber, b[i].noteNumber) << "note " << i;
        EXPECT_EQ(a[i].velocity, b[i].velocity) << "note " << i;
        EXPECT_DOUBLE_EQ(a[i].durationBeats, b[i].durationBeats) << "note " << i;
    }
}
} // namespace

// Test that every new style generates non-empty output and is deterministic
TEST(PhraseGeneratorNewStyles, TrapHiHatDeterminism)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::TrapHiHat;
    p.seed = 42;
    p.lengthBeats = 4.0;
    p.density = 8;
    auto a = PhraseGenerator::generatePhrase(p);
    auto b = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(a.empty());
    expectSameNotes(a, b);
}

TEST(PhraseGeneratorNewStyles, DrillBassDeterminism)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::DrillBass;
    p.seed = 42;
    p.lengthBeats = 8.0;
    p.density = 6;
    auto a = PhraseGenerator::generatePhrase(p);
    auto b = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(a.empty());
    expectSameNotes(a, b);
}

TEST(PhraseGeneratorNewStyles, CounterpointDeterminism)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::Counterpoint;
    p.seed = 42;
    p.lengthBeats = 4.0;
    p.density = 8;
    auto a = PhraseGenerator::generatePhrase(p);
    auto b = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(a.empty());
    expectSameNotes(a, b);
}

TEST(PhraseGeneratorNewStyles, WalkingBassDeterminism)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::WalkingBass;
    p.seed = 42;
    p.lengthBeats = 4.0;
    p.density = 16;
    auto a = PhraseGenerator::generatePhrase(p);
    auto b = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(a.empty());
    expectSameNotes(a, b);
}

TEST(PhraseGeneratorNewStyles, SwingCompingDeterminism)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::SwingComping;
    p.seed = 42;
    p.lengthBeats = 8.0;
    p.density = 16;
    auto a = PhraseGenerator::generatePhrase(p);
    auto b = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(a.empty());
    expectSameNotes(a, b);
}

TEST(PhraseGeneratorNewStyles, MarkovMelodyDeterminism)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::MarkovMelody;
    p.seed = 42;
    p.lengthBeats = 4.0;
    p.density = 16;
    auto a = PhraseGenerator::generatePhrase(p);
    auto b = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(a.empty());
    expectSameNotes(a, b);
}

TEST(PhraseGeneratorNewStyles, EvolvingTextureDeterminism)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::EvolvingTexture;
    p.seed = 42;
    p.lengthBeats = 8.0;
    p.density = 16;
    auto a = PhraseGenerator::generatePhrase(p);
    auto b = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(a.empty());
    expectSameNotes(a, b);
}

TEST(PhraseGeneratorNewStyles, AleatoricDeterminism)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::Aleatoric;
    p.seed = 42;
    p.lengthBeats = 4.0;
    p.density = 16;
    auto a = PhraseGenerator::generatePhrase(p);
    auto b = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(a.empty());
    expectSameNotes(a, b);
}

TEST(PhraseGeneratorNewStyles, ScalarRunDeterminism)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::ScalarRun;
    p.seed = 42;
    p.lengthBeats = 4.0;
    p.density = 16;
    auto a = PhraseGenerator::generatePhrase(p);
    auto b = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(a.empty());
    expectSameNotes(a, b);
}

TEST(PhraseGeneratorNewStyles, ChordToneSeqDeterminism)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::ChordToneSeq;
    p.seed = 42;
    p.lengthBeats = 4.0;
    p.density = 16;
    auto a = PhraseGenerator::generatePhrase(p);
    auto b = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(a.empty());
    expectSameNotes(a, b);
}

TEST(PhraseGeneratorNewStyles, CallResponseDeterminism)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::CallResponse;
    p.seed = 42;
    p.lengthBeats = 16.0;
    p.density = 16;
    auto a = PhraseGenerator::generatePhrase(p);
    auto b = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(a.empty());
    expectSameNotes(a, b);
}

TEST(PhraseGeneratorNewStyles, PhaseShiftDeterminism)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::PhaseShift;
    p.seed = 42;
    p.lengthBeats = 8.0;
    p.density = 16;
    auto a = PhraseGenerator::generatePhrase(p);
    auto b = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(a.empty());
    expectSameNotes(a, b);
}

TEST(PhraseGeneratorNewStyles, AdditiveRhythmDeterminism)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::AdditiveRhythm;
    p.seed = 42;
    p.lengthBeats = 4.0;
    p.density = 16;
    auto a = PhraseGenerator::generatePhrase(p);
    auto b = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(a.empty());
    expectSameNotes(a, b);
}

TEST(PhraseGeneratorNewStyles, MinimalistLoopDeterminism)
{
    PhraseGenerator::PhraseParams p;
    p.style = PhraseGenerator::MinimalistLoop;
    p.seed = 42;
    p.lengthBeats = 8.0;
    p.density = 24;
    auto a = PhraseGenerator::generatePhrase(p);
    auto b = PhraseGenerator::generatePhrase(p);
    ASSERT_FALSE(a.empty());
    expectSameNotes(a, b);
}

TEST(PhraseGeneratorNewStyles, AllNewStylesDeterministic)
{
    for (int style = 10; style <= 23; ++style)
    {
        PhraseGenerator::PhraseParams p;
        p.style = static_cast<PhraseGenerator::Style>(style);
        p.seed = 99;
        p.lengthBeats = 4.0;
        p.density = 8;
        auto a = PhraseGenerator::generatePhrase(p);
        auto b = PhraseGenerator::generatePhrase(p);
        ASSERT_FALSE(a.empty()) << "style " << style;
        expectSameNotes(a, b);
    }
}
```

- [ ] **Step 6: Add test file to tests/CMakeLists.txt**

After the `phrase_generator_test.cpp` line, add:

```cpp
    unit/engine/phrase_generator_new_styles_test.cpp
```

- [ ] **Step 7: Build and run all tests**

Run: `cmake --build build --config Debug --target hdaw_tests; if ($?) { build\Debug\hdaw_tests.exe --gtest_filter=PhraseGeneratorNewStyles.* }`
Expected: All 15 tests pass

- [ ] **Step 8: Commit**

```bash
git add src/engine/PhraseGenerator.cpp tests/unit/engine/phrase_generator_new_styles_test.cpp tests/CMakeLists.txt
git commit -m "feat: implement PhaseShift, AdditiveRhythm, MinimalistLoop styles + full test suite for all 15 new styles"
```

---

## Task 10: Frontend — PresetBrowser Component

**Files:**
- Create: `frontend/src/components/PresetBrowser.tsx`
- Create: `frontend/src/components/PresetBrowser.css`

- [ ] **Step 1: Create PresetBrowser.css**

```css
.preset-browser {
  width: 200px;
  min-width: 200px;
  background: var(--bg-panel);
  border-right: 1px solid var(--border);
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.preset-browser.collapsed {
  width: 0;
  min-width: 0;
  border-right: none;
}

.preset-browser-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 8px;
  border-bottom: 1px solid var(--border);
}

.preset-browser-header h3 {
  margin: 0;
  font-size: 11px;
  text-transform: uppercase;
  letter-spacing: 0.5px;
  color: var(--text-secondary);
}

.preset-browser-actions {
  display: flex;
  gap: 4px;
}

.preset-browser-actions button {
  background: none;
  border: 1px solid var(--border);
  color: var(--text-secondary);
  padding: 2px 6px;
  border-radius: 3px;
  cursor: pointer;
  font-size: 11px;
}

.preset-browser-actions button:hover {
  background: var(--bg-hover);
  color: var(--text);
}

.preset-search {
  padding: 4px 8px;
}

.preset-search input {
  width: 100%;
  background: var(--bg-input);
  border: 1px solid var(--border);
  color: var(--text);
  padding: 4px 6px;
  border-radius: 3px;
  font-size: 11px;
}

.preset-list {
  flex: 1;
  overflow-y: auto;
  padding: 4px 0;
}

.preset-category {
  padding: 4px 8px;
  font-size: 10px;
  text-transform: uppercase;
  letter-spacing: 0.5px;
  color: var(--text-muted);
  cursor: pointer;
  user-select: none;
}

.preset-category:hover {
  color: var(--text-secondary);
}

.preset-item {
  padding: 4px 8px 4px 16px;
  font-size: 12px;
  color: var(--text-secondary);
  cursor: pointer;
  display: flex;
  align-items: center;
  gap: 6px;
}

.preset-item:hover {
  background: var(--bg-hover);
  color: var(--text);
}

.preset-item.active {
  background: var(--accent-bg);
  color: var(--accent);
}

.preset-item .preset-style {
  font-size: 10px;
  color: var(--text-muted);
  margin-left: auto;
}

.preset-item .preset-delete {
  opacity: 0;
  background: none;
  border: none;
  color: var(--danger);
  cursor: pointer;
  font-size: 10px;
  padding: 0 2px;
}

.preset-item:hover .preset-delete {
  opacity: 1;
}
```

- [ ] **Step 2: Create PresetBrowser.tsx**

```tsx
import { useState, useEffect, useCallback, useMemo } from "react";
import { rpc } from "../rpc";
import type { PatternIndexEntry } from "../rpc/types";
import "./PresetBrowser.css";

interface Props {
  onLoadPreset: (preset: {
    name: string;
    style: string;
    params: Record<string, unknown>;
    styleParams: Record<string, unknown>;
  }) => void;
  currentStyle: string;
}

export default function PresetBrowser({ onLoadPreset, currentStyle }: Props) {
  const [patterns, setPatterns] = useState<PatternIndexEntry[]>([]);
  const [search, setSearch] = useState("");
  const [collapsed, setCollapsed] = useState(false);
  const [activeId, setActiveId] = useState<string | null>(null);

  // Load pattern index on mount
  useEffect(() => {
    rpc.call("composition.listPatterns", {}).then((result: any) => {
      setPatterns(result || []);
    });
  }, []);

  // Filter by search
  const filtered = useMemo(() => {
    if (!search) return patterns;
    const lower = search.toLowerCase();
    return patterns.filter(
      (p) =>
        p.name.toLowerCase().includes(lower) ||
        p.style.toLowerCase().includes(lower) ||
        p.tags?.some((t) => t.toLowerCase().includes(lower))
    );
  }, [patterns, search]);

  // Group by category
  const grouped = useMemo(() => {
    const map = new Map<string, PatternIndexEntry[]>();
    for (const p of filtered) {
      const cat = p.category || "other";
      if (!map.has(cat)) map.set(cat, []);
      map.get(cat)!.push(p);
    }
    return map;
  }, [filtered]);

  const handleLoad = useCallback(
    async (id: string) => {
      const result: any = await rpc.call("composition.loadPattern", { id });
      if (result) {
        setActiveId(id);
        onLoadPreset({
          name: result.name,
          style: result.style,
          params: result.params,
          styleParams: result.styleParams,
        });
      }
    },
    [onLoadPreset]
  );

  const handleDelete = useCallback(
    async (id: string, e: React.MouseEvent) => {
      e.stopPropagation();
      if (!confirm("Delete this preset?")) return;
      await rpc.call("composition.deletePattern", { id });
      setPatterns((prev) => prev.filter((p) => p.id !== id));
      if (activeId === id) setActiveId(null);
    },
    [activeId]
  );

  const handleImport = useCallback(async () => {
    const input = document.createElement("input");
    input.type = "file";
    input.accept = ".json";
    input.onchange = async (e) => {
      const file = (e.target as HTMLInputElement).files?.[0];
      if (!file) return;
      const text = await file.text();
      const result: any = await rpc.call("composition.importPattern", { json: text });
      if (result?.success) {
        // Refresh the list
        const updated: any = await rpc.call("composition.listPatterns", {});
        setPatterns(updated || []);
      }
    };
    input.click();
  }, []);

  if (collapsed) {
    return (
      <div className="preset-browser collapsed">
        <button
          className="preset-expand-btn"
          onClick={() => setCollapsed(false)}
          title="Show presets"
        >
          ▶
        </button>
      </div>
    );
  }

  return (
    <div className="preset-browser">
      <div className="preset-browser-header">
        <h3>Presets</h3>
        <div className="preset-browser-actions">
          <button onClick={handleImport} title="Import JSON pattern">
            Import
          </button>
          <button onClick={() => setCollapsed(true)} title="Hide presets">
            ◀
          </button>
        </div>
      </div>

      <div className="preset-search">
        <input
          type="text"
          placeholder="Search..."
          value={search}
          onChange={(e) => setSearch(e.target.value)}
        />
      </div>

      <div className="preset-list">
        {Array.from(grouped.entries()).map(([category, items]) => (
          <div key={category}>
            <div className="preset-category">{category}</div>
            {items.map((p) => (
              <div
                key={p.id}
                className={`preset-item ${activeId === p.id ? "active" : ""}`}
                onClick={() => handleLoad(p.id)}
              >
                <span>{p.name}</span>
                <span className="preset-style">{p.style}</span>
                {p.source === "user" && (
                  <button
                    className="preset-delete"
                    onClick={(e) => handleDelete(p.id, e)}
                    title="Delete preset"
                  >
                    ×
                  </button>
                )}
              </div>
            ))}
          </div>
        ))}
      </div>
    </div>
  );
}
```

- [ ] **Step 3: Verify it compiles**

Run: `cd frontend; npm run build`
Expected: Build succeeds

- [ ] **Step 4: Commit**

```bash
git add frontend/src/components/PresetBrowser.tsx frontend/src/components/PresetBrowser.css
git commit -m "feat: add PresetBrowser sidebar component for pattern library"
```

---

## Task 11: Frontend — Integrate PresetBrowser + Dynamic Style Controls

**Files:**
- Modify: `frontend/src/components/PhraseGeneratorDialog.tsx`
- Modify: `frontend/src/components/PhraseGeneratorDialog.css`

- [ ] **Step 1: Add PresetBrowser import and state**

In `PhraseGeneratorDialog.tsx`, add import at top:

```tsx
import PresetBrowser from "./PresetBrowser";
```

Add state for style-specific params and preset browser:

```tsx
const [styleParams, setStyleParams] = useState<Record<string, unknown>>({});
const [styleParamSchema, setStyleParamSchema] = useState<Array<{
  name: string; type: string; min: number; max: number; default: number; label: string;
}>>([]);
```

- [ ] **Step 2: Load style params schema when style changes**

Add a `useEffect` that calls `getStyleParams` when the phrase style changes:

```tsx
useEffect(() => {
  if (mode !== 0) return; // Only for phrase mode
  rpc.call("composition.getStyleParams", { style: styles[phraseStyle]?.name || "Standard" })
    .then((result: any) => {
      setStyleParamSchema(result?.fields || []);
    });
}, [mode, phraseStyle, styles]);
```

- [ ] **Step 3: Add handleLoadPreset callback**

```tsx
const handleLoadPreset = useCallback((preset: {
  name: string; style: string;
  params: Record<string, unknown>;
  styleParams: Record<string, unknown>;
}) => {
  // Find style index
  const idx = styles.findIndex(s => s.name === preset.style);
  if (idx >= 0) setPhraseStyle(idx);

  // Apply shared params
  const p = preset.params;
  if (p.scaleRoot !== undefined) setScaleRoot(p.scaleRoot as number);
  if (p.scaleMode !== undefined) setScaleMode(p.scaleMode as number);
  if (p.lowNote !== undefined) setLowNote(p.lowNote as number);
  if (p.highNote !== undefined) setHighNote(p.highNote as number);
  if (p.minVelocity !== undefined) setVelocity(p.minVelocity as number);
  if (p.seed !== undefined) setSeed(p.seed as number);
  if (p.lengthBeats !== undefined) setLengthBeats(p.lengthBeats as number);
  if (p.density !== undefined) setDensity(p.density as number);
  if (p.noteDuration !== undefined) setNoteDuration(p.noteDuration as number);

  // Apply style-specific params
  setStyleParams(preset.styleParams || {});
}, [styles]);
```

- [ ] **Step 4: Wrap dialog content with PresetBrowser**

In the JSX, wrap the existing content in a flex container with the PresetBrowser on the left:

```tsx
<div className="pgd-container">
  <PresetBrowser onLoadPreset={handleLoadPreset} currentStyle={styles[phraseStyle]?.name || ""} />
  <div className="pgd-content">
    {/* existing dialog content */}
  </div>
</div>
```

- [ ] **Step 5: Add dynamic style-specific controls**

In the phrase mode section, after the existing controls, add dynamic style params rendering:

```tsx
{mode === 0 && styleParamSchema.length > 0 && (
  <div className="pgd-style-params">
    <h4>{styles[phraseStyle]?.name} Parameters</h4>
    {styleParamSchema.map((field) => (
      <div key={field.name} className="pgd-param-row">
        <label>{field.label}</label>
        {field.type === "bool" ? (
          <input
            type="checkbox"
            checked={(styleParams[field.name] as boolean) ?? (field.default === 1)}
            onChange={(e) =>
              setStyleParams((prev) => ({ ...prev, [field.name]: e.target.checked }))
            }
          />
        ) : field.type === "int" ? (
          <input
            type="number"
            min={field.min}
            max={field.max}
            value={(styleParams[field.name] as number) ?? field.default}
            onChange={(e) =>
              setStyleParams((prev) => ({ ...prev, [field.name]: parseInt(e.target.value) }))
            }
          />
        ) : (
          <input
            type="range"
            min={field.min}
            max={field.max}
            step={0.01}
            value={(styleParams[field.name] as number) ?? field.default}
            onChange={(e) =>
              setStyleParams((prev) => ({ ...prev, [field.name]: parseFloat(e.target.value) }))
            }
          />
        )}
      </div>
    ))}
  </div>
)}
```

- [ ] **Step 6: Add CSS for the container layout**

In `PhraseGeneratorDialog.css`, add:

```css
.pgd-container {
  display: flex;
  height: 100%;
}

.pgd-content {
  flex: 1;
  overflow-y: auto;
  padding: 16px;
}

.pgd-style-params {
  margin-top: 12px;
  padding-top: 12px;
  border-top: 1px solid var(--border);
}

.pgd-style-params h4 {
  margin: 0 0 8px 0;
  font-size: 12px;
  color: var(--text-secondary);
}

.pgd-param-row {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 6px;
}

.pgd-param-row label {
  font-size: 11px;
  color: var(--text-secondary);
  min-width: 100px;
}

.pgd-param-row input[type="range"] {
  flex: 1;
}

.pgd-param-row input[type="number"] {
  width: 60px;
  background: var(--bg-input);
  border: 1px solid var(--border);
  color: var(--text);
  padding: 2px 4px;
  border-radius: 3px;
  font-size: 11px;
}
```

- [ ] **Step 7: Pass styleParams to generatePhrase RPC**

In the `handleGenerate` function, add `styleParams` to the RPC call params:

```tsx
const result = await rpc.call("composition.generatePhrase", {
  trackIndex,
  style: styles[phraseStyle].name,
  lengthBeats,
  density,
  noteDuration,
  scaleRoot,
  scaleMode,
  lowNote,
  highNote,
  minVelocity: velocity,
  maxVelocity: velocity + 20,
  seed,
  styleParams,  // Add this
});
```

- [ ] **Step 8: Verify it compiles**

Run: `cd frontend; npm run build`
Expected: Build succeeds

- [ ] **Step 9: Commit**

```bash
git add frontend/src/components/PhraseGeneratorDialog.tsx frontend/src/components/PhraseGeneratorDialog.css
git commit -m "feat: integrate PresetBrowser + dynamic style controls into PhraseGeneratorDialog"
```

---

## Task 12: Factory Patterns

**Files:**
- Create: `patterns/_factory/trap/*.json`
- Create: `patterns/_factory/jazz/*.json`
- Create: `patterns/_factory/ambient/*.json`
- Create: `patterns/_factory/melodic/*.json`
- Create: `patterns/_factory/polyrhythm/*.json`

- [ ] **Step 1: Create factory pattern directory structure**

```
patterns/_factory/trap/
patterns/_factory/jazz/
patterns/_factory/ambient/
patterns/_factory/melodic/
patterns/_factory/polyrhythm/
```

- [ ] **Step 2: Create 15 factory pattern JSON files**

Create one JSON file per pattern, following the schema from the spec. Examples:

**`patterns/_factory/trap/dark-drill-bass.json`:**
```json
{
  "version": 1,
  "name": "Dark Drill Bass",
  "description": "Sliding 808 with displaced kick pattern, minor pentatonic",
  "category": "trap",
  "tags": ["808", "drill", "bass", "dark"],
  "author": "HDAW",
  "style": "DrillBass",
  "params": {
    "scaleRoot": 0,
    "scaleMode": 10,
    "lowNote": 36,
    "highNote": 48,
    "minVelocity": 80,
    "maxVelocity": 110,
    "seed": 42,
    "lengthBeats": 8.0,
    "density": 6,
    "noteDuration": 0.5
  },
  "styleParams": {
    "glideDuration": 0.15,
    "slideIntensity": 0.8,
    "sustainTail": true,
    "displacement": 0.5
  }
}
```

Create similar files for all 15 patterns listed in the spec. Each should have sensible defaults for its style.

- [ ] **Step 3: Verify patterns load correctly**

Run a quick test: start the engine, call `composition.listPatterns`, verify all factory patterns appear.

- [ ] **Step 4: Commit**

```bash
git add patterns/
git commit -m "feat: add 15 factory pattern presets across all style categories"
```

---

## Task 13: Full Build + Test Verification

- [ ] **Step 1: Full C++ build**

Run: `cmake --build build --config Debug`
Expected: Clean build, no errors

- [ ] **Step 2: Run all C++ tests**

Run: `build\Debug\hdaw_tests.exe --gtest_filter=PhraseGenerator*:PatternLibrary*`
Expected: All tests pass (existing + new)

- [ ] **Step 3: Run full test suite**

Run: `build\Debug\hdaw_tests.exe`
Expected: No regressions

- [ ] **Step 4: Frontend build**

Run: `cd frontend; npm run build`
Expected: Clean build

- [ ] **Step 5: Frontend tests**

Run: `cd frontend; npm test`
Expected: No regressions

- [ ] **Step 6: Commit any fixes**

```bash
git add -A
git commit -m "fix: address build/test issues from pattern library integration"
```

---

## Task 14: Knowledge Graph Refresh

- [ ] **Step 1: Refresh the codebase-memory index**

```
codebase-memory index_repository (repo_path: "D:\pdf\roo projects\hdaw3", mode: "fast")
```

- [ ] **Step 2: Verify new files are indexed**

```
codebase-memory search_graph (query: "PatternLibrary")
```

Expected: Returns `PatternLibrary` class and its methods

---

## Summary

| Task | Description | Est. Time |
|------|-------------|-----------|
| 1 | PatternLibrary header | 10 min |
| 2 | PatternLibrary implementation | 30 min |
| 3 | PatternLibrary unit tests | 20 min |
| 4 | Extend Style enum + RPC wiring | 20 min |
| 5 | Pattern Library RPC methods | 25 min |
| 6 | Pattern Library MCP tools | 20 min |
| 7 | Styles batch 1 (Trap/Drill + Jazz) | 30 min |
| 8 | Styles batch 2 (Ambient + Melodic) | 30 min |
| 9 | Styles batch 3 (Polyrhythm) + tests | 30 min |
| 10 | PresetBrowser component | 20 min |
| 11 | Dialog integration | 25 min |
| 12 | Factory patterns | 15 min |
| 13 | Full build + test verification | 15 min |
| 14 | Knowledge graph refresh | 5 min |
| **Total** | | **~5 hours** |
