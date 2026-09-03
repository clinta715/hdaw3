// src/engine/ChainLibrary.cpp
// File storage for named FX-chain presets. Mirrors src/engine/PatternLibrary.cpp:
// root/user/<sanitized>.json, uniquified with -N, load by relative path,
// scan of *.json. JSON carries "version": 1; unknown future fields are
// ignored on load. No exceptions across API boundaries: empty id / empty
// preset signal failure.
#include "engine/ChainLibrary.h"

namespace {
constexpr int kChainPresetVersion = 1;

juce::String sanitizeFileName(const juce::String& name)
{
    juce::String trimmed = name.trim();
    juce::String result;
    result.preallocateBytes((size_t) juce::jmax(16, trimmed.length()));
    for (int i = 0; i < trimmed.length(); ++i)
    {
        juce::juce_wchar c = trimmed[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c == '-' || c == '_';
        result += ok ? juce::String::charToString(c) : juce::String("_");
    }
    if (result.length() > 64)
        result = result.substring(0, 64);
    if (result.isEmpty())
        result = "preset";
    return result;
}

// Forward-tolerant reader: only known fields are read, anything else ignored.
ChainPreset presetFromObject(juce::DynamicObject* obj)
{
    ChainPreset p;
    if (obj == nullptr)
        return p;
    p.name = obj->getProperty("name").toString();
    if (auto* slotsVar = obj->getProperty("slots").getArray())
    {
        for (const auto& sv : *slotsVar)
        {
            auto* sObj = sv.getDynamicObject();
            if (sObj == nullptr)
                continue;
            ChainPreset::Slot s;
            s.fxType = sObj->getProperty("fxType").toString();
            s.bypassed = (bool) sObj->getProperty("bypassed");
            s.name = sObj->getProperty("name").toString();
            if (auto* paramsObj = sObj->getProperty("params").getDynamicObject())
            {
                for (const auto& prop : paramsObj->getProperties())
                    s.params[prop.name.toString()] = (double) prop.value;
            }
            if (auto* plugObj = sObj->getProperty("plugin").getDynamicObject())
            {
                s.plugin.id = plugObj->getProperty("id").toString();
                s.plugin.format = plugObj->getProperty("format").toString();
                s.plugin.path = plugObj->getProperty("path").toString();
                s.plugin.stateBase64 = plugObj->getProperty("stateBase64").toString();
            }
            if (auto* sampObj = sObj->getProperty("sampler").getDynamicObject())
            {
                for (const auto& prop : sampObj->getProperties())
                    s.sampler[prop.name.toString()] = prop.value.toString();
            }
            s.slicePoints = sObj->getProperty("slicePoints").toString();
            s.psyFmMatrix = sObj->getProperty("psyFmMatrix").toString();
            if (sObj->hasProperty("psyFmSweepRate"))
                s.psyFmSweepRate = (double) sObj->getProperty("psyFmSweepRate");
            p.slots.push_back(std::move(s));
        }
    }
    return p;
}

juce::DynamicObject::Ptr presetToObject(const ChainPreset& p)
{
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("version", kChainPresetVersion);
    obj->setProperty("name", p.name);
    juce::Array<juce::var> slotsArr;
    for (const auto& s : p.slots)
    {
        juce::DynamicObject::Ptr sObj = new juce::DynamicObject();
        sObj->setProperty("fxType", s.fxType);
        sObj->setProperty("bypassed", s.bypassed);
        sObj->setProperty("name", s.name);
        juce::DynamicObject::Ptr paramsObj = new juce::DynamicObject();
        for (const auto& kv : s.params)
            paramsObj->setProperty(kv.first, kv.second);
        sObj->setProperty("params", juce::var(paramsObj.get()));
        juce::DynamicObject::Ptr plugObj = new juce::DynamicObject();
        plugObj->setProperty("id", s.plugin.id);
        plugObj->setProperty("format", s.plugin.format);
        plugObj->setProperty("path", s.plugin.path);
        plugObj->setProperty("stateBase64", s.plugin.stateBase64);
        sObj->setProperty("plugin", juce::var(plugObj.get()));
        juce::DynamicObject::Ptr sampObj = new juce::DynamicObject();
        for (const auto& kv : s.sampler)
            sampObj->setProperty(kv.first, kv.second);
        sObj->setProperty("sampler", juce::var(sampObj.get()));
        sObj->setProperty("slicePoints", s.slicePoints);
        sObj->setProperty("psyFmMatrix", s.psyFmMatrix);
        sObj->setProperty("psyFmSweepRate", s.psyFmSweepRate);
        slotsArr.add(juce::var(sObj.get()));
    }
    obj->setProperty("slots", slotsArr);
    return obj;
}

ChainPreset readPresetFile(const juce::File& file, const juce::File& root)
{
    ChainPreset empty;
    if (! file.existsAsFile())
        return empty;
    auto json = juce::JSON::parse(file.loadFileAsString());
    auto* obj = json.getDynamicObject();
    if (obj == nullptr)
        return empty;
    ChainPreset p = presetFromObject(obj);
    p.id = file.getRelativePathFrom(root).replaceCharacter('\\', '/');
    return p;
}
} // namespace

ChainLibrary::ChainLibrary(const juce::File& root)
    : root_(root), userDir_(root.getChildFile("user"))
{
    root_.createDirectory();
    root_.getChildFile("_factory").createDirectory();
    userDir_.createDirectory();
}

ChainLibrary ChainLibrary::userLibrary()
{
    static ChainLibrary lib(
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("HDAW").getChildFile("chains"));
    return lib;
}

juce::String ChainLibrary::savePreset(const ChainPreset& p)
{
    if (p.name.trim().isEmpty())
        return {};

    userDir_.createDirectory();
    juce::String sanitized = sanitizeFileName(p.name);
    juce::File file = userDir_.getChildFile(sanitized + ".json");

    if (file.existsAsFile())
    {
        juce::String baseName = sanitized;
        int counter = 1;
        while (file.existsAsFile() && counter < 100)
        {
            file = userDir_.getChildFile(baseName + "-" + juce::String(counter) + ".json");
            counter++;
        }
        if (file.existsAsFile())
            return {};
    }

    juce::String jsonText = juce::JSON::toString(presetToObject(p).get(), true);
    if (! file.replaceWithText(jsonText))
        return {};

    return file.getRelativePathFrom(root_).replaceCharacter('\\', '/');
}

std::vector<ChainPreset> ChainLibrary::listPresets()
{
    std::vector<ChainPreset> result;
    if (! userDir_.isDirectory())
        return result;

    juce::DirectoryIterator iter(userDir_, true, "*.json", juce::File::findFiles);
    while (iter.next())
    {
        ChainPreset p = readPresetFile(iter.getFile(), root_);
        if (p.id.isEmpty())
            continue;
        result.push_back(std::move(p));
    }
    return result;
}

ChainPreset ChainLibrary::loadPreset(const juce::String& id)
{
    ChainPreset empty;
    if (id.isEmpty())
        return empty;

    juce::File file = root_.getChildFile(id.replaceCharacter('/', juce::File::getSeparatorChar()));
    if (! file.isAChildOf(root_))
        return empty;

    return readPresetFile(file, root_);
}

bool ChainLibrary::deletePreset(const juce::String& id)
{
    if (id.isEmpty())
        return false;

    juce::File file = root_.getChildFile(id.replaceCharacter('/', juce::File::getSeparatorChar()));
    if (! file.isAChildOf(root_))
        return false;
    if (! file.existsAsFile())
        return false;
    return file.deleteFile();
}
