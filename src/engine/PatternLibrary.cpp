// src/engine/PatternLibrary.cpp
#include "PatternLibrary.h"
#include <juce_core/juce_core.h>
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

// --- CRUD ---

bool PatternLibrary::savePattern(const PatternPreset& preset, juce::String& outError)
{
    if (!validatePreset(preset, outError))
        return false;

    std::lock_guard<std::mutex> lock(mutex);

    juce::String sanitized = sanitizeName(preset.name);
    if (sanitized.isEmpty())
    {
        outError = "Name produces empty filename after sanitization";
        return false;
    }

    juce::String category = preset.category.toLowerCase();
    if (category.isEmpty())
        category = "user";

    juce::File categoryDir = root.getChildFile("user").getChildFile(category);
    categoryDir.createDirectory();

    juce::File file = categoryDir.getChildFile(sanitized + ".json");

    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("version", preset.version);
    obj->setProperty("name", preset.name);
    obj->setProperty("description", preset.description);
    obj->setProperty("category", preset.category);
    obj->setProperty("author", preset.author);
    obj->setProperty("createdAt", preset.createdAt);
    obj->setProperty("style", preset.style);

    juce::Array<juce::var> tagsArr;
    for (const auto& tag : preset.tags)
        tagsArr.add(tag);
    obj->setProperty("tags", tagsArr);

    if (preset.paramsJson.isNotEmpty())
    {
        auto paramsJson = juce::JSON::parse(preset.paramsJson);
        obj->setProperty("params", paramsJson);
    }

    if (preset.styleParamsJson.isNotEmpty())
    {
        auto styleParamsJson = juce::JSON::parse(preset.styleParamsJson);
        obj->setProperty("styleParams", styleParamsJson);
    }

    juce::String jsonText = juce::JSON::toString(obj.get(), true);
    if (!file.replaceWithText(jsonText))
    {
        outError = "Failed to write file: " + file.getFullPathName();
        return false;
    }

    rebuildIndex();
    return true;
}

bool PatternLibrary::loadPattern(const juce::String& id, PatternPreset& outPreset, juce::String& outError)
{
    std::lock_guard<std::mutex> lock(mutex);

    PatternIndexEntry entry;
    bool found = false;
    for (const auto& e : index)
    {
        if (e.id == id)
        {
            entry = e;
            found = true;
            break;
        }
    }

    if (!found)
    {
        outError = "Pattern not found: " + id;
        return false;
    }

    juce::File file = root.getChildFile(entry.path);
    if (!file.existsAsFile())
    {
        outError = "File does not exist: " + file.getFullPathName();
        return false;
    }

    juce::String content = file.loadFileAsString();
    auto json = juce::JSON::parse(content);
    auto* obj = json.getDynamicObject();
    if (obj == nullptr)
    {
        outError = "Invalid JSON in file: " + file.getFullPathName();
        return false;
    }

    outPreset.version = (int)obj->getProperty("version");
    outPreset.name = obj->getProperty("name").toString();
    outPreset.description = obj->getProperty("description").toString();
    outPreset.category = obj->getProperty("category").toString();
    outPreset.author = obj->getProperty("author").toString();
    outPreset.createdAt = obj->getProperty("createdAt").toString();
    outPreset.style = obj->getProperty("style").toString();

    outPreset.tags.clear();
    if (obj->hasProperty("tags"))
    {
        auto tagsVar = obj->getProperty("tags");
        if (auto* tagsArr = tagsVar.getArray())
        {
            for (const auto& t : *tagsArr)
                outPreset.tags.add(t.toString());
        }
    }

    outPreset.paramsJson = obj->hasProperty("params")
        ? juce::JSON::toString(obj->getProperty("params"), true)
        : juce::String();

    outPreset.styleParamsJson = obj->hasProperty("styleParams")
        ? juce::JSON::toString(obj->getProperty("styleParams"), true)
        : juce::String();

    return true;
}

bool PatternLibrary::deletePattern(const juce::String& id, juce::String& outError)
{
    if (isFactoryPattern(id))
    {
        outError = "Cannot delete factory pattern: " + id;
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex);

    PatternIndexEntry entry;
    bool found = false;
    for (const auto& e : index)
    {
        if (e.id == id)
        {
            entry = e;
            found = true;
            break;
        }
    }

    if (!found)
    {
        outError = "Pattern not found: " + id;
        return false;
    }

    juce::File file = root.getChildFile(entry.path);
    if (file.existsAsFile() && !file.deleteFile())
    {
        outError = "Failed to delete file: " + file.getFullPathName();
        return false;
    }

    rebuildIndex();
    return true;
}

// --- Browse ---

std::vector<PatternIndexEntry> PatternLibrary::listPatterns(
    const juce::String& category, const juce::String& style, const juce::String& tag) const
{
    std::lock_guard<std::mutex> lock(mutex);

    std::vector<PatternIndexEntry> result;
    for (const auto& entry : index)
    {
        if (category.isNotEmpty() && !entry.category.equalsIgnoreCase(category))
            continue;
        if (style.isNotEmpty() && !entry.style.equalsIgnoreCase(style))
            continue;
        if (tag.isNotEmpty())
        {
            bool hasTag = false;
            for (const auto& t : entry.tags)
            {
                if (t.equalsIgnoreCase(tag))
                {
                    hasTag = true;
                    break;
                }
            }
            if (!hasTag)
                continue;
        }
        result.push_back(entry);
    }
    return result;
}

// --- Import/Export ---

bool PatternLibrary::importPattern(const juce::String& jsonString, juce::String& outId, juce::String& outError)
{
    auto json = juce::JSON::parse(jsonString);
    auto* obj = json.getDynamicObject();
    if (obj == nullptr)
    {
        outError = "Invalid JSON";
        return false;
    }

    PatternPreset preset;
    preset.version = (int)obj->getProperty("version");
    preset.name = obj->getProperty("name").toString();
    preset.description = obj->getProperty("description").toString();
    preset.category = obj->getProperty("category").toString();
    preset.author = obj->getProperty("author").toString();
    preset.createdAt = obj->getProperty("createdAt").toString();
    preset.style = obj->getProperty("style").toString();

    if (preset.category.isEmpty())
        preset.category = "user";

    if (obj->hasProperty("tags"))
    {
        if (auto* tagsArr = obj->getProperty("tags").getArray())
        {
            for (const auto& t : *tagsArr)
                preset.tags.add(t.toString());
        }
    }

    preset.paramsJson = obj->hasProperty("params")
        ? juce::JSON::toString(obj->getProperty("params"), true)
        : juce::String();

    preset.styleParamsJson = obj->hasProperty("styleParams")
        ? juce::JSON::toString(obj->getProperty("styleParams"), true)
        : juce::String();

    if (!validatePreset(preset, outError))
        return false;

    std::lock_guard<std::mutex> lock(mutex);

    juce::String sanitized = sanitizeName(preset.name);
    juce::String category = preset.category.toLowerCase();
    if (category.isEmpty()) category = "user";

    juce::File categoryDir = root.getChildFile("user").getChildFile(category);
    categoryDir.createDirectory();
    juce::File file = categoryDir.getChildFile(sanitized + ".json");

    if (file.existsAsFile())
    {
        juce::String baseName = sanitized;
        int counter = 1;
        while (file.existsAsFile() && counter < 100)
        {
            file = categoryDir.getChildFile(baseName + "-" + juce::String(counter) + ".json");
            counter++;
        }
        if (file.existsAsFile())
        {
            outError = "Too many name collisions";
            return false;
        }
        sanitized = file.getFileNameWithoutExtension();
    }

    if (!file.replaceWithText(jsonString))
    {
        outError = "Failed to write file";
        return false;
    }

    outId = generateId("user", category, sanitized);
    rebuildIndex();
    return true;
}

bool PatternLibrary::importPatternFile(const juce::File& file, juce::String& outId, juce::String& outError)
{
    if (!file.existsAsFile())
    {
        outError = "File does not exist: " + file.getFullPathName();
        return false;
    }

    juce::String content = file.loadFileAsString();
    return importPattern(content, outId, outError);
}

bool PatternLibrary::exportPattern(const juce::String& id, juce::String& outJson, juce::String& outError)
{
    PatternPreset preset;
    if (!loadPattern(id, preset, outError))
        return false;

    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("version", preset.version);
    obj->setProperty("name", preset.name);
    obj->setProperty("description", preset.description);
    obj->setProperty("category", preset.category);
    obj->setProperty("author", preset.author);
    obj->setProperty("createdAt", preset.createdAt);
    obj->setProperty("style", preset.style);

    juce::Array<juce::var> tagsArr;
    for (const auto& tag : preset.tags)
        tagsArr.add(tag);
    obj->setProperty("tags", tagsArr);

    if (preset.paramsJson.isNotEmpty())
        obj->setProperty("params", juce::JSON::parse(preset.paramsJson));

    if (preset.styleParamsJson.isNotEmpty())
        obj->setProperty("styleParams", juce::JSON::parse(preset.styleParamsJson));

    outJson = juce::JSON::toString(obj.get(), true);
    return true;
}

// --- Index management ---

void PatternLibrary::rebuildIndex()
{
    std::lock_guard<std::mutex> lock(mutex);

    index.clear();

    auto scanDir = [this](const juce::String& source, const juce::File& dir)
    {
        if (!dir.isDirectory())
            return;

        juce::DirectoryIterator iter(dir, true, "*.json", juce::File::findFiles);
        while (iter.next())
        {
            juce::File file = iter.getFile();
            juce::String relativePath = file.getRelativePathFrom(root);
            juce::String category = file.getParentDirectory().getFileName();
            juce::String filename = file.getFileNameWithoutExtension();

            auto json = juce::JSON::parse(file.loadFileAsString());
            auto* obj = json.getDynamicObject();
            if (obj == nullptr)
                continue;

            PatternIndexEntry entry;
            entry.id = generateId(source, category, filename);
            entry.path = relativePath;
            entry.name = obj->getProperty("name").toString();
            if (entry.name.isEmpty())
                entry.name = filename;
            entry.style = obj->getProperty("style").toString();
            entry.category = category;
            entry.source = source;

            if (obj->hasProperty("tags"))
            {
                if (auto* tagsArr = obj->getProperty("tags").getArray())
                {
                    for (const auto& t : *tagsArr)
                        entry.tags.add(t.toString());
                }
            }

            index.push_back(entry);
        }
    };

    scanDir("factory", root.getChildFile("_factory"));
    scanDir("user", root.getChildFile("user"));

    writeIndex();
}

bool PatternLibrary::isFactoryPattern(const juce::String& id) const
{
    return id.startsWith("factory/");
}

// --- Private helpers ---

juce::String PatternLibrary::sanitizeName(const juce::String& name) const
{
    juce::String result = name.trim();

    static const juce::String invalidChars("<>:\"/\\|?*");
    for (int i = 0; i < invalidChars.length(); ++i)
        result = result.replaceCharacter(invalidChars[i], '-');

    while (result.contains("--"))
        result = result.replace("--", "-");

    if (result.length() > 64)
        result = result.substring(0, 64);

    while (result.isNotEmpty() && result.getLastCharacter() == '-')
        result = result.dropLastCharacters(1);

    while (result.isNotEmpty() && result[0] == '-')
        result = result.substring(1);

    return result;
}

juce::String PatternLibrary::generateId(const juce::String& source, const juce::String& category,
                                         const juce::String& filename) const
{
    return source + "/" + category + "/" + filename;
}

bool PatternLibrary::validatePreset(const PatternPreset& preset, juce::String& outError) const
{
    if (preset.name.isEmpty())
    {
        outError = "Name is required";
        return false;
    }

    if (preset.name.length() > 128)
    {
        outError = "Name too long (max 128 characters)";
        return false;
    }

    if (preset.style.isEmpty())
    {
        outError = "Style is required";
        return false;
    }

    if (preset.version < 1)
    {
        outError = "Invalid version";
        return false;
    }

    if (preset.tags.size() > 20)
    {
        outError = "Too many tags (max 20)";
        return false;
    }

    return true;
}

void PatternLibrary::writeIndex()
{
    juce::DynamicObject::Ptr rootObj = new juce::DynamicObject();
    rootObj->setProperty("version", 1);
    rootObj->setProperty("generatedAt", juce::Time::getCurrentTime().toISO8601(true));

    juce::Array<juce::var> patternsArr;
    for (const auto& entry : index)
    {
        juce::DynamicObject::Ptr entryObj = new juce::DynamicObject();
        entryObj->setProperty("id", entry.id);
        entryObj->setProperty("path", entry.path);
        entryObj->setProperty("name", entry.name);
        entryObj->setProperty("style", entry.style);
        entryObj->setProperty("category", entry.category);
        entryObj->setProperty("source", entry.source);

        juce::Array<juce::var> tagsArr;
        for (const auto& tag : entry.tags)
            tagsArr.add(tag);
        entryObj->setProperty("tags", tagsArr);

        patternsArr.add(entryObj.get());
    }
    rootObj->setProperty("patterns", patternsArr);

    juce::String jsonText = juce::JSON::toString(rootObj.get(), true);
    indexFile().replaceWithText(jsonText);
}

void PatternLibrary::readIndex()
{
    juce::File file = indexFile();
    if (!file.existsAsFile())
    {
        rebuildIndex();
        return;
    }

    auto json = juce::JSON::parse(file.loadFileAsString());
    auto* obj = json.getDynamicObject();
    if (obj == nullptr)
    {
        rebuildIndex();
        return;
    }

    index.clear();

    auto patternsVar = obj->getProperty("patterns");
    auto* patternsArr = patternsVar.getArray();
    if (patternsArr == nullptr)
    {
        rebuildIndex();
        return;
    }

    for (const auto& p : *patternsArr)
    {
        auto* pObj = p.getDynamicObject();
        if (pObj == nullptr)
            continue;

        PatternIndexEntry entry;
        entry.id = pObj->getProperty("id").toString();
        entry.path = pObj->getProperty("path").toString();
        entry.name = pObj->getProperty("name").toString();
        entry.style = pObj->getProperty("style").toString();
        entry.category = pObj->getProperty("category").toString();
        entry.source = pObj->getProperty("source").toString();

        if (pObj->hasProperty("tags"))
        {
            if (auto* tagsArr = pObj->getProperty("tags").getArray())
            {
                for (const auto& t : *tagsArr)
                    entry.tags.add(t.toString());
            }
        }

        index.push_back(entry);
    }
}

} // namespace HDAW
