// src/engine/FileLibraryManager.cpp
#include "FileLibraryManager.h"
#include <juce_core/juce_core.h>
#include <fstream>
#include <sstream>

namespace HDAW {

FileLibraryManager::FileLibraryManager() {
    auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                       .getChildFile("HDAW");
    appData.createDirectory();
    librariesDir = appData.getChildFile("libraries");
    librariesDir.createDirectory();
    registryFile = librariesDir.getChildFile("registry.json");
    loadRegistry();
}

FileLibraryManager::~FileLibraryManager() {
    threadPool.removeAllJobs(true, 1000);
}

void FileLibraryManager::initialize() {
    for (const auto& lib : libraries) {
        if (lib.autoScan) scanLibrary(lib.id);
    }
}

juce::StringArray FileLibraryManager::getLibraryIds() const {
    std::lock_guard<std::mutex> lock(mutex);
    juce::StringArray ids;
    for (const auto& lib : libraries) ids.add(lib.id);
    return ids;
}

LibraryInfo FileLibraryManager::getLibraryInfo(const juce::String& id) const {
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& lib : libraries) {
        if (lib.id == id) return lib;
    }
    return {};
}

juce::String FileLibraryManager::addLibrary(const juce::String& name,
                                             const juce::String& path,
                                             const juce::String& type) {
    juce::String id = juce::Uuid().toString().removeCharacters("-{}").substring(0, 12);
    LibraryInfo info;
    info.id = id;
    info.name = name;
    info.path = path;
    info.type = type;
    info.autoScan = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        libraries.push_back(info);
    }
    saveRegistry();
    return id;
}

void FileLibraryManager::removeLibrary(const juce::String& id) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        libraries.erase(std::remove_if(libraries.begin(), libraries.end(),
            [&](const LibraryInfo& l) { return l.id == id; }), libraries.end());
        entries.erase(id);
        loadedLibraries.erase(id);
    }
    auto entryFile = librariesDir.getChildFile(id + ".json");
    if (entryFile.existsAsFile()) entryFile.deleteFile();
    saveRegistry();
}

void FileLibraryManager::setAutoScan(const juce::String& id, bool enabled) {
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& lib : libraries) {
        if (lib.id == id) {
            lib.autoScan = enabled;
            break;
        }
    }
    saveRegistry();
}

bool FileLibraryManager::isScanning() const {
    return scanning.load();
}

void FileLibraryManager::setScanProgressCallback(ScanProgressCallback cb) {
    progressCallback = std::move(cb);
}

void FileLibraryManager::setScanCompleteCallback(ScanCompleteCallback cb) {
    completeCallback = std::move(cb);
}

void FileLibraryManager::loadRegistry() {
    if (!registryFile.existsAsFile()) return;
    auto content = registryFile.loadFileAsString();
    if (content.isEmpty()) return;

    auto json = juce::JSON::parse(content);
    auto* obj = json.getDynamicObject();
    if (!obj) return;

    auto& libs = obj->getProperty("libraries");
    auto* libsArray = libs.getArray();
    if (!libsArray) return;

    for (int i = 0; i < libsArray->size(); ++i) {
        auto entry = (*libsArray)[i];
        auto* eObj = entry.getDynamicObject();
        if (!eObj) continue;
        LibraryInfo info;
        info.id = eObj->getProperty("id").toString();
        info.name = eObj->getProperty("name").toString();
        info.path = eObj->getProperty("path").toString();
        info.type = eObj->getProperty("type").toString();
        info.lastScan = eObj->getProperty("lastScan").toString();
        info.fileCount = (int)eObj->getProperty("fileCount");
        info.autoScan = (bool)eObj->getProperty("autoScan");
        if (info.id.isNotEmpty()) libraries.push_back(info);
    }
}

void FileLibraryManager::saveRegistry() {
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    juce::Array<juce::var> libs;
    for (const auto& lib : libraries) {
        juce::DynamicObject::Ptr obj = new juce::DynamicObject();
        obj->setProperty("id", lib.id);
        obj->setProperty("name", lib.name);
        obj->setProperty("path", lib.path);
        obj->setProperty("type", lib.type);
        obj->setProperty("lastScan", lib.lastScan);
        obj->setProperty("fileCount", lib.fileCount);
        obj->setProperty("autoScan", lib.autoScan);
        libs.add(juce::var(obj.get()));
    }
    root->setProperty("libraries", libs);
    registryFile.getParentDirectory().createDirectory();
    registryFile.replaceWithText(juce::JSON::toString(juce::var(root.get())));
}

void FileLibraryManager::scanLibrary(const juce::String&) {}
void FileLibraryManager::scanAll() {}
void FileLibraryManager::loadLibraryEntries(const juce::String&) {}
void FileLibraryManager::saveLibraryEntries(const juce::String&) {}
void FileLibraryManager::scanDirectory(const juce::String&, const juce::File&) {}
LibraryEntry FileLibraryManager::extractMidiMetadata(const juce::File&) { return {}; }
LibraryEntry FileLibraryManager::extractAudioMetadata(const juce::File&) { return {}; }
juce::String FileLibraryManager::detectKey(const std::vector<int>&) const { return {}; }

std::vector<LibraryEntry> FileLibraryManager::search(const juce::String&, const juce::String&,
    const juce::String&, double, double, double, double, const juce::String&, int, int) const { return {}; }

LibraryEntry FileLibraryManager::getEntry(const juce::String&, const juce::String&) const { return {}; }

} // namespace HDAW
