// Headless engine MCP HTTP listen port — the silent-drop regression
// (2026-10-01).
//
// Measured: `HDAW_headless.exe --mcp-http --mcp-http-port 18841 --port 18843`
// bound the WebSocket frontend on 18843 (so parseHeadlessArgs ran) but the
// engine log said "MCP HTTP start failed: failed to listen on
// 127.0.0.1:18765" — it used the PERSISTED port, not the CLI one, and the
// registry key stayed at the old value while the process was running.
//
// Root cause: main_headless.cpp / main.cpp wrote the mcp/http* settings with a
// DEFAULT-CONSTRUCTED QSettings BEFORE any QCoreApplication instance existed.
// Qt's default ctor resolves the organization/application names from
// QCoreApplication::organizationName()/applicationName(), which are EMPTY
// without an instance, so the write was silently discarded;
// AudioEngine::initialize() -> syncMcpHttpFromSettings() then read the stale
// persisted value and bound that.
//
// Consequence that cost real time: a second engine launched with --mcp-http
// cannot bind the port a stale engine already holds, logs the failure and KEEPS
// RUNNING — it looks alive and healthy while every tool call is answered by the
// OTHER (older) engine.
//
// Contract pinned by this file (the fix itself lives in main_headless.cpp):
//   1. an explicit --mcp-http-port is the port actually served;
//   2. when that port is already occupied, an explicit --mcp-http exits with a
//      NON-ZERO status promptly instead of lingering alive-but-mute.
//
// A second defect (2026-10-01, fixed in src/engine/AudioEngine.cpp) is pinned
// here too: even when the CLI port was FREE, the engine stayed on the PERSISTED
// port whenever that persisted port also bound successfully, because
// setMcpHttpConfig assigned the requested host/port to the LIVE members before
// startMcpHttp() ran — so startMcpHttp's "already running on the same config"
// check short-circuited against the request and never rebound the transport.
// Case 2 (CliPortWinsOverAFreePersistedPort) makes that deterministic by
// persisting a FREE port A and asserting the engine serves the CLI port B.

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>

#include "common/SettingsKeys.h"

namespace
{

// Slow machines are the norm for a first audio-device init (the engine's
// device open can take 10+ s), so both waits poll against a deadline and never
// rely on a fixed sleep.
constexpr int kStartDeadlineMs = 10000;   // child process creation
constexpr int kServeDeadlineMs = 25000;   // MCP HTTP accepting on the CLI port
constexpr int kExitDeadlineMs  = 25000;   // fail-fast exit when the port is taken
constexpr int kStopDeadlineMs  = 10000;   // terminate/kill settle
constexpr int kPollIntervalMs  = 200;

QString enginePath()
{
    // Platform-specific engine binary name: Windows builds HDAW_headless.exe,
    // Linux has no suffix (the Linux migration left this hard-coded and red).
#ifdef _WIN32
    return QCoreApplication::applicationDirPath() + QStringLiteral("/HDAW_headless.exe");
#else
    return QCoreApplication::applicationDirPath() + QStringLiteral("/HDAW_headless");
#endif
}

// Best-effort free-port probe: bind an ephemeral port, read the OS-assigned
// number, close. A tiny race (something else grabs the port between our close
// and the child's bind) is accepted — it is the same race the engine's own
// "--port 0" request faces, and the alternative (a hard-coded port) collides
// with the live dev-box engine.
quint16 probeFreePort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0))
        return 0;
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
}

// RAII: a leaked engine process poisons every later suite (the repo's
// documented stale-engine class — it keeps holding the MCP HTTP port), so
// EVERY exit path from a test body, including an ASSERT_* early return and a
// thrown gtest failure, must reap the child. The guard kills on scope exit and
// the body also stops the child explicitly on the happy path.
class ChildGuard
{
public:
    explicit ChildGuard(QProcess& proc) : proc_(proc) {}

    ChildGuard(const ChildGuard&) = delete;
    ChildGuard& operator=(const ChildGuard&) = delete;

    ~ChildGuard()
    {
        if (proc_.state() != QProcess::NotRunning)
        {
            proc_.kill();
            proc_.waitForFinished(kStopDeadlineMs);
        }
    }

private:
    QProcess& proc_;
};

// The engine logs to hdaw_debug.log under the child's %TEMP% (pointed at a
// QTemporaryDir below). Surfaced in failure messages: this regression's whole
// signature is a log line that contradicts the requested port.
QString debugLogTail(const QString& dir, int maxChars = 1200)
{
    QFile f(QDir(dir).filePath(QStringLiteral("hdaw_debug.log")));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("<no hdaw_debug.log in %1>").arg(dir);
    const QString text = QString::fromUtf8(f.readAll());
    return text.right(maxChars);
}

// The engine persists mcp/http* into the NATIVE store (the registry on
// Windows) as part of serving an explicit --mcp-http. Snapshot + restore those
// three keys so this test never leaves the machine — or a live dev-box engine —
// pointed at a random probe port. The explicit format/scope ctor is required:
// this TEST process has QSettings redirected to an isolated INI
// (tests/test_main.cpp), and only the spawned child uses the native store.
class NativeMcpHttpSettingsGuard
{
public:
    NativeMcpHttpSettingsGuard()
        : settings_(QSettings::NativeFormat, QSettings::UserScope,
                    QStringLiteral("HDAW"), QStringLiteral("HDAW"))
    {
        const char* const keys[] = { SettingsKeys::kKeyMcpHttpEnabled,
                                     SettingsKeys::kKeyMcpHttpHost,
                                     SettingsKeys::kKeyMcpHttpPort };
        for (int i = 0; i < 3; ++i)
        {
            keys_[i] = keys[i];
            present_[i] = settings_.contains(QString::fromUtf8(keys[i]));
            if (present_[i])
                saved_[i] = settings_.value(QString::fromUtf8(keys[i]));
        }
    }

    NativeMcpHttpSettingsGuard(const NativeMcpHttpSettingsGuard&) = delete;
    NativeMcpHttpSettingsGuard& operator=(const NativeMcpHttpSettingsGuard&) = delete;

    ~NativeMcpHttpSettingsGuard()
    {
        for (int i = 0; i < 3; ++i)
        {
            const QString key = QString::fromUtf8(keys_[i]);
            if (present_[i])
                settings_.setValue(key, saved_[i]);
            else
                settings_.remove(key);
        }
        settings_.sync();
    }

private:
    QSettings settings_;
    const char* keys_[3] = { nullptr, nullptr, nullptr };
    QVariant saved_[3];
    bool present_[3] = { false, false, false };
};

// Write an explicit PERSISTED MCP HTTP port into the CHILD's native settings
// store (the same store AudioEngine::initialize() ->
// syncMcpHttpFromSettings() reads). This is what makes the CLI-port cases
// deterministic: with a FREE persisted port the settings-driven start SUCCEEDS
// on it, so the CLI port can only be served if the explicit --mcp-http-port
// really rebinds the live transport. Pre-fix it did not (startMcpHttp
// short-circuited against the just-assigned members), which is the regression.
// Pair with NativeMcpHttpSettingsGuard so the keys are restored afterwards.
//
// Returns whether the write ACTUALLY LANDED. The native store is not writable
// on every machine (a restricted token makes the registry refuse
// HKCU\Software\HDAW writes, and QSettings::setValue/sync then silently do
// nothing), so a caller must SKIP rather than assert on a precondition that was
// never established. The read-back uses a FRESH QSettings instance: reading
// through the writing instance could be served from its own in-memory cache.
bool persistMcpHttpPort(quint16 port)
{
    const QString enabledKey = QString::fromUtf8(SettingsKeys::kKeyMcpHttpEnabled);
    const QString hostKey    = QString::fromUtf8(SettingsKeys::kKeyMcpHttpHost);
    const QString portKey    = QString::fromUtf8(SettingsKeys::kKeyMcpHttpPort);
    const QString host       = QString::fromUtf8(SettingsKeys::kDefaultMcpHttpHost);

    {
        QSettings settings(QSettings::NativeFormat, QSettings::UserScope,
                           QStringLiteral("HDAW"), QStringLiteral("HDAW"));
        settings.setValue(enabledKey, true);
        settings.setValue(hostKey, host);
        settings.setValue(portKey, static_cast<int>(port));
        settings.sync();
    }

    QSettings verify(QSettings::NativeFormat, QSettings::UserScope,
                     QStringLiteral("HDAW"), QStringLiteral("HDAW"));
    verify.sync();
    return verify.value(enabledKey).toBool() == true
        && verify.value(hostKey).toString() == host
        && verify.value(portKey).toInt() == static_cast<int>(port);
}

// The documented environmental skip shared by the two CLI-port cases: their
// discriminating precondition (a persisted port that BINDS during initialize())
// needs a write into the child's native store, and without that write the case
// would pass vacuously on whichever port the store happens to hold.
QString unwritableNativeStoreSkipMessage()
{
    return QStringLiteral(
        "cannot persist a free MCP HTTP port into the child's native QSettings "
        "store (QSettings::NativeFormat/UserScope, org/app HDAW/HDAW — the "
        "Windows registry key HKCU\\Software\\HDAW): the store is NOT WRITABLE on "
        "this machine (a restricted token makes the write silently no-op, and the "
        "read-back after sync() confirms it did not land), so the persisted-port "
        "precondition cannot be established and this case would not discriminate "
        "the regression. Skipping instead of passing vacuously.");
}

// Is `port` free RIGHT NOW? probeFreePort() only proves a port was free when it
// was picked; the CLI-port cases assert their discriminating preconditions with
// this immediately before spawning, so a lost race is an explicit failure
// rather than a silently non-discriminating pass.
bool isPortFree(quint16 port)
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, port))
        return false;
    probe.close();
    return true;
}

// Spawn the headless engine with BOTH listen ports. The child's %TMP%/%TEMP%
// point at a scratch dir so the engine's hdaw_debug.log (and any dump) lands
// there instead of the machine temp — the QProcess-environment analogue of the
// proxy isolation test's setChildEnv().
void startEngine(QProcess& proc, const QTemporaryDir& tmpDir,
                 quint16 mcpHttpPort, quint16 wsPort)
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("TMP"), tmpDir.path());
    env.insert(QStringLiteral("TEMP"), tmpDir.path());
    proc.setProcessEnvironment(env);
    // The engine runs for up to the whole serve deadline while this thread is
    // blocked in waitForConnected/waitForFinished (no event loop draining
    // QProcess's pipes), so forward its output rather than let a full 64 KB
    // pipe buffer stall it. The diagnostic text we quote in failures comes
    // from hdaw_debug.log in tmpDir.
    proc.setProcessChannelMode(QProcess::ForwardedChannels);
    proc.setWorkingDirectory(QCoreApplication::applicationDirPath());
    proc.start(enginePath(),
               { QStringLiteral("--mcp-http"),
                 QStringLiteral("--mcp-http-port"), QString::number(mcpHttpPort),
                 QStringLiteral("--port"), QString::number(wsPort) });
}

// Poll a TCP connect to `port` until it is accepted or the deadline expires.
// Also stops early when the child dies (then the failure is "exited", not
// "never listened" — a materially different diagnosis).
bool waitForPortAccepting(QProcess& proc, quint16 port, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs)
    {
        if (proc.state() == QProcess::NotRunning)
            return false;
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, port);
        if (socket.waitForConnected(1000))
        {
            socket.disconnectFromHost();
            return true;
        }
        QThread::msleep(kPollIntervalMs);
    }
    return false;
}

// Stop the child and confirm it is really gone: terminate first (graceful), and
// only then hard-kill. Returns true when the process is NotRunning afterwards.
bool stopEngine(QProcess& proc)
{
    if (proc.state() == QProcess::NotRunning)
        return true;
    proc.terminate();
    if (!proc.waitForFinished(kStopDeadlineMs))
    {
        proc.kill();
        proc.waitForFinished(kStopDeadlineMs);
    }
    return proc.state() == QProcess::NotRunning;
}

} // namespace

// Case 1: an explicit --mcp-http-port must be the port the engine serves on.
// Pre-fix the engine bound the PERSISTED port (18765 by default) and this
// connect poll never succeeded.
TEST(HeadlessMcpHttpPort, ServesOnTheCliPort)
{
    const QString exe = enginePath();
    ASSERT_TRUE(QFile::exists(exe))
        << "engine binary not found at " << exe.toStdString()
        << " — this is an environmental failure (build HDAW_headless first), not a pass";

    const quint16 mcpPort = probeFreePort();
    ASSERT_NE(mcpPort, 0) << "could not probe a free MCP HTTP port";
    quint16 wsPort = probeFreePort();
    ASSERT_NE(wsPort, 0) << "could not probe a free WebSocket port";
    if (wsPort == mcpPort)
        wsPort = probeFreePort();   // astronomically unlikely; cheap to avoid

    // A FREE persisted port that is DIFFERENT from the CLI port. Persisting it
    // before the spawn makes syncMcpHttpFromSettings() bind successfully on it
    // during initialize(), so the CLI port can only be served if the explicit
    // --mcp-http-port really rebinds the live transport. Without this the case
    // was environment-dependent: it passed only when the persisted port
    // happened to be BUSY (making the settings-driven start fail, so the CLI
    // port won by default).
    quint16 persistedPort = probeFreePort();
    ASSERT_NE(persistedPort, 0) << "could not probe a free persisted port";
    if (persistedPort == mcpPort || persistedPort == wsPort)
        persistedPort = probeFreePort();
    ASSERT_NE(persistedPort, mcpPort)
        << "persisted port must differ from the CLI port";
    ASSERT_NE(persistedPort, wsPort)
        << "persisted port must differ from the WebSocket port";

    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());

    // Declared BEFORE the process: destroyed LAST, i.e. after the child is
    // reaped, so the child's own settings write cannot land after the restore.
    NativeMcpHttpSettingsGuard settingsGuard;
    if (!persistMcpHttpPort(persistedPort))
        GTEST_SKIP() << unwritableNativeStoreSkipMessage().toStdString();

    // The discriminating precondition, asserted rather than assumed: the
    // persisted port must still be FREE right before the spawn (so the
    // settings-driven start binds it successfully) and so must the CLI port.
    ASSERT_TRUE(isPortFree(persistedPort))
        << "persisted port " << persistedPort << " is not free — the settings "
        << "start would fail on it and this case would not discriminate";
    ASSERT_TRUE(isPortFree(mcpPort))
        << "CLI port " << mcpPort << " is not free — the engine could not bind it";

    QProcess proc;
    ChildGuard guard(proc);
    startEngine(proc, tmpDir, mcpPort, wsPort);

    ASSERT_TRUE(proc.waitForStarted(kStartDeadlineMs))
        << "failed to start " << exe.toStdString();

    const bool accepting = waitForPortAccepting(proc, mcpPort, kServeDeadlineMs);
    ASSERT_TRUE(accepting)
        << "MCP HTTP never accepted on the CLI port " << mcpPort
        << " (exitCode=" << proc.exitCode() << " state=" << int(proc.state()) << ")\n"
        << debugLogTail(tmpDir.path()).toStdString();

    // The engine must still be the process we started (an early exit would have
    // been caught above; this also covers the "died right after accepting" race).
    EXPECT_EQ(proc.state(), QProcess::Running)
        << "engine exited right after accepting on the CLI port\n"
        << debugLogTail(tmpDir.path()).toStdString();

    // Confirm it is gone after we stop it (no leaked engine holding the port).
    EXPECT_TRUE(stopEngine(proc)) << "engine did not exit after terminate/kill";
    EXPECT_EQ(proc.state(), QProcess::NotRunning);
}

// Case 2: a FREE persisted port must not capture the server when the CLI names
// a different port. This is the deterministic, environment-independent form of
// the silent-drop regression: the persisted port binds SUCCESSFULLY during
// initialize(), so the engine ends up on the CLI port only if the explicit
// --mcp-http-port rebinds the live transport. It FAILS against the pre-fix code
// (setMcpHttpConfig assigned the requested port to mcpHttpPort_ before
// startMcpHttp ran, so startMcpHttp's same-config check short-circuited and the
// server stayed on the persisted port) and PASSES after the fix.
TEST(HeadlessMcpHttpPort, CliPortWinsOverAFreePersistedPort)
{
    const QString exe = enginePath();
    ASSERT_TRUE(QFile::exists(exe))
        << "engine binary not found at " << exe.toStdString()
        << " — this is an environmental failure (build HDAW_headless first), not a pass";

    const quint16 persistedPort = probeFreePort();
    ASSERT_NE(persistedPort, 0) << "could not probe a free persisted port";
    quint16 cliPort = probeFreePort();
    ASSERT_NE(cliPort, 0) << "could not probe a free CLI port";
    if (cliPort == persistedPort)
        cliPort = probeFreePort();
    quint16 wsPort = probeFreePort();
    ASSERT_NE(wsPort, 0) << "could not probe a free WebSocket port";
    if (wsPort == cliPort || wsPort == persistedPort)
        wsPort = probeFreePort();
    ASSERT_NE(cliPort, persistedPort)
        << "CLI port must differ from the persisted port";
    ASSERT_NE(wsPort, cliPort) << "WebSocket port must differ from the CLI port";
    ASSERT_NE(wsPort, persistedPort)
        << "WebSocket port must differ from the persisted port";

    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());

    // Persist A (free) BEFORE the child starts; the guard restores it after.
    // When the write does not land (unwritable native store) the case would be
    // vacuous, so skip with the documented environmental message.
    NativeMcpHttpSettingsGuard settingsGuard;
    if (!persistMcpHttpPort(persistedPort))
        GTEST_SKIP() << unwritableNativeStoreSkipMessage().toStdString();

    // The discriminating precondition, asserted rather than assumed: the
    // persisted port A must be FREE right before the spawn, so
    // syncMcpHttpFromSettings() binds it SUCCESSFULLY during initialize() and
    // the engine can end up on the CLI port B only if --mcp-http-port really
    // rebinds the live transport. B must be free too, or the accept poll below
    // would fail for the wrong reason.
    ASSERT_TRUE(isPortFree(persistedPort))
        << "persisted port " << persistedPort << " is not free — the settings "
        << "start would fail on it and this case would not discriminate";
    ASSERT_TRUE(isPortFree(cliPort))
        << "CLI port " << cliPort << " is not free — the engine could not bind it";

    QProcess proc;
    ChildGuard guard(proc);
    startEngine(proc, tmpDir, cliPort, wsPort);

    ASSERT_TRUE(proc.waitForStarted(kStartDeadlineMs))
        << "failed to start " << exe.toStdString();

    // The engine must be serving the CLI port B...
    const bool accepting = waitForPortAccepting(proc, cliPort, kServeDeadlineMs);
    ASSERT_TRUE(accepting)
        << "MCP HTTP never accepted on the CLI port " << cliPort
        << " (persisted port was the FREE " << persistedPort << "; exitCode="
        << proc.exitCode() << " state=" << int(proc.state()) << ")\n"
        << debugLogTail(tmpDir.path()).toStdString();

    // ...and the process must still be the one we started.
    EXPECT_EQ(proc.state(), QProcess::Running)
        << "engine exited right after accepting on the CLI port\n"
        << debugLogTail(tmpDir.path()).toStdString();

    // ...and it must NOT be bound to the persisted port A (the pre-fix
    // behaviour: the transport stayed on the persisted port).
    QTcpSocket staleSocket;
    staleSocket.connectToHost(QHostAddress::LocalHost, persistedPort);
    const bool boundToPersisted = staleSocket.waitForConnected(1000);
    staleSocket.disconnectFromHost();
    EXPECT_FALSE(boundToPersisted)
        << "engine is listening on the PERSISTED port " << persistedPort
        << " instead of the CLI port " << cliPort << "\n"
        << debugLogTail(tmpDir.path()).toStdString();

    EXPECT_TRUE(stopEngine(proc)) << "engine did not exit after terminate/kill";
    EXPECT_EQ(proc.state(), QProcess::NotRunning);
}

// Case 3: an explicit --mcp-http whose port is ALREADY TAKEN must fail fast
// (non-zero exit) instead of running alive-but-mute, where a stale engine on
// that port silently answers every later tool call.
TEST(HeadlessMcpHttpPort, ExitsNonZeroWhenTheCliPortIsTaken)
{
    const QString exe = enginePath();
    ASSERT_TRUE(QFile::exists(exe))
        << "engine binary not found at " << exe.toStdString()
        << " — this is an environmental failure (build HDAW_headless first), not a pass";

    const quint16 mcpPort = probeFreePort();
    ASSERT_NE(mcpPort, 0) << "could not probe a free MCP HTTP port";
    quint16 wsPort = probeFreePort();
    ASSERT_NE(wsPort, 0) << "could not probe a free WebSocket port";
    if (wsPort == mcpPort)
        wsPort = probeFreePort();

    // Hold the MCP HTTP port with our own listener for the whole case.
    QTcpServer blocker;
    ASSERT_TRUE(blocker.listen(QHostAddress::LocalHost, mcpPort))
        << "could not occupy port " << mcpPort << " for the test";

    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());

    // Declared BEFORE the process: destroyed LAST (see case 1).
    NativeMcpHttpSettingsGuard settingsGuard;

    QProcess proc;
    ChildGuard guard(proc);
    startEngine(proc, tmpDir, mcpPort, wsPort);

    ASSERT_TRUE(proc.waitForStarted(kStartDeadlineMs))
        << "failed to start " << exe.toStdString();

    // The fail-fast must land well inside the deadline; a live-but-mute engine
    // (the pre-fix behaviour) never finishes and fails here.
    ASSERT_TRUE(proc.waitForFinished(kExitDeadlineMs))
        << "engine lingered alive-but-mute with --mcp-http on an occupied port "
        << mcpPort << " (state=" << int(proc.state()) << ")\n"
        << debugLogTail(tmpDir.path()).toStdString();

    EXPECT_EQ(proc.exitStatus(), QProcess::NormalExit)
        << "engine should exit cleanly with a non-zero code, not crash\n"
        << debugLogTail(tmpDir.path()).toStdString();
    EXPECT_NE(proc.exitCode(), 0)
        << "engine must exit NON-ZERO when its explicit --mcp-http-port is taken\n"
        << debugLogTail(tmpDir.path()).toStdString();

    blocker.close();
}
