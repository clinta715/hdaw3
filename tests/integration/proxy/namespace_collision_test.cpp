#include <gtest/gtest.h>
#include "proxy/ProxyProcessManager.h"
#include "proxy/ProxyPipe.h"
#include "proxy/ProxySharedMemory.h"
#include "engine/PluginManager.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <string>

// Gate A: every ProxyProcessManager owns a unique OS name namespace by
// construction (pid hex + process-wide instance counter), so two managers in
// one process can never collide on pipes/shm/state files even though their
// slot-id counters both start at 1.
TEST(ProxyNamespace, ManagersGetDistinctNamespaces) {
    proxy::ProxyProcessManager a;
    proxy::ProxyProcessManager b;

    EXPECT_NE(a.getNamePrefix(), b.getNamePrefix());
    EXPECT_NE(a.makePipeName(1), b.makePipeName(1));
    EXPECT_NE(a.makeShmName(1), b.makeShmName(1));
}

#if HDAW_PLUGIN_ISOLATION

TEST(ProxyNamespace, PluginManagersGetDistinctNamespaces) {
    juce::ScopedJuceInitialiser_GUI juceInit;

    HDAW::PluginManager a;
    HDAW::PluginManager b;

    EXPECT_NE(a.getProxyNamespacePrefix(), b.getProxyNamespacePrefix());
}

TEST(ProxyNamespace, OfflineCopyHasExportDomainAndUniqueSuffix) {
    juce::ScopedJuceInitialiser_GUI juceInit;

    HDAW::PluginManager live;
    auto offline = HDAW::PluginManager::createOfflineCopy(live);
    ASSERT_NE(offline, nullptr);
    offline->setProxyNamespacePrefix("export_");

    EXPECT_NE(live.getProxyNamespacePrefix(), offline->getProxyNamespacePrefix());
    // juce::String has no std::string-style find()/npos in this JUCE;
    // indexOf returns -1 when the text is absent (equivalent semantics).
    EXPECT_NE(offline->getProxyNamespacePrefix().indexOf("export_"), -1)
        << "offline copy's prefix must carry the export_ domain label";
}

TEST(ProxyNamespace, SpawnBumpsSlotWhenPipeNameHeld) {
    // The squatter holds the manager's would-be slot-1 pipe name (PipeServer
    // creates with nMaxInstances=1, so a second instance of the same name
    // fails). spawnPluginHost's first CreateNamedPipeA for that name fails —
    // the held-name GetLastError() code is logged by spawnPluginHost as
    // "PipeServer::start() FAILED for ... error=N" — and it bumps the slot id
    // and retries on a fresh name, so the spawn succeeds on slot 2. The bump
    // works regardless of the exact error code.
    proxy::ProxyProcessManager mgr;

    const std::string pipe1 = mgr.makePipeName(1);
    proxy::PipeServer squatter(pipe1);
    ASSERT_TRUE(squatter.start());

    uint32_t slot = 1;
    uint32_t actual = 0;
    ASSERT_TRUE(mgr.spawnPluginHost("__passthrough__", slot, &actual))
        << "spawn must succeed by bumping past the held slot-1 pipe name";
    EXPECT_EQ(actual, 2u);
    EXPECT_NE(actual, slot);
    EXPECT_TRUE(mgr.isAlive(actual));

    mgr.killPluginHost(actual, proxy::KillMode::KillHard);
    // squatter's dtor releases the held pipe name.
}

TEST(ProxyNamespace, SpawnBumpsSlotWhenShmNameHeld) {
    // The squatter holds the manager's would-be slot-1 shm name.
    // ShmRegion::create treats ERROR_ALREADY_EXISTS as a hard failure (never
    // silently opens a same-size existing region), so spawnPluginHost bumps to
    // a free slot id and retries.
    proxy::ProxyProcessManager mgr;

    proxy::ShmRegion squatter;
    ASSERT_TRUE(squatter.create(mgr.makeShmName(1),
        proxy::computeShmSize(proxy::kMaxShmChannels, proxy::kMaxShmBlockSize)));

    uint32_t slot = 1;
    uint32_t actual = 0;
    ASSERT_TRUE(mgr.spawnPluginHost("__passthrough__", slot, &actual))
        << "spawn must succeed by bumping past the held slot-1 shm name";
    EXPECT_EQ(actual, 2u);
    EXPECT_NE(actual, slot);
    EXPECT_TRUE(mgr.isAlive(actual));

    mgr.killPluginHost(actual, proxy::KillMode::KillHard);
}

#endif // HDAW_PLUGIN_ISOLATION
