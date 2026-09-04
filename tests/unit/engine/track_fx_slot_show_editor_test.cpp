#include <gtest/gtest.h>
#include "engine/TrackFXSlot.h"
#include "proxy/ProxyProcessManager.h"
#include "proxy/ProxyCommon.h"
#include "proxy/PluginProxySlot.h"
#include <chrono>
#include <memory>
#include <thread>

using namespace proxy;

// The non-isolated showEditor path (in-process DocumentWindow) is dead in
// production: plugins always run isolated, and TrackFXSlot::showEditor routes
// the isolated branch through the child-process pipe — it sends SHOW_EDITOR,
// waits for the child's SHOW_EDITOR ack, and marks the remote editor open
// (isEditorOpen() reads remoteEditorOpen for isolated slots). The real plugin
// HWND opens INSIDE the child process; this test asserts the request is
// delivered and acknowledged, never that a local window exists. That also
// keeps the old WM_IME_SETCONTEXT/worker-thread HWND hazard out of the test
// process: no parent-side window is ever created.
//
// The __passthrough__ child keeps the child side inert too: its createEditor()
// returns nullptr, so even the child's async openEditorOnGUIThread() creates
// no window — the SHOW_EDITOR/CLOSE_EDITOR protocol still round-trips fully.
TEST(TrackFXSlotShowEditor, ShowEditorSendsPipeRequestToChild) {
    ProxyProcessManager mgr;
    const uint32_t slotId = 9310;

    ASSERT_TRUE(mgr.spawnPluginHost("__passthrough__", slotId));
    for (int i = 0; i < 100 && !mgr.isAlive(slotId); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(mgr.isAlive(slotId)) << "child should be alive after spawn";

    auto plugin = std::make_unique<PluginProxySlot>(mgr, slotId, "EditorTest");
    HDAW::TrackFXSlot slot(std::move(plugin), "EditorTest", /*isIsolated=*/true);

    EXPECT_TRUE(slot.isIsolated());
    EXPECT_FALSE(slot.isEditorOpen());

    // Isolated path: SHOW_EDITOR -> child ack -> remoteEditorOpen = true.
    slot.showEditor();
    EXPECT_TRUE(slot.isEditorOpen())
        << "showEditor should mark the remote editor open after the child ack";

    // Re-open while the remote editor is marked open must early-out
    // (no second SHOW_EDITOR reaches the pipe, state stays open).
    slot.showEditor();
    EXPECT_TRUE(slot.isEditorOpen());

    // CLOSE_EDITOR -> child ack -> remoteEditorOpen = false.
    slot.closeEditor();
    EXPECT_FALSE(slot.isEditorOpen());

    mgr.killPluginHost(slotId, KillMode::KillHard);
}
