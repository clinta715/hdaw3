// O1 — set_audio_output_device / audio.setOutputDevice must REFUSE an unknown
// device name instead of silently dropping the open device and persisting the
// dead name to QSettings. One shared validator (common/AudioDeviceNameApply.h)
// serves both surfaces, so this file exercises BOTH and asserts the refusal
// text is identical.
//
// Deviceless discipline: the unknown-name + non-destructive cases need no real
// device at all; the valid-name / empty-name cases depend on the environment's
// WASAPI enumeration and GTEST_SKIP when it is empty, like
// incremental_routing_ab_test.cpp does.

#include <gtest/gtest.h>

#include "common/SettingsKeys.h"
#include "common/AudioDeviceNameApply.h"
#include "engine/AudioEngine.h"
#include "frontend/FrontendRouter.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QString>

#include <memory>
#include <string>

namespace {

class AudioOutputDeviceRefusalTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        server = std::make_unique<mcp::McpServer>();
        server->setEngine(engine.get());
        mcp::registerAllTools(*server);
    }

    void TearDown() override {
        server.reset();
        engine.reset();
    }

    // MCP surface: the tools/call envelope {isError, content}.
    QJsonObject mcpCall(const QJsonObject& args) {
        return server->handleRequestOnTestThread(
                   1, "tools/call",
                   QJsonObject { { "name", "set_audio_output_device" },
                                 { "arguments", args } })
            .toObject();
    }

    static QString mcpText(const QJsonObject& r) {
        return r.value("content").toArray().at(0).toObject().value("text").toString();
    }

    // RPC surface: audio.setOutputDevice.
    frontend::DispatchResult rpcCall(const QJsonObject& args) {
        return frontend::dispatch(*engine, "audio.setOutputDevice", args);
    }

    QString currentOutputName() const {
        return QString::fromUtf8(
            engine->getDeviceManager().getAudioDeviceSetup().outputDeviceName.toRawUTF8());
    }

    QStringList availableOutputNames() const {
        QStringList names;
        auto* type = engine->getDeviceManager().getCurrentDeviceTypeObject();
        if (type != nullptr)
            for (const auto& n : type->getDeviceNames(false))
                names << QString::fromUtf8(n.toRawUTF8());
        return names;
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;
};

// G1: an unknown non-empty name is refused with the requested name AND the
// available device list — and BOTH surfaces produce the IDENTICAL message.
TEST_F(AudioOutputDeviceRefusalTest, UnknownNameRefusedIdenticallyOnBothSurfaces) {
    // A name that can never be in the enumeration.
    const QString bogus = "HDAW surely-not-a-real-device 0xF00D";
    ASSERT_FALSE(availableOutputNames().contains(bogus));

    const auto mcp = mcpCall(QJsonObject { { "name", bogus } });
    ASSERT_TRUE(mcp.value("isError").toBool())
        << "unknown device name must be refused, got: "
        << mcpText(mcp).toStdString();
    const QString mcpMsg = mcpText(mcp);
    EXPECT_NE(mcpMsg.indexOf(bogus), -1) << "refusal must name the requested device";
    for (const QString& avail : availableOutputNames())
        EXPECT_NE(mcpMsg.indexOf(avail), -1)
            << "refusal must list available devices (missing: "
            << avail.toStdString() << ")";

    const auto rpc = rpcCall(QJsonObject { { "name", bogus } });
    ASSERT_TRUE(rpc.isError) << "RPC surface must refuse too";
    EXPECT_EQ(rpc.payload.toObject().value("message").toString(), mcpMsg)
        << "both surfaces must produce the identical refusal text";
}

// G2: the refusal is non-destructive — the previously open device/setup is
// unchanged, and the dead name is NOT persisted to QSettings.
TEST_F(AudioOutputDeviceRefusalTest, RefusalKeepsPreviousDeviceAndSkipsQSettings) {
    const auto before = engine->getDeviceManager().getAudioDeviceSetup();
    auto* devBefore = engine->getDeviceManager().getCurrentAudioDevice();

    QSettings st;
    const QString savedBefore = st.value(SettingsKeys::kKeyAudioOutputDevice).toString();

    const auto mcp = mcpCall(QJsonObject { { "name", "HDAW nonexistent output" } });
    ASSERT_TRUE(mcp.value("isError").toBool());

    const auto after = engine->getDeviceManager().getAudioDeviceSetup();
    EXPECT_EQ(after.outputDeviceName, before.outputDeviceName);
    EXPECT_EQ(after.inputDeviceName, before.inputDeviceName);
    EXPECT_EQ(engine->getDeviceManager().getCurrentAudioDevice() == nullptr,
              devBefore == nullptr)
        << "refusal must not change whether a device is open";
    EXPECT_EQ(st.value(SettingsKeys::kKeyAudioOutputDevice).toString(), savedBefore)
        << "a refused set must NOT persist the device name to QSettings";
}

// Deterministic rollback coverage (review requirement): the apply step is an
// injectable seam (applyOutputDeviceValidated), so the rollback sequence runs
// here WITHOUT hardware. Fake apply: fails for the requested name, succeeds
// for the snapshot.
TEST_F(AudioOutputDeviceRefusalTest, RollbackRestoresSnapshotWhenApplyFailsOnce) {
    QSettings st;
    const QString savedBefore = st.value(SettingsKeys::kKeyAudioOutputDevice).toString();

    auto& dm = engine->getDeviceManager();
    const auto previous = dm.getAudioDeviceSetup();

    int calls = 0;
    juce::StringArray appliedNames;
    HDAW::DeviceSetupApplyFn fake =
        [&](juce::AudioDeviceManager&, const juce::AudioDeviceManager::AudioDeviceSetup& s) {
            ++calls;
            appliedNames.add(s.outputDeviceName);
            if (s.outputDeviceName == juce::String("Broken Device"))
                return juce::String("No driver (fake)");
            return juce::String{};
        };

    const auto err = HDAW::applyOutputDeviceValidated(dm, juce::String("Broken Device"), fake);

    EXPECT_EQ(err, juce::String("could not open output device \"Broken Device\": No driver (fake)"))
        << "restore success must return ONLY the open-failure text";
    EXPECT_EQ(calls, 2) << "apply once, then rollback once";
    ASSERT_EQ(appliedNames.size(), 2);
    EXPECT_EQ(appliedNames[1], previous.outputDeviceName)
        << "the rollback invocation must carry the SNAPSHOT setup taken before the failed apply";
    EXPECT_EQ(st.value(SettingsKeys::kKeyAudioOutputDevice).toString(), savedBefore)
        << "helper must never touch QSettings";
}

// Both invocations fail → the combined message names the original open error
// AND the rollback failure; QSettings untouched.
TEST_F(AudioOutputDeviceRefusalTest, RollbackFailureIsLoudInCombinedMessage) {
    QSettings st;
    const QString savedBefore = st.value(SettingsKeys::kKeyAudioOutputDevice).toString();

    auto& dm = engine->getDeviceManager();

    int calls = 0;
    HDAW::DeviceSetupApplyFn alwaysFails =
        [&calls](juce::AudioDeviceManager&, const juce::AudioDeviceManager::AudioDeviceSetup&) {
            ++calls;
            return juce::String("No driver (fake)");
        };

    const auto err = HDAW::applyOutputDeviceValidated(dm, juce::String("Broken Device"), alwaysFails);

    const auto text = err.toStdString();
    EXPECT_NE(text.find("could not open output device \"Broken Device\": No driver (fake)"),
              std::string::npos)
        << text;
    EXPECT_NE(text.find("; rollback to previous device also failed: No driver (fake)"),
              std::string::npos)
        << text;
    EXPECT_EQ(calls, 2) << "apply once, then the failed rollback attempt";
    EXPECT_EQ(st.value(SettingsKeys::kKeyAudioOutputDevice).toString(), savedBefore)
        << "helper must never touch QSettings";
}

// G3: a valid enumerated name either opens exactly as before (observable
// output + QSettings persisted) OR — on a box whose listed device cannot
// actually open ("No driver" — the deviceless-pattern environmental failure)
// — hits the P6-b rollback branch: the error is returned AND the previous
// setup is restored, with QSettings skipped. Silent-drop is the only failure.
// The failure outcome doubles as the rollback-branch coverage.
TEST_F(AudioOutputDeviceRefusalTest, ValidNameOpensOrRollsBackNeverSilent) {
    const QStringList names = availableOutputNames();
    if (names.isEmpty())
        GTEST_SKIP() << "no output devices enumerated in this environment";

    const QString valid = names.first();
    const auto previous = engine->getDeviceManager().getAudioDeviceSetup();
    QSettings st;
    const QString savedBefore = st.value(SettingsKeys::kKeyAudioOutputDevice).toString();

    const auto rpc = rpcCall(QJsonObject { { "name", valid } });

    if (rpc.isError) {
        const QString msg = rpc.payload.toObject().value("message").toString();
        EXPECT_NE(msg.indexOf("could not open output device"), -1)
            << "an open failure must surface the open error, got: "
            << msg.toStdString();
        const auto after = engine->getDeviceManager().getAudioDeviceSetup();
        EXPECT_EQ(after.outputDeviceName, previous.outputDeviceName)
            << "P6-b: the previous device setup must be restored on open failure";
        EXPECT_EQ(st.value(SettingsKeys::kKeyAudioOutputDevice).toString(), savedBefore)
            << "a failed set must NOT persist the device name";
        return;
    }

    // Observable through the MCP read surface, not just the ok payload.
    const auto setup = server->handleRequestOnTestThread(
        2, "tools/call",
        QJsonObject { { "name", "get_audio_current_setup" }, { "arguments", QJsonObject{} } });
    const auto setupObj = QJsonDocument::fromJson(
        setup.toObject().value("content").toArray().at(0).toObject()
            .value("text").toString().toUtf8()).object();
    EXPECT_EQ(setupObj.value("output").toString(), valid);

    EXPECT_EQ(st.value(SettingsKeys::kKeyAudioOutputDevice).toString(), valid)
        << "a successful set must persist the device name";
}

// G4: the empty name is the documented close/default path — it must be
// ALLOWED THROUGH (never refused as an unknown device). A JUCE open error on
// a driverless box is environmental; only the unknown-name refusal would be a
// defect. QSettings must not gain a stale name either way.
TEST_F(AudioOutputDeviceRefusalTest, EmptyNameIsAllowedThroughNeverUnknownNameRefused) {
    if (availableOutputNames().isEmpty())
        GTEST_SKIP() << "no output devices enumerated in this environment — "
                        "the empty-name close path cannot be observed";

    QSettings st;
    const QString savedBefore = st.value(SettingsKeys::kKeyAudioOutputDevice).toString();

    const auto mcp = mcpCall(QJsonObject { { "name", QString("") } });
    if (mcp.value("isError").toBool()) {
        EXPECT_EQ(mcpText(mcp).indexOf("unknown output device"), -1)
            << "the empty name must not be refused as unknown, got: "
            << mcpText(mcp).toStdString();
        EXPECT_EQ(st.value(SettingsKeys::kKeyAudioOutputDevice).toString(), savedBefore)
            << "a failed close must not persist a stale name";
        return;
    }
    EXPECT_EQ(currentOutputName(), QString())
        << "empty name must leave the setup output empty (closed/default)";
}

// ── Input twin (O1 input device side) ───────────────────────────────────────

class AudioInputDeviceRefusalTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        server = std::make_unique<mcp::McpServer>();
        server->setEngine(engine.get());
        mcp::registerAllTools(*server);
    }

    void TearDown() override {
        server.reset();
        engine.reset();
    }

    QJsonObject mcpCall(const QJsonObject& args) {
        return server->handleRequestOnTestThread(
                   1, "tools/call",
                   QJsonObject { { "name", "set_audio_input_device" },
                                 { "arguments", args } })
            .toObject();
    }

    static QString mcpText(const QJsonObject& r) {
        return r.value("content").toArray().at(0).toObject().value("text").toString();
    }

    frontend::DispatchResult rpcCall(const QJsonObject& args) {
        return frontend::dispatch(*engine, "audio.setInputDevice", args);
    }

    QString currentInputName() const {
        return QString::fromUtf8(
            engine->getDeviceManager().getAudioDeviceSetup().inputDeviceName.toRawUTF8());
    }

    QStringList availableInputNames() const {
        QStringList names;
        auto* type = engine->getDeviceManager().getCurrentDeviceTypeObject();
        if (type != nullptr)
            for (const auto& n : type->getDeviceNames(true))
                names << QString::fromUtf8(n.toRawUTF8());
        return names;
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;
};

// G1: an unknown non-empty input name is refused with the requested name AND
// the available INPUT device list — identical bytes on both surfaces.
TEST_F(AudioInputDeviceRefusalTest, UnknownNameRefusedIdenticallyOnBothSurfaces) {
    const QString bogus = "HDAW surely-not-a-real-input 0xF00D";
    ASSERT_FALSE(availableInputNames().contains(bogus));

    const auto mcp = mcpCall(QJsonObject { { "name", bogus } });
    ASSERT_TRUE(mcp.value("isError").toBool())
        << "unknown input device name must be refused, got: "
        << mcpText(mcp).toStdString();
    const QString mcpMsg = mcpText(mcp);
    EXPECT_NE(mcpMsg.indexOf(bogus), -1) << "refusal must name the requested device";
    EXPECT_NE(mcpMsg.indexOf("unknown input device"), -1)
        << "refusal must say which side failed";
    for (const QString& avail : availableInputNames())
        EXPECT_NE(mcpMsg.indexOf(avail), -1)
            << "refusal must list available input devices (missing: "
            << avail.toStdString() << ")";

    const auto rpc = rpcCall(QJsonObject { { "name", bogus } });
    ASSERT_TRUE(rpc.isError) << "RPC surface must refuse too";
    EXPECT_EQ(rpc.payload.toObject().value("message").toString(), mcpMsg)
        << "both surfaces must produce the identical refusal text";
}

// G2: the refusal is non-destructive — the setup (input AND output names) is
// unchanged, and the dead name is NOT persisted to QSettings.
TEST_F(AudioInputDeviceRefusalTest, RefusalKeepsPreviousDeviceAndSkipsQSettings) {
    const auto before = engine->getDeviceManager().getAudioDeviceSetup();
    auto* devBefore = engine->getDeviceManager().getCurrentAudioDevice();

    QSettings st;
    const QString savedBefore = st.value(SettingsKeys::kKeyAudioInputDevice).toString();

    const auto mcp = mcpCall(QJsonObject { { "name", "HDAW nonexistent input" } });
    ASSERT_TRUE(mcp.value("isError").toBool());

    const auto after = engine->getDeviceManager().getAudioDeviceSetup();
    EXPECT_EQ(after.inputDeviceName, before.inputDeviceName);
    EXPECT_EQ(after.outputDeviceName, before.outputDeviceName);
    EXPECT_EQ(engine->getDeviceManager().getCurrentAudioDevice() == nullptr,
              devBefore == nullptr)
        << "refusal must not change whether a device is open";
    EXPECT_EQ(st.value(SettingsKeys::kKeyAudioInputDevice).toString(), savedBefore)
        << "a refused set must NOT persist the device name to QSettings";
}

// Deterministic rollback coverage: fake apply fails for the requested input
// name, succeeds for the snapshot — no hardware needed.
TEST_F(AudioInputDeviceRefusalTest, RollbackRestoresSnapshotWhenApplyFailsOnce) {
    QSettings st;
    const QString savedBefore = st.value(SettingsKeys::kKeyAudioInputDevice).toString();

    auto& dm = engine->getDeviceManager();
    const auto previous = dm.getAudioDeviceSetup();

    int calls = 0;
    juce::StringArray appliedNames;
    HDAW::DeviceSetupApplyFn fake =
        [&](juce::AudioDeviceManager&, const juce::AudioDeviceManager::AudioDeviceSetup& s) {
            ++calls;
            appliedNames.add(s.inputDeviceName);
            if (s.inputDeviceName == juce::String("Broken Input"))
                return juce::String("No driver (fake)");
            return juce::String{};
        };

    const auto err = HDAW::applyInputDeviceValidated(dm, juce::String("Broken Input"), fake);

    EXPECT_EQ(err, juce::String("could not open input device \"Broken Input\": No driver (fake)"))
        << "restore success must return ONLY the open-failure text";
    EXPECT_EQ(calls, 2) << "apply once, then rollback once";
    ASSERT_EQ(appliedNames.size(), 2);
    EXPECT_EQ(appliedNames[1], previous.inputDeviceName)
        << "the rollback invocation must carry the SNAPSHOT setup taken before the failed apply";
    EXPECT_EQ(st.value(SettingsKeys::kKeyAudioInputDevice).toString(), savedBefore)
        << "helper must never touch QSettings";
}

// Both invocations fail → the combined message names the original open error
// AND the rollback failure; QSettings untouched.
TEST_F(AudioInputDeviceRefusalTest, RollbackFailureIsLoudInCombinedMessage) {
    QSettings st;
    const QString savedBefore = st.value(SettingsKeys::kKeyAudioInputDevice).toString();

    auto& dm = engine->getDeviceManager();

    int calls = 0;
    HDAW::DeviceSetupApplyFn alwaysFails =
        [&calls](juce::AudioDeviceManager&, const juce::AudioDeviceManager::AudioDeviceSetup&) {
            ++calls;
            return juce::String("No driver (fake)");
        };

    const auto err = HDAW::applyInputDeviceValidated(dm, juce::String("Broken Input"), alwaysFails);

    const auto text = err.toStdString();
    EXPECT_NE(text.find("could not open input device \"Broken Input\": No driver (fake)"),
              std::string::npos)
        << text;
    EXPECT_NE(text.find("; rollback to previous device also failed: No driver (fake)"),
              std::string::npos)
        << text;
    EXPECT_EQ(calls, 2) << "apply once, then the failed rollback attempt";
    EXPECT_EQ(st.value(SettingsKeys::kKeyAudioInputDevice).toString(), savedBefore)
        << "helper must never touch QSettings";
}

// G3: a valid enumerated INPUT name either opens (observable input set +
// QSettings persisted) OR — when the listed device cannot actually open —
// hits the rollback branch: error returned, previous setup restored, QSettings
// skipped. Silent-drop is the only failure.
TEST_F(AudioInputDeviceRefusalTest, ValidNameOpensOrRollsBackNeverSilent) {
    const QStringList names = availableInputNames();
    if (names.isEmpty())
        GTEST_SKIP() << "no input devices enumerated in this environment";

    const QString valid = names.first();
    const auto previous = engine->getDeviceManager().getAudioDeviceSetup();
    QSettings st;
    const QString savedBefore = st.value(SettingsKeys::kKeyAudioInputDevice).toString();

    const auto rpc = rpcCall(QJsonObject { { "name", valid } });

    if (rpc.isError) {
        const QString msg = rpc.payload.toObject().value("message").toString();
        EXPECT_NE(msg.indexOf("could not open input device"), -1)
            << "an open failure must surface the open error, got: "
            << msg.toStdString();
        const auto after = engine->getDeviceManager().getAudioDeviceSetup();
        EXPECT_EQ(after.inputDeviceName, previous.inputDeviceName)
            << "P6-b: the previous device setup must be restored on open failure";
        EXPECT_EQ(st.value(SettingsKeys::kKeyAudioInputDevice).toString(), savedBefore)
            << "a failed set must NOT persist the device name";
        return;
    }

    EXPECT_EQ(currentInputName(), valid)
        << "a successful set must be observable in the setup";
    EXPECT_EQ(st.value(SettingsKeys::kKeyAudioInputDevice).toString(), valid)
        << "a successful set must persist the device name";
}

// G4: the empty name is the documented "no input" spelling — ALLOWED THROUGH
// (never refused as an unknown device). Only an unknown-name refusal would be
// a defect; a JUCE open error is environmental. No stale QSettings either way.
TEST_F(AudioInputDeviceRefusalTest, EmptyNameIsAllowedThroughNeverUnknownNameRefused) {
    QSettings st;
    const QString savedBefore = st.value(SettingsKeys::kKeyAudioInputDevice).toString();

    const auto mcp = mcpCall(QJsonObject { { "name", QString("") } });
    if (mcp.value("isError").toBool()) {
        EXPECT_EQ(mcpText(mcp).indexOf("unknown input device"), -1)
            << "the empty name must not be refused as unknown, got: "
            << mcpText(mcp).toStdString();
        EXPECT_EQ(st.value(SettingsKeys::kKeyAudioInputDevice).toString(), savedBefore)
            << "a failed clear must not persist a stale name";
        return;
    }
    EXPECT_EQ(currentInputName(), QString())
        << "empty name must leave the setup input empty (no input device)";
}

} // namespace
