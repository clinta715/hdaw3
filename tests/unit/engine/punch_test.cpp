#include <gtest/gtest.h>
#include "engine/AudioEngine.h"

TEST(PunchInOut, PunchDisabledByDefault)
{
    AudioEngine engine;
    engine.initialize();

    auto snap = engine.getReadModel().snapshot().transport;
    EXPECT_FALSE(snap.punchEnabled);
}

TEST(PunchInOut, SetPunchEnabled)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getTransportCommands();

    cmds.setPunchEnabled(true);
    EXPECT_TRUE(engine.getTransportManager().isPunchEnabled());

    cmds.setPunchEnabled(false);
    EXPECT_FALSE(engine.getTransportManager().isPunchEnabled());
}

TEST(PunchInOut, PunchEnabledAppearsInSnapshot)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getTransportCommands();

    cmds.setPunchEnabled(true);

    auto snap = engine.getReadModel().snapshot().transport;
    EXPECT_TRUE(snap.punchEnabled);
}
