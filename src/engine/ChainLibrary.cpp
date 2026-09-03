// src/engine/ChainLibrary.cpp
// File storage for named FX-chain presets. Mirrors src/engine/PatternLibrary.cpp:
// root/user/<sanitized>.json, uniquified with -N, load by relative path,
// scan of *.json. JSON carries "version"; unknown future fields are
// ignored on load (best-effort). No exceptions across API boundaries:
// empty id / empty preset signal failure. All public methods perform
// blocking file IO: message thread (or background/test thread) only,
// never the audio thread.
#include "ChainLibrary.h"
#include "common/DebugLog.h"
#include <algorithm>

namespace HDAW {

namespace {

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
    if (obj->hasProperty("version"))
        p.version = (int) obj->getProperty("version");
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
    obj->setProperty("version", p.version);
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

bool isFactoryId(const juce::String& id)
{
    return id.startsWith("_factory/") || id.startsWith("_factory\\");
}

// --- Built-in factory roster (psytrance per-role chains, internal FX only) ---
//
// Param indices/units mirror TrackFXSlot::getParamDefsForType (real units,
// keyed "param_<index>"): saturator 0=Drive dB 0..40, 1=Type 0..3,
// 3=Mix 0..1, 4=Output dB -24..24; eq 0=Frequency 20..20000, 1=Q 0.1..10,
// 2=Gain -24..24; compressor 0=Threshold -80..0, 1=Ratio 1..40,
// 2=Attack 0.1..100, 3=Release 1..2000; reverb 0=Room Size 0..1,
// 1=Damping 0..1, 2=Wet 0..1, 3=Dry 0..1, 4=Width 0..1; chorus 0=Rate,
// 1=Depth, 2=Centre Delay, 3=Feedback, 4=Mix; filter 0=Cutoff 20..20000,
// 1=Mode 0..2 (0=lowpass), 2=Resonance 0.1..10; delay 0=Delay Time s,
// 1=Feedback, 2=Mix, 3=SyncToTempo (1 = time derived from Division + BPM),
// 4=Division enum (1=1/16, 4=dotted-1/8); phaser 0=Rate, 1=Depth,
// 2=Centre Frequency, 3=Feedback, 4=Mix.

struct FactorySlotDef {
    const char* fxType;
    const char* name;
    std::vector<std::pair<const char*, double>> params;
};

struct FactoryChainDef {
    const char* name;
    std::vector<FactorySlotDef> slots;
};

const std::vector<FactoryChainDef>& factoryChainDefs()
{
    static const std::vector<FactoryChainDef> defs = {
        // Mild SoftTanh drive for punch, eq tames sub boom, eq lifts beater click.
        { "Kick Punch", {
            { "saturator", "Kick Drive",
              { { "param_0", 14.0 }, { "param_1", 0.0 }, { "param_3", 0.6 }, { "param_4", -2.0 } } },
            { "eq", "Sub Control",
              { { "param_0", 55.0 }, { "param_1", 1.2 }, { "param_2", -3.0 } } },
            { "eq", "Click Boost",
              { { "param_0", 4000.0 }, { "param_1", 1.0 }, { "param_2", 3.0 } } },
        }},
        // Gentle glue: body eq, slow-attack compressor, whisper of saturation.
        { "Bass Glue", {
            { "eq", "Bass Body",
              { { "param_0", 120.0 }, { "param_1", 0.9 }, { "param_2", 1.5 } } },
            { "compressor", "Glue",
              { { "param_0", -18.0 }, { "param_1", 3.0 }, { "param_2", 12.0 }, { "param_3", 120.0 } } },
            { "saturator", "Warmth",
              { { "param_0", 8.0 }, { "param_1", 0.0 }, { "param_3", 0.5 }, { "param_4", -1.0 } } },
        }},
        // Air lift plus a small, bright room that stays out of the way.
        { "Hat Air", {
            { "eq", "Air",
              { { "param_0", 9000.0 }, { "param_1", 0.7 }, { "param_2", 3.0 } } },
            { "reverb", "Short Room",
              { { "param_0", 0.25 }, { "param_1", 0.3 }, { "param_2", 0.30 }, { "param_3", 0.9 }, { "param_4", 1.0 } } },
        }},
        // Wide slow chorus into a large lush hall.
        { "Pad Shimmer", {
            { "chorus", "Shimmer",
              { { "param_0", 0.6 }, { "param_1", 0.45 }, { "param_2", 12.0 }, { "param_3", 0.15 }, { "param_4", 0.5 } } },
            { "reverb", "Lush Hall",
              { { "param_0", 0.92 }, { "param_1", 0.25 }, { "param_2", 0.42 }, { "param_3", 0.7 }, { "param_4", 1.0 } } },
        }},
        // Resonant lowpass squelch; delay tempo-syncs to a dotted 1/8.
        { "Acid Lead", {
            { "filter", "Acid Squelch",
              { { "param_0", 1200.0 }, { "param_1", 0.0 }, { "param_2", 4.5 } } },
            { "delay", "Dotted 8th",
              { { "param_0", 0.321 }, { "param_1", 0.45 }, { "param_2", 0.35 }, { "param_3", 1.0 }, { "param_4", 4.0 } } },
        }},
        // Subtle widening chorus plus a quiet synced 1/16 slap.
        { "Arp Width", {
            { "chorus", "Width",
              { { "param_0", 1.2 }, { "param_1", 0.25 }, { "param_2", 8.0 }, { "param_3", 0.0 }, { "param_4", 0.3 } } },
            { "delay", "16th Slap",
              { { "param_0", 0.107 }, { "param_1", 0.3 }, { "param_2", 0.22 }, { "param_3", 1.0 }, { "param_4", 1.0 } } },
        }},
        // Narrow mid snip keeps stabs out of the lead's way; phaser barely moves.
        { "Stab Snip", {
            { "eq", "Mid Snip",
              { { "param_0", 1800.0 }, { "param_1", 3.5 }, { "param_2", -2.5 } } },
            { "phaser", "Subtle Move",
              { { "param_0", 0.4 }, { "param_1", 0.3 }, { "param_2", 1400.0 }, { "param_3", 0.2 }, { "param_4", 0.3 } } },
        }},
        // Closed resonant filter start (sweep it with an automation lane)
        // into a long bright tail.
        { "Riser Sweep", {
            { "filter", "Sweep Start",
              { { "param_0", 400.0 }, { "param_1", 0.0 }, { "param_2", 6.0 } } },
            { "reverb", "Long Tail",
              { { "param_0", 0.95 }, { "param_1", 0.2 }, { "param_2", 0.5 }, { "param_3", 0.6 }, { "param_4", 1.0 } } },
        }},
    };
    return defs;
}

// Hand-built minimal slot JSON for factory files: only fxType/bypassed/name/
// params. presetFromObject is forward-tolerant — plugin/sampler/slicePoints/
// psyFm fields are simply absent.
juce::DynamicObject::Ptr factorySlotObject(const FactorySlotDef& sd)
{
    juce::DynamicObject::Ptr sObj = new juce::DynamicObject();
    sObj->setProperty("fxType", sd.fxType);
    sObj->setProperty("bypassed", false);
    sObj->setProperty("name", sd.name);
    juce::DynamicObject::Ptr paramsObj = new juce::DynamicObject();
    for (const auto& kv : sd.params)
        paramsObj->setProperty(kv.first, kv.second);
    sObj->setProperty("params", juce::var(paramsObj.get()));
    return sObj;
}

ChainPreset readPresetFile(const juce::File& file, const juce::File& root)
{
    ChainPreset empty;
    if (! file.existsAsFile())
        return empty;
    auto json = juce::JSON::parse(file.loadFileAsString());
    auto* obj = json.getDynamicObject();
    if (obj == nullptr)
    {
        HDAW_LOG("ChainLibrary",
                 "skipping unparseable preset file: " + file.getFullPathName());
        return empty;
    }
    ChainPreset p = presetFromObject(obj);
    p.id = file.getRelativePathFrom(root).replaceCharacter('\\', '/');
    p.isFactory = isFactoryId(p.id);
    return p;
}

} // namespace

ChainLibrary::ChainLibrary(const juce::File& root)
    : root_(root), userDir_(root.getChildFile("user"))
{
    root_.createDirectory();
    root_.getChildFile("_factory").createDirectory();
    userDir_.createDirectory();
    seedFactoryPresetsIfMissing();
}

void ChainLibrary::seedFactoryPresetsIfMissing()
{
    juce::File factoryDir = root_.getChildFile("_factory");
    for (const auto& def : factoryChainDefs())
    {
        juce::File file = factoryDir.getChildFile(sanitizeFileName(def.name) + ".json");
        // Never overwrite: user edits of a factory file must survive
        // upgrades (seeding is create-if-missing only).
        if (file.existsAsFile())
            continue;
        juce::DynamicObject::Ptr obj = new juce::DynamicObject();
        obj->setProperty("version", 1);
        obj->setProperty("name", def.name);
        juce::Array<juce::var> slotsArr;
        for (const auto& sd : def.slots)
            slotsArr.add(juce::var(factorySlotObject(sd).get()));
        obj->setProperty("slots", slotsArr);
        if (! file.replaceWithText(juce::JSON::toString(obj.get(), true)))
            HDAW_LOG("ChainLibrary",
                     "failed to seed factory preset: " + file.getFullPathName());
    }
}

const ChainLibrary& ChainLibrary::userLibrary()
{
    static ChainLibrary lib(
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("HDAW").getChildFile("chains"));
    return lib;
}

juce::String ChainLibrary::savePreset(const ChainPreset& p) const
{
    if (p.name.trim().isEmpty())
        return {};

    std::lock_guard<std::mutex> lock(mutex_);

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

std::vector<ChainPreset> ChainLibrary::listPresets() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<ChainPreset> result;
    auto scanDir = [this, &result](const juce::File& dir, bool factory)
    {
        if (! dir.isDirectory())
            return;
        juce::DirectoryIterator iter(dir, true, "*.json", juce::File::findFiles);
        while (iter.next())
        {
            ChainPreset p = readPresetFile(iter.getFile(), root_);
            if (p.id.isEmpty())
                continue;
            if (p.name.isEmpty())
            {
                HDAW_LOG("ChainLibrary",
                         "skipping preset with empty name: " + iter.getFile().getFullPathName());
                continue;
            }
            p.isFactory = factory;
            result.push_back(std::move(p));
        }
    };
    // Mirror PatternLibrary.cpp:428: factory tree first, then user. The sort
    // makes the order deterministic regardless of DirectoryIterator order:
    // factory group first, then user, each alphabetical by id.
    scanDir(root_.getChildFile("_factory"), true);
    scanDir(userDir_, false);

    std::sort(result.begin(), result.end(),
              [](const ChainPreset& a, const ChainPreset& b)
              {
                  if (a.isFactory != b.isFactory)
                      return a.isFactory;
                  return a.id < b.id;
              });
    return result;
}

ChainPreset ChainLibrary::loadPreset(const juce::String& id) const
{
    ChainPreset empty;
    if (id.isEmpty())
        return empty;

    std::lock_guard<std::mutex> lock(mutex_);

    juce::File file = root_.getChildFile(id.replaceCharacter('/', juce::File::getSeparatorChar()));
    if (! file.isAChildOf(root_))
        return empty;

    return readPresetFile(file, root_);
}

bool ChainLibrary::deletePreset(const juce::String& id) const
{
    if (id.isEmpty())
        return false;
    if (isFactoryId(id))
        return false;

    std::lock_guard<std::mutex> lock(mutex_);

    juce::File file = root_.getChildFile(id.replaceCharacter('/', juce::File::getSeparatorChar()));
    if (! file.isAChildOf(root_))
        return false;
    if (! file.existsAsFile())
        return false;
    return file.deleteFile();
}

} // namespace HDAW
