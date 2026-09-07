#include "RaveService.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QString>
#include <QStringList>

#include "../common/SettingsKeys.h"

#include <algorithm>
#include <cstdlib>

namespace HDAW {
namespace {

juce::String envString(const char* name)
{
    if (const char* value = std::getenv(name))
        return juce::String(value);
    return {};
}

juce::String canonicalPathOf(const juce::File& file)
{
    return file.getFullPathName();
}

// QSettings needs the org/app names set (every HDAW entry point sets them);
// constructing one without them only warns and returns defaults, so skip the
// QSettings layer entirely in that case (legacy env/default behavior).
bool settingsAvailable()
{
    return !QCoreApplication::organizationName().isEmpty()
        && !QCoreApplication::applicationName().isEmpty();
}

juce::String settingsString(const char* key)
{
    if (!settingsAvailable())
        return {};
    QSettings s;
    return juce::String(s.value(QString::fromUtf8(key)).toString().toUtf8().constData());
}

} // namespace

bool RaveService::isSupportedModelExtension(const juce::String& extension)
{
    const auto e = extension.toLowerCase();
    return e == ".ts" || e == ".pt" || e == ".pth" || e == ".rave" || e == ".onnx";
}

std::vector<juce::File> RaveService::defaultModelDirectories()
{
    std::vector<juce::File> dirs;
    // De-duplicated by full path (case-insensitive: Windows paths).
    auto addDir = [&dirs](const juce::File& dir) {
        const auto path = dir.getFullPathName();
        for (const auto& existing : dirs)
            if (existing.getFullPathName().equalsIgnoreCase(path))
                return;
        dirs.push_back(dir);
    };

    // RAVE #5: persisted model dirs first, then the existing defaults.
    if (settingsAvailable())
    {
        QSettings s;
        const auto modelDirs = s.value(SettingsKeys::kKeyRaveModelDirs).toStringList();
        for (const QString& d : modelDirs)
        {
            const juce::File dir(juce::String(d.toUtf8().constData()));
            if (dir.isDirectory())
                addDir(dir);
        }
    }

    auto repoRave = juce::File::getCurrentWorkingDirectory().getChildFile("rave");
    if (repoRave.isDirectory())
        addDir(repoRave);

    auto appData = envString("APPDATA");
    if (appData.isNotEmpty())
    {
        auto appDataRave = juce::File(appData).getChildFile("ACIDS").getChildFile("RAVE");
        if (appDataRave.isDirectory())
            addDir(appDataRave);
    }

    return dirs;
}

std::vector<RaveModelInfo> RaveService::listModels(const juce::File& directory) const
{
    std::vector<juce::File> dirs;
    if (directory != juce::File{})
    {
        if (directory.isDirectory())
            dirs.push_back(directory);
    }
    else
    {
        dirs = defaultModelDirectories();
    }

    std::vector<RaveModelInfo> out;
    for (const auto& dir : dirs)
    {
        juce::Array<juce::File> files;
        dir.findChildFiles(files, juce::File::findFiles, false);
        for (const auto& file : files)
        {
            const auto ext = file.getFileExtension().toLowerCase();
            if (!isSupportedModelExtension(ext))
                continue;

            RaveModelInfo info;
            info.name = file.getFileNameWithoutExtension();
            info.path = canonicalPathOf(file);
            info.extension = ext;
            info.sizeBytes = static_cast<int64_t>(file.getSize());
            out.push_back(std::move(info));
        }
    }

    std::sort(out.begin(), out.end(), [](const RaveModelInfo& a, const RaveModelInfo& b) {
        const int byName = a.name.compareIgnoreCase(b.name);
        if (byName != 0) return byName < 0;
        return a.path.compareIgnoreCase(b.path) < 0;
    });
    return out;
}

juce::String RaveService::resolveScriptPath(const juce::String& explicitValue)
{
    if (explicitValue.isNotEmpty())
        return explicitValue;
    const auto fromSettings = settingsString(SettingsKeys::kKeyRaveScriptPath);
    if (fromSettings.isNotEmpty())
        return fromSettings;
    const auto fromEnv = envString("HDAW_RAVE_SCRIPT");
    if (fromEnv.isNotEmpty())
        return fromEnv;

    // Script-path option (c): zero-config fallbacks so fresh installs resolve
    // the sidecar without manual setup. First candidate that existsAsFile wins:
    //   1. dev tree:        <cwd>/tools/rave/rave_transform.py
    //   2. packaged layout: <exe_dir>/../rave/rave_transform.py
    //      (the engine exe ships in resources/engine/, the sidecar in
    //       resources/rave/ via electron-builder extraResources)
    // None -> empty result; callers report the unchanged "script not
    // configured (set scriptPath or HDAW_RAVE_SCRIPT)" error.
    const auto devCandidate = juce::File::getCurrentWorkingDirectory()
                                  .getChildFile("tools")
                                  .getChildFile("rave")
                                  .getChildFile("rave_transform.py");
    if (devCandidate.existsAsFile())
        return devCandidate.getFullPathName();

    const auto exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                            .getParentDirectory();
    const auto packagedCandidate = exeDir.getParentDirectory()
                                       .getChildFile("rave")
                                       .getChildFile("rave_transform.py");
    if (packagedCandidate.existsAsFile())
        return packagedCandidate.getFullPathName();

    return {};
}

juce::String RaveService::resolvePythonPath(const juce::String& explicitValue)
{
    if (explicitValue.isNotEmpty())
        return explicitValue;
    const auto fromSettings = settingsString(SettingsKeys::kKeyRavePythonPath);
    if (fromSettings.isNotEmpty())
        return fromSettings;
    const auto fromEnv = envString("HDAW_RAVE_PYTHON");
    if (fromEnv.isNotEmpty())
        return fromEnv;
    return juce::String("python");
}

int RaveService::resolveTimeoutMs()
{
    if (settingsAvailable())
    {
        QSettings s;
        const int fromSettings = s.value(SettingsKeys::kKeyRaveTimeoutMs).toInt();
        if (fromSettings > 0)
            return fromSettings;
    }
    if (const char* raw = std::getenv("HDAW_RAVE_TIMEOUT_MS"))
    {
        const int parsed = QString::fromUtf8(raw).toInt();
        if (parsed > 0)
            return parsed;
    }
    return SettingsKeys::kDefaultRaveTimeoutMs;
}

QJsonObject RaveService::persistedConfigJson()
{
    QJsonArray dirs;
    QString defaultModel;
    QString pythonPath;
    QString scriptPath;
    int timeoutMs = SettingsKeys::kDefaultRaveTimeoutMs;
    if (settingsAvailable())
    {
        QSettings s;
        for (const QString& d : s.value(SettingsKeys::kKeyRaveModelDirs).toStringList())
            dirs.append(d);
        defaultModel = s.value(SettingsKeys::kKeyRaveDefaultModel).toString();
        pythonPath = s.value(SettingsKeys::kKeyRavePythonPath).toString();
        scriptPath = s.value(SettingsKeys::kKeyRaveScriptPath).toString();
        const int stored = s.value(SettingsKeys::kKeyRaveTimeoutMs).toInt();
        if (stored > 0)
            timeoutMs = stored;
    }
    return QJsonObject{
        { "modelDirs", dirs },
        { "defaultModel", defaultModel },
        { "pythonPath", pythonPath },
        { "scriptPath", scriptPath },
        { "timeoutMs", timeoutMs },
    };
}

bool RaveService::savePersistedConfig(const QJsonObject& patch, QString* errorOut)
{
    // Validate BEFORE any write so a bad patch never half-applies.
    if (patch.contains("timeoutMs"))
    {
        const auto tv = patch.value("timeoutMs");
        if (!tv.isDouble() || tv.toInt(0) <= 0)
        {
            if (errorOut != nullptr)
                *errorOut = QStringLiteral("timeoutMs must be > 0");
            return false;
        }
    }
    if (!settingsAvailable())
    {
        if (errorOut != nullptr)
            *errorOut = QStringLiteral("QSettings unavailable (organization/application name not set)");
        return false;
    }
    QSettings s;
    if (patch.contains("modelDirs"))
    {
        QStringList list;
        for (const auto& v : patch.value("modelDirs").toArray())
            list << v.toString();
        s.setValue(SettingsKeys::kKeyRaveModelDirs, list);
    }
    if (patch.contains("defaultModel"))
        s.setValue(SettingsKeys::kKeyRaveDefaultModel, patch.value("defaultModel").toString());
    if (patch.contains("pythonPath"))
        s.setValue(SettingsKeys::kKeyRavePythonPath, patch.value("pythonPath").toString());
    if (patch.contains("scriptPath"))
        s.setValue(SettingsKeys::kKeyRaveScriptPath, patch.value("scriptPath").toString());
    if (patch.contains("timeoutMs"))
        s.setValue(SettingsKeys::kKeyRaveTimeoutMs, patch.value("timeoutMs").toInt());
    return true;
}

RaveTransformResult RaveService::transformFile(const RaveTransformRequest& request) const
{
    RaveTransformResult result;
    result.outputPath = request.outputPath;

    const juce::File input(request.inputPath);
    if (request.inputPath.isEmpty() || !input.existsAsFile())
    {
        result.error = "input file not found: " + request.inputPath;
        return result;
    }

    const juce::File model(request.modelPath);
    if (request.modelPath.isEmpty() || !model.existsAsFile())
    {
        result.error = "model file not found: " + request.modelPath;
        return result;
    }

    if (!isSupportedModelExtension(model.getFileExtension()))
    {
        result.error = "unsupported RAVE model extension: " + model.getFileExtension();
        return result;
    }

    if (request.outputPath.isEmpty())
    {
        result.error = "outputPath required";
        return result;
    }

    const auto scriptPath = resolveScriptPath(request.scriptPath);
    if (scriptPath.isEmpty())
    {
        result.error = "RAVE transform script not configured (set scriptPath or HDAW_RAVE_SCRIPT)";
        return result;
    }

    const juce::File script(scriptPath);
    if (!script.existsAsFile())
    {
        result.error = "RAVE transform script not found: " + scriptPath;
        return result;
    }

    const auto pythonPath = resolvePythonPath(request.pythonPath);

    QProcess process;
    process.setProcessEnvironment(QProcessEnvironment::systemEnvironment());

    QStringList args;
    args << QString::fromUtf8(scriptPath.toRawUTF8())
         << QStringLiteral("--input") << QString::fromUtf8(input.getFullPathName().toRawUTF8())
         << QStringLiteral("--model") << QString::fromUtf8(model.getFullPathName().toRawUTF8())
         << QStringLiteral("--output") << QString::fromUtf8(request.outputPath.toRawUTF8())
         << QStringLiteral("--temperature") << QString::number(request.temperature, 'g', 12)
         << QStringLiteral("--seed") << QString::number(request.seed);

    process.start(QString::fromUtf8(pythonPath.toRawUTF8()), args);
    if (!process.waitForStarted(5000))
    {
        result.error = "failed to start RAVE sidecar: " + juce::String(process.errorString().toUtf8().constData());
        result.stderrText = juce::String(process.readAllStandardError().constData());
        result.stdoutText = juce::String(process.readAllStandardOutput().constData());
        return result;
    }

    if (!process.waitForFinished(resolveTimeoutMs()))
    {
        process.kill();
        process.waitForFinished(5000);
        result.exitCode = process.exitCode();
        result.error = "RAVE sidecar timed out";
        result.stderrText = juce::String(process.readAllStandardError().constData());
        result.stdoutText = juce::String(process.readAllStandardOutput().constData());
        return result;
    }

    result.exitCode = process.exitCode();
    result.stderrText = juce::String(process.readAllStandardError().constData());
    result.stdoutText = juce::String(process.readAllStandardOutput().constData());
    result.ok = process.exitStatus() == QProcess::NormalExit && result.exitCode == 0;
    if (result.ok && !juce::File(request.outputPath).existsAsFile())
    {
        result.ok = false;
        result.error = "RAVE sidecar completed successfully but did not create output: " + request.outputPath;
    }
    else if (!result.ok)
    {
        result.error = result.stderrText.isNotEmpty() ? result.stderrText : ("RAVE sidecar failed with exit code " + juce::String(result.exitCode));
    }
    return result;
}

} // namespace HDAW
