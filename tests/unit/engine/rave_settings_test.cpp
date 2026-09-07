// RAVE #5: persisted RAVE settings.
//
// Covers (a) settings.getRaveConfig / settings.setRaveConfig round-trip
// through frontend::dispatch, (b) RaveService::listModels scanning the
// QSettings-configured model dirs when no explicit directory is given, and
// (c) the script-path resolution order (explicit request > QSettings > env),
// and (d) the zero-config script fallbacks (dev-tree <cwd>/tools/rave,
// packaged <exe_dir>/../rave) added by script-path option (c).
//
// QSettings guard pattern follows tests/unit/frontend/frontend_server_test.cpp
// (SettingsNamespaceExposesMcpHttpConfig): set org/app name to HDAW, save the
// prior value of EVERY key touched, restore in teardown (removing keys that
// were unset) so no state leaks to other tests. HDAW_RAVE_SCRIPT /
// HDAW_RAVE_PYTHON are guarded the same way so a dev/CI environment never
// masks the QSettings layer under test.

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariant>

#include "common/SettingsKeys.h"
#include "engine/AudioEngine.h"
#include "engine/RaveService.h"
#include "frontend/FrontendRouter.h"

#include <cstdlib>
#include <tuple>
#include <utility>
#include <vector>

namespace {

const char* const kRaveKeys[] = {
    SettingsKeys::kKeyRaveModelDirs,
    SettingsKeys::kKeyRaveDefaultModel,
    SettingsKeys::kKeyRavePythonPath,
    SettingsKeys::kKeyRaveScriptPath,
    SettingsKeys::kKeyRaveTimeoutMs,
};

void writeFile(const QString& path)
{
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("x");
    file.close();
}

// Repo root derived from this translation unit: the file lives at
// tests/unit/engine/rave_settings_test.cpp, so four getParentDirectory()
// hops (engine, unit, tests, <repo root>) reach the checkout root. Callers
// ASSERT the expected child exists so a relative-__FILE__ build layout fails
// loudly instead of silently mis-resolving.
juce::File repoRootFromThisFile()
{
    return juce::File(juce::CharPointer_UTF8(__FILE__))
        .getParentDirectory()
        .getParentDirectory()
        .getParentDirectory()
        .getParentDirectory();
}

// RAII cwd swap: cwd is process-global like QSettings, so every fallback test
// restores it exactly (pitfall: a leaked cwd would poison later suites that
// read <cwd>/rave or <cwd>/tools).
class CwdGuard
{
public:
    explicit CwdGuard(const juce::File& newCwd)
        : prior_(juce::File::getCurrentWorkingDirectory())
    {
        newCwd.setAsCurrentWorkingDirectory();
    }
    ~CwdGuard()
    {
        prior_.setAsCurrentWorkingDirectory();
    }
    CwdGuard(const CwdGuard&) = delete;
    CwdGuard& operator=(const CwdGuard&) = delete;

private:
    juce::File prior_;
};

} // namespace

class RaveSettings : public ::testing::Test
{
protected:
    void SetUp() override
    {
        QCoreApplication::setOrganizationName(QStringLiteral("HDAW"));
        QCoreApplication::setApplicationName(QStringLiteral("HDAW"));
        {
            QSettings s;
            for (const char* key : kRaveKeys)
                prior_.emplace_back(QString::fromUtf8(key), s.value(QString::fromUtf8(key)));
        }
        for (const char* name : { "HDAW_RAVE_SCRIPT", "HDAW_RAVE_PYTHON" })
        {
            const bool had = qEnvironmentVariableIsSet(name);
            priorEnv_.emplace_back(QByteArray(name),
                                   had ? qEnvironmentVariable(name).toUtf8() : QByteArray(),
                                   had);
            qunsetenv(name);
#ifdef _WIN32
            // RaveService::envString reads std::getenv (CRT), not Qt's env
            // copy: make the clear CRT-visible too so a machine-level
            // HDAW_RAVE_SCRIPT can never mask the cwd-fallback tests.
            _putenv_s(name, "");
#endif
        }
    }

    void TearDown() override
    {
        {
            QSettings s;
            for (const auto& [key, value] : prior_)
            {
                if (value.isValid())
                    s.setValue(key, value);
                else
                    s.remove(key);
            }
            s.sync();
        }
        for (const auto& [name, value, had] : priorEnv_)
        {
            if (had)
                qputenv(name.constData(), value);
            else
                qunsetenv(name.constData());
#ifdef _WIN32
            _putenv_s(name.constData(), had ? value.constData() : "");
#endif
        }
    }

    std::vector<std::pair<QString, QVariant>> prior_;
    std::vector<std::tuple<QByteArray, QByteArray, bool>> priorEnv_;
};

// (a) RPC round-trip -----------------------------------------------------------

TEST_F(RaveSettings, UnsetConfigReturnsEmptyValuesAndDefaultTimeout)
{
    {
        QSettings s;
        for (const char* key : kRaveKeys)
            s.remove(QString::fromUtf8(key));
        s.sync();
    }

    AudioEngine engine;
    auto resp = frontend::dispatch(engine, "settings.getRaveConfig", QJsonValue());
    ASSERT_FALSE(resp.isError);
    const auto cfg = resp.payload.toObject();
    EXPECT_TRUE(cfg.contains("modelDirs"));
    EXPECT_TRUE(cfg.contains("defaultModel"));
    EXPECT_TRUE(cfg.contains("pythonPath"));
    EXPECT_TRUE(cfg.contains("scriptPath"));
    EXPECT_TRUE(cfg.contains("timeoutMs"));
    EXPECT_TRUE(cfg.value("modelDirs").toArray().isEmpty());
    EXPECT_TRUE(cfg.value("defaultModel").toString().isEmpty());
    EXPECT_TRUE(cfg.value("pythonPath").toString().isEmpty());
    EXPECT_TRUE(cfg.value("scriptPath").toString().isEmpty());
    EXPECT_EQ(cfg.value("timeoutMs").toInt(), SettingsKeys::kDefaultRaveTimeoutMs);
}

TEST_F(RaveSettings, SetGetRoundTripThroughDispatch)
{
    AudioEngine engine;
    QJsonArray dirs;
    dirs.append(QStringLiteral("C:/rave-models/a"));
    dirs.append(QStringLiteral("C:/rave-models/b"));
    auto setResp = frontend::dispatch(engine, "settings.setRaveConfig", QJsonObject{
        { "modelDirs", dirs },
        { "defaultModel", QStringLiteral("alpha") },
        { "pythonPath", QStringLiteral("C:/python/python.exe") },
        { "scriptPath", QStringLiteral("C:/rave/transform.py") },
        { "timeoutMs", 123456 },
    });
    ASSERT_FALSE(setResp.isError)
        << setResp.payload.toObject().value("message").toString().toStdString();
    EXPECT_TRUE(setResp.payload.isNull());

    auto getResp = frontend::dispatch(engine, "settings.getRaveConfig", QJsonValue());
    ASSERT_FALSE(getResp.isError);
    const auto cfg = getResp.payload.toObject();
    const auto gotDirs = cfg.value("modelDirs").toArray();
    ASSERT_EQ(gotDirs.size(), 2);
    EXPECT_EQ(gotDirs.at(0).toString(), QStringLiteral("C:/rave-models/a"));
    EXPECT_EQ(gotDirs.at(1).toString(), QStringLiteral("C:/rave-models/b"));
    EXPECT_EQ(cfg.value("defaultModel").toString(), QStringLiteral("alpha"));
    EXPECT_EQ(cfg.value("pythonPath").toString(), QStringLiteral("C:/python/python.exe"));
    EXPECT_EQ(cfg.value("scriptPath").toString(), QStringLiteral("C:/rave/transform.py"));
    EXPECT_EQ(cfg.value("timeoutMs").toInt(), 123456);
}

TEST_F(RaveSettings, PartialUpdatePreservesOtherFields)
{
    AudioEngine engine;
    auto first = frontend::dispatch(engine, "settings.setRaveConfig", QJsonObject{
        { "defaultModel", QStringLiteral("alpha") },
        { "scriptPath", QStringLiteral("C:/rave/transform.py") },
        { "timeoutMs", 60000 },
    });
    ASSERT_FALSE(first.isError);

    auto second = frontend::dispatch(engine, "settings.setRaveConfig", QJsonObject{
        { "defaultModel", QStringLiteral("bravo") },
    });
    ASSERT_FALSE(second.isError);
    EXPECT_TRUE(second.payload.isNull());

    auto getResp = frontend::dispatch(engine, "settings.getRaveConfig", QJsonValue());
    ASSERT_FALSE(getResp.isError);
    const auto cfg = getResp.payload.toObject();
    EXPECT_EQ(cfg.value("defaultModel").toString(), QStringLiteral("bravo"));
    EXPECT_EQ(cfg.value("scriptPath").toString(), QStringLiteral("C:/rave/transform.py"));
    EXPECT_EQ(cfg.value("timeoutMs").toInt(), 60000);
}

TEST_F(RaveSettings, NonPositiveTimeoutRejected)
{
    AudioEngine engine;
    for (int bad : { 0, -5 })
    {
        auto resp = frontend::dispatch(engine, "settings.setRaveConfig",
                                       QJsonObject{ { "timeoutMs", bad } });
        EXPECT_TRUE(resp.isError) << "timeoutMs=" << bad << " accepted";
        EXPECT_EQ(resp.payload.toObject().value("code").toInt(), -32602);
    }
    // A rejected patch must not have written anything.
    auto getResp = frontend::dispatch(engine, "settings.getRaveConfig", QJsonValue());
    ASSERT_FALSE(getResp.isError);
    EXPECT_NE(getResp.payload.toObject().value("timeoutMs").toInt(), 0);
    EXPECT_NE(getResp.payload.toObject().value("timeoutMs").toInt(), -5);
}

// (b) listModels picks up QSettings model dirs ---------------------------------

TEST_F(RaveSettings, ListModelsPicksUpSettingsModelDirs)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    writeFile(dir.filePath("settings_beta.rave"));

    {
        QSettings s;
        s.setValue(SettingsKeys::kKeyRaveModelDirs, QStringList{ dir.path() });
        s.sync();
    }

    const HDAW::RaveService service;
    const auto models = service.listModels(); // no explicit directory
    bool found = false;
    for (const auto& model : models)
    {
        if (model.name == "settings_beta")
        {
            found = true;
            EXPECT_TRUE(juce::File(model.path).existsAsFile());
        }
    }
    EXPECT_TRUE(found);
}

// (c) script-path resolution order ----------------------------------------------

TEST_F(RaveSettings, ScriptFromSettingsUsedWhenRequestOmitsIt)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    writeFile(dir.filePath("input.wav"));
    writeFile(dir.filePath("model.rave"));
    // NOTE: settings_script_missing.py is never created.

    {
        QSettings s;
        s.setValue(SettingsKeys::kKeyRaveScriptPath,
                   dir.filePath("settings_script_missing.py"));
        s.sync();
    }

    HDAW::RaveTransformRequest req;
    req.inputPath = dir.filePath("input.wav").toStdString();
    req.modelPath = dir.filePath("model.rave").toStdString();
    req.outputPath = dir.filePath("out.wav").toStdString();
    // scriptPath intentionally empty: env HDAW_RAVE_SCRIPT unset in SetUp, so
    // resolution must land on the QSettings value.

    const HDAW::RaveService service;
    const auto result = service.transformFile(req);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.error.contains("script not found"))
        << result.error.toStdString();
    EXPECT_TRUE(result.error.contains("settings_script_missing.py"))
        << result.error.toStdString();
}

TEST_F(RaveSettings, ExplicitRequestScriptWinsOverSettings)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    writeFile(dir.filePath("input.wav"));
    writeFile(dir.filePath("model.rave"));
    // Neither script exists; the error must name the REQUEST path, proving the
    // explicit field outranks QSettings.

    {
        QSettings s;
        s.setValue(SettingsKeys::kKeyRaveScriptPath,
                   dir.filePath("settings_script_missing.py"));
        s.sync();
    }

    HDAW::RaveTransformRequest req;
    req.inputPath = dir.filePath("input.wav").toStdString();
    req.modelPath = dir.filePath("model.rave").toStdString();
    req.outputPath = dir.filePath("out.wav").toStdString();
    req.scriptPath = dir.filePath("request_script_missing.py").toStdString();

    const HDAW::RaveService service;
    const auto result = service.transformFile(req);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.error.contains("request_script_missing.py"))
        << result.error.toStdString();
    EXPECT_FALSE(result.error.contains("settings_script_missing.py"))
        << result.error.toStdString();
}

// (d) zero-config script fallbacks (script-path option c) ----------------------

TEST_F(RaveSettings, ScriptFallsBackToDevTreeFromCwd)
{
    const auto root = repoRootFromThisFile();
    const auto expected = root.getChildFile("tools")
                              .getChildFile("rave")
                              .getChildFile("rave_transform.py");
    ASSERT_TRUE(expected.existsAsFile())
        << "repo-root derivation from __FILE__ failed: "
        << root.getFullPathName().toStdString();

    {
        QSettings s;
        s.remove(SettingsKeys::kKeyRaveScriptPath);
        s.sync();
    }
    // env HDAW_RAVE_SCRIPT unset by SetUp (qunsetenv + _putenv_s).

    const CwdGuard cwd(root);
    const auto resolved = HDAW::RaveService::resolveScriptPath({});
    EXPECT_EQ(juce::File(resolved).getFullPathName(), expected.getFullPathName())
        << resolved.toStdString();
}

TEST_F(RaveSettings, SettingsScriptPathWinsOverCwdFallback)
{
    const auto root = repoRootFromThisFile();
    ASSERT_TRUE(root.getChildFile("tools")
                    .getChildFile("rave")
                    .getChildFile("rave_transform.py")
                    .existsAsFile())
        << "repo-root derivation from __FILE__ failed: "
        << root.getFullPathName().toStdString();

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    writeFile(dir.filePath("settings_script.py"));

    {
        QSettings s;
        s.setValue(SettingsKeys::kKeyRaveScriptPath,
                   dir.filePath("settings_script.py"));
        s.sync();
    }

    // cwd makes the dev fallback candidate resolvable, so this asserts the
    // candidate order: QSettings must win over the cwd fallback (and the
    // exe-relative candidate never fires under gtest: build/../rave has no
    // rave_transform.py).
    const CwdGuard cwd(root);
    const auto resolved = HDAW::RaveService::resolveScriptPath({});
    const juce::File settingsScript(dir.filePath("settings_script.py").toStdString());
    EXPECT_EQ(juce::File(resolved).getFullPathName(), settingsScript.getFullPathName())
        << resolved.toStdString();
    EXPECT_FALSE(resolved.contains("rave_transform"))
        << "cwd fallback outranked QSettings: " << resolved.toStdString();
}

TEST_F(RaveSettings, NoScriptAnywhereKeepsNotConfiguredError)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    writeFile(dir.filePath("input.wav"));
    writeFile(dir.filePath("model.rave"));

    {
        QSettings s;
        s.remove(SettingsKeys::kKeyRaveScriptPath);
        s.sync();
    }

    // Temp-dir cwd has no tools/rave; exe candidate (build/../rave) has no
    // rave_transform.py either -> nothing resolves anywhere.
    const CwdGuard cwd(juce::File(dir.path().toStdString()));
    ASSERT_TRUE(HDAW::RaveService::resolveScriptPath({}).isEmpty())
        << "a fallback candidate resolved unexpectedly from cwd "
        << juce::File::getCurrentWorkingDirectory().getFullPathName().toStdString();

    HDAW::RaveTransformRequest req;
    req.inputPath = dir.filePath("input.wav").toStdString();
    req.modelPath = dir.filePath("model.rave").toStdString();
    req.outputPath = dir.filePath("out.wav").toStdString();

    const HDAW::RaveService service;
    const auto result = service.transformFile(req);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.error.contains("not configured (set scriptPath or HDAW_RAVE_SCRIPT)"))
        << result.error.toStdString();
}
