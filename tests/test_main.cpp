#include <gtest/gtest.h>
#include <QCoreApplication>

#include "common/MessagePumpThread.h"
#include "common/ScopedComInit.h"

// After the common headers: Qt's `slots` macro (pulled in by QCoreApplication
// above) would otherwise rewrite such a member name in those headers.
#include <QSettings>

// This runs FIRST, before ScopedComInit / MessagePumpThread / QCoreApplication
// (Gate 11 ordering: COM, pump thread, Qt app - do not disturb).
//
// Why: the engine's windowed-render scratch path comes from
// juce::File::getSpecialLocation(File::tempDirectory), which on Windows is
// Win32 GetTempPath() -> the process environment block (TMP/TEMP/USERPROFILE).
// A sandboxed agent shell can deny a CHILD process writes under %TEMP% while
// the shell itself and the repo tree stay writable, so every render/export/
// save test fails with a plausible-looking (but environmental)
// "export failed: Could not create output file". The harness therefore probes
// the current temp dir once and redirects TMP/TEMP to <cwd>/.tmp_tests/tmp/<pid>
// when it is unwritable. HDAW_TEST_TMP overrides the choice outright.
//
// IMPORTANT: the redirect sets BOTH the CRT environment (_putenv_s / setenv) and
// the Win32 process environment block (SetEnvironmentVariableW). _putenv_s alone
// updates only the CRT's private copy, which GetTempPath() - and therefore JUCE -
// does not read; the parent-visible workaround (TMP=... cmd) works because the
// variable is inherited in the PEB, so the harness must write there too.
//
// Widened (2026-09-25) to the engine's OTHER user-scope roots, so the same
// sandbox stops masquerading as a code regression:
//   * userApplicationDataDirectory - juce_Files_windows.cpp:721 maps it to
//     SHGetSpecialFolderPathW(CSIDL_APPDATA) (:146), i.e. the registry value
//     %USERPROFILE%\AppData\Roaming expanded against the process USERPROFILE.
//     The engine writes HDAW/section-templates, HDAW/patterns, HDAW/chains,
//     HDAW/libraries, HDAW/plugin_cache.xml, ... under it (see
//     AudioEngineCommands_Song.cpp:64, ChainLibrary.cpp:288,
//     PluginManager.cpp:96, FileLibraryManager.cpp:30). When that root is
//     unwritable the harness points USERPROFILE (plus APPDATA/LOCALAPPDATA) at
//     <cwd>/.tmp_tests/userdata. shell32 resolves its folder set on first use and
//     does NOT re-read the env afterwards (measured 2026-09-25: a fresh process
//     launched with USERPROFILE overridden resolves to the override, while an
//     in-process override after shell32's first query does not), so this must run
//     here, before anything else queries a shell folder.
//     The redirected root is EMPTY, so the harness ALSO seeds it (read mirror)
//     from the real %APPDATA%\HDAW before the redirect: plugin_cache*.xml,
//     preset_cache.xml, plugin_blacklist.xml, preferences.ron, libraries/,
//     chains/, section-templates/, patterns/, MIDI/. This restores READ FIDELITY
//     (the engine reads no plugin cache / library registry / user templates from
//     an empty root); it is not a proven failure fix: the 2026-09-25 4-shard run's
//     serial shard died mid-suite (hdaw_shard_131606_serial.log has no summary),
//     but a control run of that same 316-test filter against an EMPTY root
//     completes rc 0 (232 s), so the death was run-level (load/concurrency), not
//     attributable to the empty root. recordings/ (100 MB+ volatile WAVs) is
//     deliberately not mirrored; writes stay in the scratch tree.
//   * QSettings - the engine uses `QSettings s;` with no format/path override
//     (AudioEngine.cpp:198/345/445, RaveService.cpp:46, PluginManager.cpp:78,
//     Router_Audio.cpp:293, ...), which on Windows is the registry
//     (HKCU\Software\<org>\<app>): a write outside the working tree (denied) and
//     one store shared by every concurrent shard. The harness therefore forces
//     the TEST PROCESS onto an isolated INI store under
//     <cwd>/.tmp_tests/settings/<pid> - process-wide, so the engine and the tests
//     still share ONE consistent settings mechanism, just not the machine's.
//     Production entry points (src/main.cpp, src/main_headless.cpp) do not do
//     this and keep the native format.
//     MEASURED 2026-09-25 (native store, isolation disabled): the failure mode is
//     WRITE DENIAL, not absent values. `reg add HKCU\Software\HDAW\HDAW` fails
//     with "Access is denied" under the sandbox, yet `reg query` reads the
//     machine's persisted keys - QSettings::setValue/remove silently no-op while
//     value() still returns the machine's (and the live engine's) stale keys. That
//     is exactly the formerly-failing signature: RaveSettings read back the
//     dev-box's modelDirs/repo paths, FrontendServer read the live engine's
//     enabled=true, and SettingsScriptPathWinsOverCwdFallback fell through to the
//     dev-tree script. The writable, process-private INI is therefore the fix;
//     mirroring the native VALUES would re-import that stale shared state and is
//     not done.

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

std::string readEnvVar (const char* name)
{
#ifdef _WIN32
    const DWORD len = ::GetEnvironmentVariableA (name, nullptr, 0);
    if (len == 0)
        return {};
    std::vector<char> buf (static_cast<size_t> (len), '\0');
    const DWORD got = ::GetEnvironmentVariableA (name, buf.data(), len);
    if (got == 0 || got >= len)
        return {};
    return std::string (buf.data(), static_cast<size_t> (got));
#else
    if (const char* v = std::getenv (name); v != nullptr)
        return std::string (v);
    return {};
#endif
}

void writeEnvVar (const char* name, const std::string& value)
{
    // CRT copy: what getenv / std::filesystem / spawned CRT children see.
#ifdef _WIN32
    _putenv_s (name, value.c_str());
#else
    ::setenv (name, value.c_str(), 1);
#endif

#ifdef _WIN32
    // Win32 process environment block: what GetTempPath()/JUCE and any
    // CreateProcess child (plugin host, proxy) actually read and inherit.
    const int wideLen = ::MultiByteToWideChar (CP_ACP, 0, name, -1, nullptr, 0);
    const int wideVal = ::MultiByteToWideChar (CP_ACP, 0, value.c_str(), -1, nullptr, 0);
    if (wideLen > 0 && wideVal > 0)
    {
        std::vector<wchar_t> wName (static_cast<size_t> (wideLen));
        std::vector<wchar_t> wValue (static_cast<size_t> (wideVal));
        ::MultiByteToWideChar (CP_ACP, 0, name, -1, wName.data(), wideLen);
        ::MultiByteToWideChar (CP_ACP, 0, value.c_str(), -1, wValue.data(), wideVal);
        ::SetEnvironmentVariableW (wName.data(), wValue.data());
    }
#endif
}

// The default temp dir, resolved WITHOUT juce::File (so the probe is
// independent of the special-location path we are trying to validate):
// TMP, then TEMP, then Win32 GetTempPathW.
std::filesystem::path resolveDefaultTempDir()
{
    for (const char* name : { "TMP", "TEMP" })
    {
        const std::string v = readEnvVar (name);
        if (! v.empty())
            return std::filesystem::path (v);
    }

#ifdef _WIN32
    std::vector<wchar_t> buf (32768);
    const DWORD n = ::GetTempPathW (static_cast<DWORD> (buf.size()), buf.data());
    if (n > 0 && n < buf.size())
        return std::filesystem::path (std::wstring (buf.data(), static_cast<size_t> (n)));
#endif

    return {};
}

std::filesystem::path makeAbsolute (const std::filesystem::path& p)
{
    if (p.empty())
        return p;
    std::error_code ec;
    const auto abs = std::filesystem::absolute (p, ec);
    return ec ? p : abs.lexically_normal();
}

// Create a probe file in `dir`, read it back, delete it. Returns false (with a
// short reason in `why`) when the directory is missing/uncreatable or the
// round-trip fails - exactly the sandbox signature.
bool probeDirIsWritable (const std::filesystem::path& dir, std::string& why)
{
    if (dir.empty())
    {
        why = "temp dir could not be resolved";
        return false;
    }

    std::error_code ec;
    if (! std::filesystem::exists (dir, ec))
    {
        std::filesystem::create_directories (dir, ec);
        if (ec)
        {
            why = "create_directories failed: " + ec.message();
            return false;
        }
    }

    const unsigned long pid =
#ifdef _WIN32
        static_cast<unsigned long> (::GetCurrentProcessId());
#else
        0ul;
#endif
    const auto probe = dir / ("hdaw_tmp_probe_" + std::to_string (pid) + ".txt");
    const char payload[] = "hdaw tmp probe\n";

    {
#ifdef _WIN32
        const auto wprobe = probe.wstring();
        std::FILE* f = _wfopen (wprobe.c_str(), L"wb");
#else
        std::FILE* f = std::fopen (probe.string().c_str(), "wb");
#endif
        if (f == nullptr)
        {
            why = "cannot open probe file for write";
            return false;
        }
        const size_t wrote = std::fwrite (payload, 1, sizeof (payload), f);
        std::fclose (f);
        if (wrote != sizeof (payload))
        {
            std::filesystem::remove (probe, ec);
            why = "short write to probe file";
            return false;
        }
    }

    {
#ifdef _WIN32
        const auto wprobe = probe.wstring();
        std::FILE* f = _wfopen (wprobe.c_str(), L"rb");
#else
        std::FILE* f = std::fopen (probe.string().c_str(), "rb");
#endif
        if (f == nullptr)
        {
            std::filesystem::remove (probe, ec);
            why = "cannot re-open probe file for read";
            return false;
        }
        char readback[sizeof (payload)] = {};
        const size_t got = std::fread (readback, 1, sizeof (readback), f);
        std::fclose (f);
        if (got != sizeof (payload))
        {
            std::filesystem::remove (probe, ec);
            why = "probe readback mismatch";
            return false;
        }
    }

    std::filesystem::remove (probe, ec);
    return true;
}

void report (const std::string& line)
{
    std::printf ("[test_main] %s\n", line.c_str());
    std::fflush (stdout);
}

// Decide once, at the very top of main, which temp dir the suite uses.
void configureTestTempDir()
{
    // 1. Explicit override wins outright - used by shard runners / CI.
    const std::string overrideDir = readEnvVar ("HDAW_TEST_TMP");
    if (! overrideDir.empty())
    {
        const auto abs = makeAbsolute (std::filesystem::path (overrideDir));
        std::error_code ec;
        std::filesystem::create_directories (abs, ec);
        const std::string value = abs.string();
        writeEnvVar ("TMP", value);
        writeEnvVar ("TEMP", value);
        report ("temp dir = " + value + " (HDAW_TEST_TMP override)");
        return;
    }

    // 2. Probe the current temp dir: create if needed, write+read+delete.
    const auto current = makeAbsolute (resolveDefaultTempDir());
    std::string why;
    if (probeDirIsWritable (current, why))
    {
        // 3. Normal environment: leave TMP/TEMP exactly as inherited.
        report ("temp dir = " + current.string() + " (default, verified writable)");
        return;
    }

    // 4. Unwritable (sandbox): redirect into the working tree, which the
    //    sandbox does allow. .tmp_* is gitignored.
    std::error_code ec;
    const auto cwd = std::filesystem::current_path (ec);
    if (ec)
    {
        report ("WARNING: default temp dir " + current.string() + " unusable (" + why
                + ") and current_path() failed: " + ec.message());
        return;
    }

    // Per-PID: a shard-parallel run has many processes sharing .tmp_tests, and a
    // suite's own deleteRecursively on a SHARED temp subdir races a sibling
    // shard's writes (measured 2026-09-25: FileLibraryTest.* 3 failures in a
    // 4-shard run, 40/40 solo). The settings store and the pid-tagged render
    // scratch already do this; the temp ROOT must too. The user-data mirror root
    // stays deliberately shared (atomic writes, read-only for tests).
    const unsigned long pid =
#ifdef _WIN32
        static_cast<unsigned long> (::GetCurrentProcessId());
#else
        0ul;
#endif
    const auto fallback = makeAbsolute (cwd / ".tmp_tests" / "tmp" / std::to_string (pid));
    std::string fallbackWhy;
    if (! probeDirIsWritable (fallback, fallbackWhy))
    {
        report ("WARNING: default temp dir " + current.string() + " unusable (" + why
                + ") and fallback " + fallback.string() + " unusable (" + fallbackWhy + ")");
        return;
    }

    const std::string value = fallback.string();
    writeEnvVar ("TMP", value);
    writeEnvVar ("TEMP", value);
    report ("temp dir redirected to " + value + " (default unwritable: " + why + ")");
}

unsigned long currentProcessId()
{
#ifdef _WIN32
    return static_cast<unsigned long> (::GetCurrentProcessId());
#else
    return 0ul;
#endif
}

// --- User-data READ mirror ---------------------------------------------------
//
// The redirected user-data root starts EMPTY. The engine reads its user-scope
// assets (plugin_cache.xml at AudioEngine construction, libraries/registry.json +
// sidecars via FileLibraryManager(), chains/section-templates/patterns), so an
// empty root is a fidelity gap: tests and the engine see no cache, no registry
// and no user templates. The harness therefore seeds the scratch root from the
// REAL %APPDATA%\HDAW, resolved BEFORE the redirect, copying read-side assets
// only - never recordings/.
//
// Measured effect: NEUTRAL on every solo gate. The 2026-09-25 4-shard serial shard
// died mid-suite, but a control run of that same 316-test filter against an EMPTY
// root completes rc 0, so the mirror is not a proven failure fix - it restores
// read fidelity and is kept for that reason.
//
// Resilience: a missing source is not an error; an individual copy failure is
// counted/logged and never aborts. Files are written to a sibling temp name and
// moved into place, so a concurrent shard process (the scratch root is shared by
// all shards of a run) can never observe a torn file. A file whose destination
// already matches the source size is skipped, so the ~95 MB libraries/ tree is
// only re-copied when it actually changed since the last seed.
struct MirrorStats
{
    size_t copied = 0;              // files actually written
    size_t skipped = 0;             // destination already current
    size_t failures = 0;            // copy/move errors (logged, non-fatal)
    unsigned long long bytes = 0;   // bytes written by this process
};

bool destHasSourceSize (const std::filesystem::path& src, const std::filesystem::path& dst)
{
    std::error_code ec;
    if (! std::filesystem::is_regular_file (dst, ec))
        return false;
    const auto srcSize = std::filesystem::file_size (src, ec);
    if (ec) return false;
    const auto dstSize = std::filesystem::file_size (dst, ec);
    if (ec) return false;
    return srcSize == dstSize;
}

void copyFileMirrored (const std::filesystem::path& src, const std::filesystem::path& dst,
                       MirrorStats& stats)
{
    std::error_code ec;
    std::error_code sizeEc;
    const auto srcSize = std::filesystem::file_size (src, sizeEc);
    if (sizeEc)
        return; // vanished/undreadable source: not an error

    if (destHasSourceSize (src, dst))
    {
        ++stats.skipped;
        return;
    }

    std::filesystem::create_directories (dst.parent_path(), ec);
    if (ec) { ++stats.failures; return; }

    const auto tmp = dst.parent_path()
                   / (dst.filename().string() + ".mirrortmp." + std::to_string (currentProcessId()));
    std::filesystem::copy_file (src, tmp, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) { ++stats.failures; return; }

#ifdef _WIN32
    if (::MoveFileExW (tmp.wstring().c_str(), dst.wstring().c_str(), MOVEFILE_REPLACE_EXISTING) == 0)
    {
        std::error_code rmEc;
        std::filesystem::remove (tmp, rmEc);
        ++stats.failures;
        return;
    }
#else
    std::filesystem::rename (tmp, dst, ec);
    if (ec) { ++stats.failures; return; }
#endif

    ++stats.copied;
    stats.bytes += static_cast<unsigned long long> (srcSize);
}

// Copy `src` (file or directory tree) into `dst`, best-effort.
void copyTreeMirrored (const std::filesystem::path& src, const std::filesystem::path& dst,
                       MirrorStats& stats)
{
    std::error_code ec;
    if (! std::filesystem::exists (src, ec))
        return; // missing source asset: fine

    if (std::filesystem::is_regular_file (src, ec))
    {
        copyFileMirrored (src, dst, stats);
        return;
    }
    if (! std::filesystem::is_directory (src, ec))
        return;

    for (std::filesystem::recursive_directory_iterator it (
             src, std::filesystem::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment (ec))
    {
        if (ec)
            break;
        std::error_code fileEc;
        if (! it->is_regular_file (fileEc))
            continue;
        const auto name = it->path().filename().string();
        if (name.rfind (".registry_temp", 0) == 0) // stale registry scratch in the real tree
            continue;
        const auto rel = std::filesystem::relative (it->path(), src, fileEc);
        if (fileEc)
            continue;
        copyFileMirrored (it->path(), dst / rel, stats);
    }
}

// Seed the scratch user-data root with the real user-scope assets. Only the
// read-sensible caches/registries/templates are copied; recordings/ (100 MB+ of
// volatile WAVs) and every other bulk tree are deliberately left behind.
void mirrorUserDataAssets (const std::filesystem::path& realRoaming,
                           const std::filesystem::path& scratchRoaming)
{
    const auto srcRoot = realRoaming / "HDAW";
    const auto dstRoot = scratchRoaming / "HDAW";

    std::error_code ec;
    if (! std::filesystem::is_directory (srcRoot, ec))
    {
        report ("user data mirror skipped: source " + srcRoot.string() + " is not a directory");
        return;
    }

    std::filesystem::create_directories (dstRoot, ec);

    MirrorStats stats;

    // Top-level caches: plugin_cache.xml and any sibling cache the engine reads
    // or rewrites, plus the small preference/blacklist/pattern-index files.
    for (std::filesystem::directory_iterator it (srcRoot, ec), end; it != end; it.increment (ec))
    {
        if (ec)
            break;
        std::error_code fileEc;
        if (! it->is_regular_file (fileEc))
            continue;
        const auto name = it->path().filename().string();
        const bool wanted = name.rfind ("plugin_cache", 0) == 0
                         || name == "preset_cache.xml"
                         || name == "plugin_blacklist.xml"
                         || name == "preferences.ron";
        if (wanted)
            copyFileMirrored (it->path(), dstRoot / name, stats);
    }

    // Registry/template/pattern trees. section-templates may not exist.
    for (const char* dirName : { "libraries", "chains", "section-templates", "patterns", "MIDI" })
        copyTreeMirrored (srcRoot / dirName, dstRoot / dirName, stats);

    std::string summary = "user data mirror = " + std::to_string (stats.copied) + " files, "
                        + std::to_string (stats.bytes) + " bytes copied ("
                        + std::to_string (stats.skipped) + " already current) from "
                        + srcRoot.string();
    if (stats.failures > 0)
        summary += " [" + std::to_string (stats.failures) + " copy failure(s), non-fatal]";
    report (summary);
}

// Point the process' profile at `profile` so that JUCE's
// File::getSpecialLocation (userApplicationDataDirectory) - resolved by shell32 as
// the registry value %USERPROFILE%\AppData\Roaming - lands inside the working
// tree. Sets BOTH the CRT and the Win32 process environment (see writeEnvVar):
// shell32's folder lookup reads the process env block on its FIRST query.
std::filesystem::path redirectUserScope (const std::filesystem::path& profile, const std::string& reason)
{
    const auto roaming = profile / "AppData" / "Roaming";
    const auto local   = profile / "AppData" / "Local";

    std::error_code ec;
    std::filesystem::create_directories (roaming, ec);
    std::filesystem::create_directories (local, ec);

    writeEnvVar ("USERPROFILE", profile.string());
    writeEnvVar ("APPDATA", roaming.string());
    writeEnvVar ("LOCALAPPDATA", local.string());

    report ("user data dir redirected to " + roaming.string() + " (" + reason + ")");
    return roaming;
}

// Decide once, at the very top of main, which user-scope root the suite uses.
void configureTestUserDataDir()
{
    // The REAL roaming root, resolved before any redirect overwrites APPDATA /
    // USERPROFILE: it is the source of the read mirror above.
    const auto realRoaming = []() -> std::filesystem::path {
        const std::string appData = readEnvVar ("APPDATA");
        if (! appData.empty())
            return makeAbsolute (std::filesystem::path (appData));
        const std::string up = readEnvVar ("USERPROFILE");
        if (! up.empty())
            return makeAbsolute (std::filesystem::path (up)) / "AppData" / "Roaming";
        return {};
    }();

    auto redirectAndMirror = [&realRoaming] (const std::filesystem::path& profile,
                                             const std::string& reason) {
        const auto scratchRoaming = redirectUserScope (profile, reason);
        if (! realRoaming.empty() && realRoaming != scratchRoaming)
            mirrorUserDataAssets (realRoaming, scratchRoaming);
    };

    // 1. Explicit override: HDAW_TEST_USERDATA is the PROFILE root (what USERPROFILE
    //    becomes); the engine's userApplicationDataDirectory is <profile>/AppData/Roaming.
    const std::string overrideRoot = readEnvVar ("HDAW_TEST_USERDATA");
    if (! overrideRoot.empty())
    {
        redirectAndMirror (makeAbsolute (std::filesystem::path (overrideRoot)),
                           "HDAW_TEST_USERDATA override");
        return;
    }

    // 2. Probe the root the engine actually writes: <USERPROFILE>/AppData/Roaming.
    const auto profile = makeAbsolute (std::filesystem::path (readEnvVar ("USERPROFILE")));
    const auto userData = profile / "AppData" / "Roaming";
    std::string why;
    if (! profile.empty() && probeDirIsWritable (userData, why))
    {
        // 3. Normal environment: leave the profile exactly as inherited.
        report ("user data dir = " + userData.string() + " (default, verified writable)");
        return;
    }
    if (profile.empty())
        why = "USERPROFILE is not set";

    // 4. Unwritable (sandbox): redirect into the working tree, as for temp.
    std::error_code ec;
    const auto cwd = std::filesystem::current_path (ec);
    if (ec)
    {
        report ("WARNING: user data dir " + userData.string() + " unusable (" + why
                + ") and current_path() failed: " + ec.message());
        return;
    }

    redirectAndMirror (makeAbsolute (cwd / ".tmp_tests" / "userdata"),
                       "default unwritable: " + why);
}

// Force this TEST PROCESS onto an isolated INI-backed QSettings store inside the
// working tree (see the header comment). MUST run before any QSettings object is
// constructed - including before QCoreApplication, which is why it is called from
// the top of main. Production entry points keep the native (registry) format.
void configureTestSettingsStore()
{
    std::error_code ec;
    const auto cwd = std::filesystem::current_path (ec);
    if (ec)
    {
        report ("WARNING: settings store not isolated, current_path() failed: " + ec.message());
        return;
    }

    const auto dir = makeAbsolute (cwd / ".tmp_tests" / "settings")
                         / std::to_string (currentProcessId());
    std::filesystem::create_directories (dir, ec);
    if (ec)
    {
        report ("WARNING: settings store not isolated, create_directories failed: " + ec.message());
        return;
    }

    const QString qdir = QString::fromStdString (dir.string());
    QSettings::setDefaultFormat (QSettings::IniFormat);
    QSettings::setPath (QSettings::IniFormat, QSettings::UserScope, qdir);
    // Reads fall back from UserScope to SystemScope; point that at the same scratch
    // store so a fallback read can neither see the machine's keys nor hit a denied write.
    QSettings::setPath (QSettings::IniFormat, QSettings::SystemScope, qdir);

    report ("settings store = " + dir.string() + " (QSettings IniFormat, this process only)");
}

} // namespace

int main(int argc, char** argv) {
    configureTestTempDir();
    configureTestUserDataDir();
    configureTestSettingsStore();

    // COM init is host-app responsibility for JUCE 8's WASAPI (see
    // common/ScopedComInit.h). Tests construct AudioEngine / touch
    // AudioDeviceManager; without COM the WASAPI scan returns empty.
    static HDAW::ScopedComInit comInit;
    (void) comInit;

    // MUST precede any other JUCE construction: the pump thread wins
    // MessageManager messageThreadId + the hidden message window on first
    // getInstance() (see MessagePumpThread.h), so AudioProcessorGraph render
    // sequences and AsyncUpdaters can bake during tests (headers, exports,
    // live render) without a GUI message loop. Without this, export graphs
    // processBlock falls into the audio.clear() early-out (silence).
    HDAW::MessagePumpThread::start();

    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
