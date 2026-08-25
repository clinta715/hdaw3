#include "AudioEngineCommands.h"
#include "AudioEngine.h"
#include "MainAudioProcessor.h"
#include "../engine/ProjectSerializer.h"
#include "../engine/ProjectBackup.h"
#include "../model/ProjectModel.h"
#include "../common/DebugLog.h"
#include "../common/SettingsKeys.h"
#include "../frontend/FrontendServer.h"
#include "../frontend/FrontendRpc.h"
#include <juce_core/juce_core.h>
#include <QCoreApplication>
#include <QJsonObject>
#include <QSettings>

// ─── ProjectCommands — Undo/redo ──────────────────────────────────

void AudioEngineCommands::undo()  { engine_.getProjectModel().getUndoManager().undo(); }
void AudioEngineCommands::redo()  { engine_.getProjectModel().getUndoManager().redo(); }
bool AudioEngineCommands::canUndo() const { return engine_.getProjectModel().getUndoManager().canUndo(); }
bool AudioEngineCommands::canRedo() const { return engine_.getProjectModel().getUndoManager().canRedo(); }

std::vector<std::string> AudioEngineCommands::getUndoDescriptions() const
{
    std::vector<std::string> result;
    for (const auto& d : engine_.getProjectModel().getUndoManager().getUndoDescriptions())
        result.push_back(d.toStdString());
    return result;
}

std::vector<std::string> AudioEngineCommands::getRedoDescriptions() const
{
    std::vector<std::string> result;
    for (const auto& d : engine_.getProjectModel().getUndoManager().getRedoDescriptions())
        result.push_back(d.toStdString());
    return result;
}

void AudioEngineCommands::beginTransaction(const std::string& name)
{
    engine_.getProjectModel().getUndoManager().beginNewTransaction(juce::String(name));
}

void AudioEngineCommands::endTransaction()
{
    engine_.getProjectModel().getUndoManager().beginNewTransaction({});
}

// ─── ProjectCommands — Project lifecycle ──────────────────────────

void AudioEngineCommands::newProject()
{
    HDAW::ProjectSerializer::createNew(engine_.getProjectModel());

    QSettings s;
    double defaultTempo = s.value(SettingsKeys::kKeyDefaultTempo, 120.0).toDouble();
    int defaultNum = s.value(SettingsKeys::kKeyDefaultTimeSigNum, 4).toInt();
    int defaultDen = s.value(SettingsKeys::kKeyDefaultTimeSigDen, 4).toInt();

    auto& model = engine_.getProjectModel();
    if (defaultTempo != 120.0)
        model.getTree().setProperty(IDs::tempo, defaultTempo, nullptr);
    auto transport = model.getTransportTree();
    if (transport.isValid()) {
        if (defaultNum != 4)
            transport.setProperty(IDs::timeSigNumerator, defaultNum, nullptr);
        if (defaultDen != 4)
            transport.setProperty(IDs::timeSigDenominator, defaultDen, nullptr);
    }

    auto* proc = engine_.getMainProcessor();
    if (proc) proc->rebuildRoutingGraph();
}

bool AudioEngineCommands::saveProject(const std::string& filePath)
{
    if (auto* proc = engine_.getMainProcessor())
        if (proc->getExportManager().isExporting())
            proc->getExportManager().cancelAndJoin();

    auto f = juce::File(filePath);
    bool ok = HDAW::ProjectSerializer::save(engine_.getProjectModel(), f, engine_.getMainProcessor());
    if (ok) {
        QSettings s;
        int maxBackups = s.value(SettingsKeys::kKeyMaxBackups, 10).toInt();
        HDAW::backupProject(f, maxBackups);
    }
    return ok;
}

bool AudioEngineCommands::loadProject(const std::string& filePath)
{
    // Guard against re-entrancy from processEvents() draining the Qt queue
    // mid-load (a second loadProject RPC could arrive while we're blocked).
    static bool loading = false;
    if (loading) return false;
    loading = true;

    auto sendProgress = [](const QString& msg, float pct) {
        if (auto* server = frontend::FrontendServer::instance()) {
            QJsonObject payload{
                { "progress", static_cast<double>(pct) },
                { "message", msg },
            };
            server->broadcastNotification(frontend::notify::LoadProgress, payload);
        }
        QCoreApplication::processEvents();
    };

    sendProgress("Reading project file...", 0.0f);

    bool ok = HDAW::ProjectSerializer::load(engine_.getProjectModel(), juce::File(filePath));
    if (!ok)
    {
        HDAW_LOG("DIAG", "loadProject: load FAILED");
        sendProgress("Load failed", 1.0f);
        loading = false;
        return false;
    }

    // Migration: ensure all existing tracks have trackType property (default 0 = audio)
    auto trackList = engine_.getProjectModel().getTrackListTree();
    if (trackList.isValid())
    {
        for (int t = 0; t < trackList.getNumChildren(); ++t)
        {
            auto track = trackList.getChild(t);
            if (!track.hasProperty(IDs::trackType))
                track.setProperty(IDs::trackType, 0, nullptr);
        }
    }

    sendProgress("Building audio graph...", 0.3f);

    auto* proc = engine_.getMainProcessor();
    HDAW_LOG("DIAG", "loadProject: calling rebuildRoutingGraph after load, trackCount=" + std::to_string(engine_.getProjectModel().getTrackListTree().getNumChildren()));

    if (proc) proc->rebuildRoutingGraph(true);

    sendProgress("Done", 1.0f);
    loading = false;
    return true;
}

// ─── ProjectCommands — Scale ──────────────────────────────────────

void AudioEngineCommands::setScaleRoot(int root) { engine_.getProjectModel().setScaleRoot(root); }
void AudioEngineCommands::setScaleMode(int mode) { engine_.getProjectModel().setScaleMode(mode); }

// ─── AudioGraphCommands ───────────────────────────────────────────

void AudioEngineCommands::rebuildRoutingGraph(bool loading)
{
    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildRoutingGraph(loading);
}

void AudioEngineCommands::rebuildTrackFX(int trackIndex)
{
    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildTrackFX(trackIndex);
}

void AudioEngineCommands::rebuildAutomationCache(int trackIndex)
{
    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildAutomationCache(trackIndex);
}

void AudioEngineCommands::rebuildModulation(int trackIndex)
{
    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildModulation(trackIndex);
}

void AudioEngineCommands::toggleFXEditor(int trackIndex, int slotIndex)
{
    if (auto* proc = engine_.getMainProcessor())
        proc->toggleFXEditor(trackIndex, slotIndex);
}

void AudioEngineCommands::switchClipTake(int clipId)
{
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid() || trackIdx < 0) return;

    auto& um = engine_.getProjectModel().getUndoManager();
    auto takeList = clip.getChildWithName(IDs::TAKE_LIST);
    if (!takeList.isValid() || takeList.getNumChildren() < 2) return;

    int activeIdx = static_cast<int>(clip.getProperty(IDs::activeTake, 0));
    int nextIdx = (activeIdx + 1) % takeList.getNumChildren();
    juce::String newSource = takeList.getChild(nextIdx).getProperty(IDs::sourceFile).toString();

    clip.setProperty(IDs::activeTake, nextIdx, &um);

    if (auto* proc = engine_.getMainProcessor())
    {
        if (auto* rm = proc->getRoutingManager())
        {
            auto trackList = engine_.getProjectModel().getTrackListTree();
            auto clipList = trackList.getChild(trackIdx).getChildWithName(IDs::CLIP_LIST);
            int clipIdx = -1;
            for (int c = 0; c < clipList.getNumChildren(); ++c)
            {
                if (static_cast<int>(clipList.getChild(c).getProperty(IDs::clipID, 0)) == clipId)
                {
                    clipIdx = c;
                    break;
                }
            }
            if (clipIdx >= 0)
                rm->switchClipTake(trackIdx, clipIdx, newSource);
        }
    }
}

void AudioEngineCommands::switchClipTakeToIndex(int clipId, int takeIndex)
{
    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid() || trackIdx < 0) return;

    auto takeList = clip.getChildWithName(IDs::TAKE_LIST);
    if (!takeList.isValid()) return;
    if (takeIndex < 0 || takeIndex >= takeList.getNumChildren()) return;

    auto& um = engine_.getProjectModel().getUndoManager();
    juce::String newSource = takeList.getChild(takeIndex).getProperty(IDs::sourceFile).toString();
    clip.setProperty(IDs::activeTake, takeIndex, &um);

    if (auto* proc = engine_.getMainProcessor())
    {
        if (auto* rm = proc->getRoutingManager())
        {
            auto trackList = engine_.getProjectModel().getTrackListTree();
            auto clipList = trackList.getChild(trackIdx).getChildWithName(IDs::CLIP_LIST);
            int clipIdx = -1;
            for (int c = 0; c < clipList.getNumChildren(); ++c)
            {
                if (static_cast<int>(clipList.getChild(c).getProperty(IDs::clipID, 0)) == clipId)
                {
                    clipIdx = c;
                    break;
                }
            }
            if (clipIdx >= 0)
                rm->switchClipTake(trackIdx, clipIdx, newSource);
        }
    }
}

// ─── Missing source-file relinking ─────────────────────────────────

static const juce::StringArray audioExtensions{".wav", ".aiff", ".aif", ".mp3", ".flac", ".ogg"};

static juce::String searchForFile(const juce::String& missingPath, const juce::File& dir)
{
    juce::File missingFile(missingPath);
    auto name = missingFile.getFileName();
    auto baseName = missingFile.getFileNameWithoutExtension();
    auto ext = missingFile.getFileExtension();

    juce::Array<juce::File> results;
    dir.findChildFiles(results, juce::File::findFiles, true, name);
    if (results.size() > 0)
        return results[0].getFullPathName();

    for (const auto& tryExt : audioExtensions)
    {
        if (tryExt.toLowerCase() == ext.toLowerCase())
            continue;
        auto tryName = baseName + tryExt;
        dir.findChildFiles(results, juce::File::findFiles, true, tryName);
        if (results.size() > 0)
            return results[0].getFullPathName();
    }

    return {};
}

std::string AudioEngineCommands::findMissingClipSourceFile(int clipId, const std::string& searchDir)
{
    juce::File dir(searchDir.empty() ? "." : juce::String(searchDir));
    if (!dir.isDirectory())
        return {};

    int trackIdx = -1;
    auto clip = findClipById(clipId, trackIdx);
    if (!clip.isValid())
        return {};

    auto sourceFile = clip.getProperty(IDs::sourceFile).toString();
    if (sourceFile.isEmpty())
        return {};

    juce::File current(sourceFile);
    if (current.existsAsFile())
        return sourceFile.toStdString();

    auto found = searchForFile(sourceFile, dir);
    if (found.isNotEmpty())
    {
        auto& um = engine_.getProjectModel().getUndoManager();
        um.beginNewTransaction("Find missing clip source file");
        clip.setProperty(IDs::sourceFile, found, &um);
        if (auto* proc = engine_.getMainProcessor())
            proc->rebuildRoutingGraph();
        return found.toStdString();
    }
    return {};
}

ProjectCommands::RelinkResult AudioEngineCommands::relinkAllMissingFiles(const std::string& searchDir)
{
    RelinkResult result{0, 0};
    juce::File dir(searchDir.empty() ? "." : juce::String(searchDir));
    if (!dir.isDirectory())
        return result;

    auto& um = engine_.getProjectModel().getUndoManager();
    um.beginNewTransaction("Relink missing files");

    auto trackList = engine_.getProjectModel().getTrackListTree();
    bool anyRelinked = false;
    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid()) continue;
        for (int c = 0; c < clipList.getNumChildren(); ++c)
        {
            auto clip = clipList.getChild(c);
            if (clip.getProperty(IDs::clipType).toString() != "audio")
                continue;
            auto sourceFile = clip.getProperty(IDs::sourceFile).toString();
            if (sourceFile.isEmpty())
                continue;
            juce::File current(sourceFile);
            if (current.existsAsFile())
                continue;

            result.totalMissing++;
            auto found = searchForFile(sourceFile, dir);
            if (found.isNotEmpty())
            {
                clip.setProperty(IDs::sourceFile, found, &um);
                result.found++;
                anyRelinked = true;
            }
        }
    }

    if (anyRelinked)
        if (auto* proc = engine_.getMainProcessor())
            proc->rebuildRoutingGraph();

    return result;
}
