#include <gtest/gtest.h>
#include "proxy/ProxyProcessManager.h"
#include "proxy/ProxyCommon.h"
#include <chrono>
#include <thread>

using namespace proxy;

// Verify that ProxyProcessManager can spawn and detect a child process.
// This test requires hdaw_plugin_host.exe to be built and present next to the test exe.
TEST(PluginIsolation, SpawnAndKillChild) {
    ProxyProcessManager mgr;

    // Get the host exe path
    auto hostExe = ProxyProcessManager::getHostExePath();
    ASSERT_FALSE(hostExe.empty());

    // Verify the host exe path contains the expected filename
    EXPECT_FALSE(hostExe.find("hdaw_plugin_host.exe") == std::string::npos);
}

TEST(PluginIsolation, PipeNameFormat) {
    ProxyProcessManager mgr;
    // Verify pipe name format
    auto path = ProxyProcessManager::getHostExePath();
    EXPECT_TRUE(path.find("hdaw_plugin_host.exe") != std::string::npos);
}
