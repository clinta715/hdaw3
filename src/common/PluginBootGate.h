#pragma once
// P3-b (2026-09-30): "is this plugin slot still BOOTING, or is it broken?"
//
// Measured context (ion_rift session): an isolated plugin child needs seconds to
// become usable — the emulated devices run an OS warmup that legitimately holds
// processBlock for ~12 s (PluginHost's kIdleWarmBlocks + warmupExpectedMs), and
// the CLAP is activated/prepared inside that window. Until it finishes, the
// parent sees a slot whose instance exists but which publishes NO parameters:
// `list_fx_params` answers `{}` and every params-dependent call fails —
// byte-identical to "this plugin is broken". An agent therefore cannot tell
// "wait" from "give up", and a caller that gives up reports a hard failure for a
// slot that was merely booting.
//
// The fix is a BOUNDED WAIT on the observable the parent already has (parameter
// availability), not a protocol change: no new pipe message, no new slot state,
// no payload-shape change (the two param surfaces deliberately spell their rows
// differently — `index` vs `paramIndex` — so unifying them was NOT the fix).
//
//   ready   — the observable is there
//   booting — not there yet, budget still open (keep waiting / retry)
//   broken  — not there after the whole budget (a real failure: check
//             get_fx_capture_status and the plugin-host log)
//
// The decision, the poll loop and the failure text are all pure (a probe
// callback + numbers), so every branch is unit-testable without a plugin, a
// child process, or a hang.
//
// Header-only (all inline), so no CMake source registration is needed.

#include <chrono>
#include <string>
#include <thread>

namespace HDAW {

// A deliberate, heavyweight operation (a preset load, a capture) may wait out the
// whole measured warmup. Measured 2026-09-30: ~12 s before the child publishes
// anything usable, so 15 s covers it with margin.
inline constexpr int kPluginBootBudgetMs = 15000;

// A READ (list_fx_params / pluginParam.getParams) must stay responsive: an
// empty list is worth a short wait, not a 15 s stall — an agent can retry.
inline constexpr int kPluginReadBudgetMs = 1500;

inline constexpr int kPluginBootPollMs = 250;

enum class PluginBootState
{
    Ready,     // the observable is available
    Booting,   // not yet, and the budget is still open
    Broken     // not after the whole budget
};

// What one bounded wait observed.
struct PluginBootWait
{
    bool ready = false;   // the probe returned true within the budget
    int waitedMs = 0;     // wall time spent (0 = the first probe succeeded)
};

// Pure decision. `ready` is the observable; `waitedMs` is how much of `budgetMs`
// the caller was willing to spend.
inline PluginBootState pluginBootState(bool ready, int waitedMs, int budgetMs)
{
    if (ready) return PluginBootState::Ready;
    return waitedMs < budgetMs ? PluginBootState::Booting : PluginBootState::Broken;
}

inline const char* pluginBootStateName(PluginBootState state)
{
    switch (state)
    {
        case PluginBootState::Ready:   return "ready";
        case PluginBootState::Booting: return "booting";
        case PluginBootState::Broken:  return "broken";
    }
    return "broken";
}

// The one line a caller reports when the budget expired. Names the state, the
// wait and the evidence to check, so "no params" is never the whole story.
inline std::string pluginBootFailureText(const char* what, int waitedMs, int budgetMs)
{
    return std::string(what) + ": no parameters after " + std::to_string(waitedMs)
         + " ms (budget " + std::to_string(budgetMs)
         + " ms) - the isolated plugin slot is still booting or broken; retry, or "
           "check get_fx_capture_status and the plugin-host log";
}

// Bounded poll: calls `probe()` (cheap, side-effect free, must not block) until
// it returns true or the budget expires. The sleep is on the CALLING thread —
// every caller is a command/MCP/render thread, never the audio thread (the same
// contract as captureFxSlotState's existing retry loop). A non-positive budget
// probes exactly once.
template <typename Probe>
inline PluginBootWait waitForPluginReady(Probe&& probe, int budgetMs = kPluginBootBudgetMs,
                                         int pollMs = kPluginBootPollMs)
{
    const auto start = std::chrono::steady_clock::now();
    const auto elapsedMs = [&start]() {
        return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count());
    };

    if (probe()) return PluginBootWait{ true, 0 };
    if (budgetMs <= 0) return PluginBootWait{ false, elapsedMs() };

    const int step = pollMs > 0 ? pollMs : 1;
    while (elapsedMs() < budgetMs)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(step));
        if (probe()) return PluginBootWait{ true, elapsedMs() };
    }
    return PluginBootWait{ false, elapsedMs() };
}

// Convenience for the read paths: wait out a booting child so a slot that is
// merely warming up answers with its real parameters instead of `{}`. Returns
// the state the wait ended in; `outWaitedMs` receives the wall time spent.
template <typename Probe>
inline PluginBootState awaitPluginParams(Probe&& probe, int budgetMs = kPluginReadBudgetMs,
                                        int* outWaitedMs = nullptr)
{
    const PluginBootWait w = waitForPluginReady(probe, budgetMs);
    if (outWaitedMs != nullptr) *outWaitedMs = w.waitedMs;
    return pluginBootState(w.ready, w.waitedMs, budgetMs);
}

} // namespace HDAW
