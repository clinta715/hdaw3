#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include "engine/PluginManager.h"
#include "common/PluginBinaryInfo.h"

#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#define PLUGIN_MANAGER_SCAN_WINDOWS_ONLY 1
#endif

// Sets an env var for the duration of a scope, restoring a sane default
// afterwards so subsequent tests in the same process are unaffected.
class ScopedEnvOverride
{
public:
    ScopedEnvOverride(const char* key, const char* value, const char* restoreValue)
        : key(key), restore(restoreValue)
    {
#ifdef _WIN32
        _putenv_s(key, value);
#else
        setenv(key, value, 1);
#endif
    }

    ~ScopedEnvOverride()
    {
#ifdef _WIN32
        _putenv_s(key.toRawUTF8(), restore.toRawUTF8());
#else
        setenv(key.toRawUTF8(), restore.toRawUTF8(), 1);
#endif
    }

private:
    juce::String key;
    juce::String restore;
};

static juce::File exeDirectory()
{
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getParentDirectory();
}

class PluginManagerScan : public ::testing::Test
{
protected:
    void SetUp() override
    {
        tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                      .getChildFile("hdaw_plugin_manager_scan_test");
        tempDir.deleteRecursively();
        tempDir.createDirectory();
    }

    void TearDown() override
    {
        tempDir.deleteRecursively();
    }

    juce::File tempDir;
};

TEST_F(PluginManagerScan, BlacklistRoundTripViaManager)
{
    // Bug 1 regression: saveBlacklist writes <BLACKLIST> as the XML document
    // root, and loadBlacklist must accept that form (previously it only
    // looked for a <BLACKLIST> CHILD, so the blacklist never reloaded).
    auto tmp = tempDir.getChildFile("blacklist_roundtrip.xml");

    HDAW::PluginManager pm;
    pm.setBlacklistFileForTesting(tmp);
    pm.blacklistPlugin("C:\\VST3\\evil.vst3", "crash");
    EXPECT_TRUE(pm.isBlacklisted("C:\\VST3\\evil.vst3"));
    EXPECT_EQ(pm.getBlacklistReason("C:\\VST3\\evil.vst3"), "crash");
    EXPECT_TRUE(tmp.existsAsFile());

    // Fresh manager reloads from the same file — root-is-BLACKLIST form.
    HDAW::PluginManager pm2;
    pm2.setBlacklistFileForTesting(tmp);
    pm2.loadBlacklist();
    EXPECT_TRUE(pm2.isBlacklisted("C:\\VST3\\evil.vst3"));
    EXPECT_EQ(pm2.getBlacklistReason("C:\\VST3\\evil.vst3"), "crash");
}

TEST_F(PluginManagerScan, BlacklistLoadsLegacyWrapperForm)
{
    // Older files wrapped BLACKLIST inside a container element.
    auto tmp = tempDir.getChildFile("blacklist_legacy.xml");
    juce::XmlElement wrapper("KNOWNPLUGINS");
    {
        auto* bl = wrapper.createNewChildElement("BLACKLIST");
        auto* el = bl->createNewChildElement("PLUGIN");
        el->setAttribute("id", "C:\\VST3\\legacy.vst3");
        el->setAttribute("reason", "crash");
    }
    ASSERT_TRUE(wrapper.writeTo(tmp, {}));

    HDAW::PluginManager pm;
    pm.setBlacklistFileForTesting(tmp);
    pm.loadBlacklist();
    EXPECT_TRUE(pm.isBlacklisted("C:\\VST3\\legacy.vst3"));
    EXPECT_EQ(pm.getBlacklistReason("C:\\VST3\\legacy.vst3"), "crash");
}

static void writePeFile(const juce::File& file, const juce::uint8* data, size_t size)
{
    file.deleteFile();
    juce::MemoryBlock block(data, size);
    ASSERT_TRUE(file.replaceWithData(block.getData(), (int)block.getSize()));
}

static const juce::uint8* makePe(juce::uint16 machine, juce::uint8* buf, size_t& size)
{
    std::memset(buf, 0, 4096);
    buf[0] = 'M';
    buf[1] = 'Z';
    const juce::uint32 peOffset = 64;
    buf[0x3C] = (juce::uint8)(peOffset & 0xFF);
    buf[0x3D] = (juce::uint8)((peOffset >> 8) & 0xFF);
    buf[0x3E] = (juce::uint8)((peOffset >> 16) & 0xFF);
    buf[0x3F] = (juce::uint8)((peOffset >> 24) & 0xFF);
    buf[peOffset + 0] = 'P';
    buf[peOffset + 1] = 'E';
    buf[peOffset + 2] = 0;
    buf[peOffset + 3] = 0;
    buf[peOffset + 4] = (juce::uint8)(machine & 0xFF);
    buf[peOffset + 5] = (juce::uint8)((machine >> 8) & 0xFF);
    size = peOffset + 6;
    return buf;
}

TEST_F(PluginManagerScan, Is32BitPeImageDetected)
{
    juce::uint8 buf[4096];
    size_t size = 0;
    makePe(0x14C, buf, size); // IMAGE_FILE_MACHINE_I386

    auto file = tempDir.getChildFile("fake32bit.vst3");
    writePeFile(file, buf, size);

    EXPECT_TRUE(HDAW::is32BitPluginBinary(file));
}

TEST_F(PluginManagerScan, Is64BitPeImageNotFlagged)
{
    juce::uint8 buf[4096];
    size_t size = 0;
    makePe(0x8664, buf, size); // IMAGE_FILE_MACHINE_AMD64

    auto file = tempDir.getChildFile("fake64bit.vst3");
    writePeFile(file, buf, size);

    EXPECT_FALSE(HDAW::is32BitPluginBinary(file));
}

TEST_F(PluginManagerScan, NonPeFileNotFlagged)
{
    auto file = tempDir.getChildFile("notape.txt");
    ASSERT_TRUE(file.replaceWithText("hello world - this is not a PE image"));

    EXPECT_FALSE(HDAW::is32BitPluginBinary(file));

    auto missing = tempDir.getChildFile("does_not_exist.vst3");
    EXPECT_FALSE(missing.existsAsFile());
    EXPECT_FALSE(HDAW::is32BitPluginBinary(missing));
}

TEST_F(PluginManagerScan, FindPluginFilesEnumeratesBundles)
{
    // Serum2.vst3 / WOMBintro.vst3 ship as DIRECTORY bundles — findPluginFiles
    // must enumerate both single-file .vst3/.clap and .vst3 bundle dirs.
    auto singleFile = tempDir.getChildFile("SingleFile.vst3");
    singleFile.replaceWithText("not a real plugin");
    auto clapFile = tempDir.getChildFile("Thing.clap");
    clapFile.replaceWithText("not a real clap");
    auto bundle = tempDir.getChildFile("Bundle.vst3");
    bundle.getChildFile("Contents").getChildFile("x86_64-win").createDirectory();
    bundle.getChildFile("Contents").getChildFile("x86_64-win").getChildFile("Bundle.vst3").replaceWithText("bundle payload");
    auto ignoreTxt = tempDir.getChildFile("Readme.txt");
    ignoreTxt.replaceWithText("not a plugin");

    HDAW::PluginManager pm;
    auto found = pm.findPluginFiles({ tempDir.getFullPathName() });

    juce::StringArray names;
    for (const auto& f : found)
        names.add(f.getFullPathName());
    EXPECT_EQ(found.size(), 3) << names.joinIntoString(" | ");
    EXPECT_TRUE(names.contains(singleFile.getFullPathName()));
    EXPECT_TRUE(names.contains(clapFile.getFullPathName()));
    EXPECT_TRUE(names.contains(bundle.getFullPathName()));
    EXPECT_FALSE(names.contains(ignoreTxt.getFullPathName()));
}

#ifdef PLUGIN_MANAGER_SCAN_WINDOWS_ONLY
TEST_F(PluginManagerScan, ScanTimeoutBoundedAndKills)
{
    auto hangHelper = exeDirectory().getChildFile("scan_hang_helper.exe");
    if (!hangHelper.existsAsFile())
        GTEST_SKIP() << "scan_hang_helper.exe not built: " << hangHelper.getFullPathName().toRawUTF8();

    ScopedEnvOverride ov("HDAW_SCAN_PLUGIN_TIMEOUT_MS", "1500", "90000");

    HDAW::PluginManager pm;
    pm.setScannerExePathForTesting(hangHelper);

    auto t0 = juce::Time::getMillisecondCounter();
    auto r = pm.scanPluginIsolated("C:\\fake\\x.vst3");
    auto elapsed = juce::Time::getMillisecondCounter() - t0;

    // Bounded — the default 90s timeout must NOT have been applied.
    EXPECT_LT(elapsed, (juce::uint32)15000);
    EXPECT_TRUE(r.error.startsWith("Scanner timed out"));

    // Give the kill a grace period, then verify no scanner child survives.
    juce::Thread::sleep(2000);
    juce::ChildProcess probe;
    ASSERT_TRUE(probe.start("\"tasklist\" /FI \"IMAGENAME eq scan_hang_helper.exe\"",
                            juce::ChildProcess::wantStdOut));
    EXPECT_TRUE(probe.waitForProcessToFinish(10000));
    auto output = probe.readAllProcessOutput();
    EXPECT_FALSE(output.contains("scan_hang_helper"))
        << "kill landed but scan_hang_helper still running, tasklist output: "
        << output.toRawUTF8();
}
#endif

TEST_F(PluginManagerScan, ScanLoadFailureReported)
{
    auto scannerExe = exeDirectory().getChildFile("hdaw_plugin_scanner.exe");
    if (!scannerExe.existsAsFile())
        GTEST_SKIP() << "hdaw_plugin_scanner.exe not built: " << scannerExe.getFullPathName().toRawUTF8();

    ScopedEnvOverride ov("HDAW_SCAN_PLUGIN_TIMEOUT_MS", "20000", "90000");

    HDAW::PluginManager pm;
    pm.setScannerExePathForTesting(scannerExe);

    // Nonexistent plugin path → scanner exits 1 (load failure, not crash) —
    // exercises the non-timeout path: output read + exit-code branch.
    auto r = pm.scanPluginIsolated("C:\\nonexistent\\fake.vst3");
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(r.error.contains("Scanner exited with code"))
        << "unexpected error: " << r.error.toRawUTF8();
}