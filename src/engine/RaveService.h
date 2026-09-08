#pragma once

#include <juce_core/juce_core.h>

#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <vector>

namespace HDAW {

struct RaveModelInfo
{
    juce::String name;
    juce::String path;
    juce::String extension;
    int64_t sizeBytes = 0;
};

struct RaveTransformRequest
{
    juce::String inputPath;
    juce::String modelPath;
    juce::String outputPath;
    juce::String pythonPath;
    juce::String scriptPath;
    double temperature = 1.0;
    int seed = 0;
};

struct RaveTransformResult
{
    bool ok = false;
    juce::String outputPath;
    juce::String error;
    juce::String stdoutText;
    juce::String stderrText;
    int exitCode = -1;
};

struct RaveProbeRequest
{
    juce::String modelPath;
    juce::String pythonPath;
    juce::String scriptPath;
};

struct RaveProbeResult
{
    bool ok = false;
    QJsonObject payload;
    juce::String error;
    juce::String stdoutText;
    juce::String stderrText;
    int exitCode = -1;
};

struct RaveTrainingRequest
{
    juce::String datasetPath;
    juce::String outputModelPath;
    juce::String name;
    juce::String pythonPath;
    juce::String scriptPath;
    int epochs = 10;
    int batchSize = 8;
    int sampleRate = 44100;
};

struct RaveTrainingResult
{
    bool ok = false;
    juce::String outputModelPath;
    juce::String error;
    juce::String stdoutText;
    juce::String stderrText;
    int exitCode = -1;
};

class RaveService
{
public:
    std::vector<RaveModelInfo> listModels(const juce::File& directory = {}) const;
    RaveProbeResult probeModel(const RaveProbeRequest& request) const;
    RaveTransformResult transformFile(const RaveTransformRequest& request) const;

    // RAVE #5 persisted-config resolution. Order:
    // explicit request field > QSettings (rave/*) > env (HDAW_RAVE_SCRIPT /
    // HDAW_RAVE_PYTHON / HDAW_RAVE_TIMEOUT_MS) > hardcoded default.
    // Script path additionally has zero-config fallbacks after env (option c):
    // dev tree <cwd>/tools/rave/rave_transform.py, then packaged
    // <exe_dir>/../rave/rave_transform.py (resources/rave/ next to
    // resources/engine/); first existing file wins, none -> empty (caller
    // reports "script not configured"). Python resolution is unchanged.
    // Shared with RaveJobManager so the sync and async sidecar paths always
    // agree. QSettings reads need the QCoreApplication org/app names set
    // (every HDAW entry point sets them); when unset the QSettings layer is
    // skipped and resolution falls through to env/defaults (legacy behavior).
    static juce::String resolveScriptPath(const juce::String& explicitValue);
    static juce::String resolveTrainScriptPath(const juce::String& explicitValue);
    static juce::String resolvePythonPath(const juce::String& explicitValue);
    static int resolveTimeoutMs();

    // Persisted-config JSON shared by the settings.getRaveConfig RPC and the
    // rave_get_config / rave_set_config MCP tools:
    // { modelDirs: string[], defaultModel, pythonPath, scriptPath, timeoutMs }
    // (empty strings/arrays when unset; timeoutMs falls back to
    // SettingsKeys::kDefaultRaveTimeoutMs).
    static QJsonObject persistedConfigJson();

    // Partial update: every field optional. timeoutMs, when present, must be
    // > 0 or returns false with errorOut set (mapped to -32602 by the RPC).
    static bool savePersistedConfig(const QJsonObject& patch, QString* errorOut);

private:
    static bool isSupportedModelExtension(const juce::String& extension);
    static std::vector<juce::File> defaultModelDirectories();
};

} // namespace HDAW
