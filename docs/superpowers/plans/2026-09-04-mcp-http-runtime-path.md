# MCP HTTP Runtime Path Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore MCP HTTP as a first-class runtime path that can be persisted in settings, toggled live from Preferences, and started from a headless launch flag without changing stdio MCP.

**Architecture:** Put the HTTP lifecycle behind `AudioEngine` so GUI RPC and launch flags both call the same start/stop/config methods. Expose the config through the existing `settings.*` frontend namespace, then wire PreferencesDialog to read and update that config live. Keep `mcp::McpServer` + `mcp::TransportHttp` as the actual implementation so `/mcp` stays exactly the same route already covered by tests.

**Tech Stack:** C++20, JUCE 8, Qt `QSettings`, React 19, TypeScript, Vitest, gtest.

---

### Task 1: Engine-owned MCP HTTP runtime + launch flags + integration test

**Files:**
- Modify: `src/common/SettingsKeys.h`
- Modify: `src/engine/AudioEngine.h`
- Modify: `src/engine/AudioEngine.cpp`
- Modify: `src/main.cpp`
- Modify: `src/main_headless.cpp`
- Modify: `src/frontend/router/Router_Project.cpp`
- Modify: `tests/unit/mcp/transport_http_test.cpp`

- [ ] **Step 1: Add the settings keys and engine API surface**

```cpp
namespace SettingsKeys
{
    inline constexpr auto kKeyMcpHttpEnabled = "mcp/httpEnabled";
    inline constexpr auto kKeyMcpHttpHost    = "mcp/httpHost";
    inline constexpr auto kKeyMcpHttpPort    = "mcp/httpPort";
}

class AudioEngine
{
public:
    struct McpHttpConfig
    {
        bool enabled = false;
        QString host;
        quint16 port = 8765;
        bool running = false;
        QString lastError;
    };

    McpHttpConfig getMcpHttpConfig() const;
    bool setMcpHttpConfig(bool enabled, const QString& host, quint16 port, QString* error = nullptr);
    void syncMcpHttpFromSettings();
};
```

- [ ] **Step 2: Implement one shared HTTP controller inside `AudioEngine`**

```cpp
bool AudioEngine::setMcpHttpConfig(bool enabled, const QString& host, quint16 port, QString* error)
{
    QSettings s;
    s.setValue(SettingsKeys::kKeyMcpHttpEnabled, enabled);
    s.setValue(SettingsKeys::kKeyMcpHttpHost, host.isEmpty() ? QStringLiteral("127.0.0.1") : host);
    s.setValue(SettingsKeys::kKeyMcpHttpPort, static_cast<int>(port));

    if (!enabled)
    {
        stopMcpHttp();
        return true;
    }

    return startMcpHttp(host, port, error);
}
```

- [ ] **Step 3: Auto-start from settings during engine init**

```cpp
void AudioEngine::initialize()
{
    // existing init work...
    syncMcpHttpFromSettings();
}
```

- [ ] **Step 4: Route the existing frontend settings RPC through the new engine API**

```cpp
if (m == "settings.getMcpHttpConfig") { return { false, toJsonObject(c.getMcpHttpConfig()) }; }
if (m == "settings.setMcpHttpConfig") {
    bool enabled; int port; std::string host;
    if (!requireBool(o, "enabled", enabled, nullptr) || !requireString(o, "host", host, nullptr) || !requireInt(o, "port", port, nullptr))
        return makeError(-32602, "enabled, host, port required");
    QString error;
    if (!c.setMcpHttpConfig(enabled, QString::fromStdString(host), static_cast<quint16>(port), &error))
        return makeError(-32000, error);
    return { false, QJsonValue::Null };
}
```

- [ ] **Step 5: Parse and persist `--mcp-http` in both launchers**

```cpp
const bool headlessMcpHttp = parseFlag(argc, argv, "--mcp-http");
const QString httpHost = parseValue(argc, argv, "--mcp-http-host")
    ? QString::fromUtf8(parseValue(argc, argv, "--mcp-http-host"))
    : QStringLiteral("127.0.0.1");
const quint16 httpPort = parseValue(argc, argv, "--mcp-http-port")
    ? QString::fromUtf8(parseValue(argc, argv, "--mcp-http-port")).toUShort()
    : 8765;

if (headlessMcpHttp)
{
    QSettings s;
    s.setValue(SettingsKeys::kKeyMcpHttpEnabled, true);
    s.setValue(SettingsKeys::kKeyMcpHttpHost, httpHost);
    s.setValue(SettingsKeys::kKeyMcpHttpPort, static_cast<int>(httpPort));
}
```

- [ ] **Step 6: Add a live HTTP round-trip test and one settings persistence test**

```cpp
TEST(HttpTransport, EngineEnabledSettingsStartHttpAndServePostMcp)
{
    QSettings s;
    s.setValue(SettingsKeys::kKeyMcpHttpEnabled, true);
    s.setValue(SettingsKeys::kKeyMcpHttpHost, QStringLiteral("127.0.0.1"));
    s.setValue(SettingsKeys::kKeyMcpHttpPort, 18760);

    AudioEngine engine;
    engine.initialize();
    ASSERT_TRUE(engine.getMcpHttpConfig().running);

    // POST /mcp against localhost:18760 and assert initialize succeeds.
}
```

- [ ] **Step 7: Verify the backend and launcher paths**

Run:
`cmake --build build --config Debug`

Run:
`build/hdaw_tests.exe --gtest_filter="HttpTransport.*:McpServer.*:FrontendServer.*"`

Expected: build succeeds; the HTTP transport test passes; the new live round-trip test passes; existing stdio tests remain unchanged.

### Task 2: Frontend settings RPC + Preferences UI + unit test

**Files:**
- Modify: `src/frontend/FrontendRouter.cpp`
- Modify: `src/frontend/FrontendRpc.h`
- Modify: `frontend/src/components/PreferencesDialog.tsx`
- Modify: `frontend/src/components/PreferencesDialog.test.tsx`

- [ ] **Step 1: Expose the new settings methods in the frontend namespace**

```cpp
namespace method {
    inline constexpr const char* Settings = "settings";
}
```

- [ ] **Step 2: Make the router return and persist the MCP HTTP config**

```cpp
if (ns == method::Settings) {
    if (m == "getMcpHttpConfig") return toJson(engine.getMcpHttpConfig());
    if (m == "setMcpHttpConfig") {
        const auto o = paramsObject(params);
        bool enabled = false;
        std::string host;
        int port = 0;
        if (!requireBool(o, "enabled", enabled, nullptr)
            || !requireString(o, "host", host, nullptr)
            || !requireInt(o, "port", port, nullptr))
            return makeError(-32602, "enabled, host, port required");
        QString error;
        if (!engine.setMcpHttpConfig(enabled, QString::fromStdString(host), static_cast<quint16>(port), &error))
            return makeError(-32000, error);
        return { false, QJsonValue::Null };
    }
}
```

- [ ] **Step 3: Add an MCP HTTP section to PreferencesDialog**

```tsx
const [mcpHttpEnabled, setMcpHttpEnabled] = useState(false);
const [mcpHttpHost, setMcpHttpHost] = useState("127.0.0.1");
const [mcpHttpPort, setMcpHttpPort] = useState(8765);

const loadSettings = useCallback(async () => {
  const mcp = await rpc.call("settings.getMcpHttpConfig").catch(() => ({ enabled: false, host: "127.0.0.1", port: 8765 }));
  setMcpHttpEnabled((mcp as any).enabled);
  setMcpHttpHost((mcp as any).host);
  setMcpHttpPort((mcp as any).port);
}, []);
```

- [ ] **Step 4: Persist and apply changes live from the UI**

```tsx
const handleSetMcpHttp = async (next: { enabled: boolean; host: string; port: number }) => {
  setMcpHttpEnabled(next.enabled);
  setMcpHttpHost(next.host);
  setMcpHttpPort(next.port);
  await rpc.call("settings.setMcpHttpConfig", next).catch(() => {});
};
```

- [ ] **Step 5: Add unit coverage for load + toggle wiring**

```ts
expect(mockedCall).toHaveBeenCalledWith("settings.getMcpHttpConfig");
await fireEvent.click(screen.getByLabelText("Enable MCP HTTP"));
expect(mockedCall).toHaveBeenCalledWith("settings.setMcpHttpConfig", {
  enabled: true,
  host: "127.0.0.1",
  port: 8765,
});
```

- [ ] **Step 6: Verify the frontend**

Run:
`cd frontend; npm test`

Run:
`cd frontend; npm run build`

Expected: PreferencesDialog tests pass and the React build succeeds.

### Task 3: Docs update

**Files:**
- Modify: `README.md`
- Modify: `docs/testing-mcp.md`

- [ ] **Step 1: Document the new headless flag and persisted settings contract**

```md
- `--mcp-http` starts the loopback MCP HTTP server and writes `mcp/httpEnabled`, `mcp/httpHost`, and `mcp/httpPort` to `QSettings`.
- `settings.getMcpHttpConfig` / `settings.setMcpHttpConfig` control the live GUI toggle.
```

- [ ] **Step 2: Verify docs stay consistent with the shipped flags**

Run:
`rg -n "mcp-http|mcp/httpEnabled|settings\.setMcpHttpConfig" README.md docs/testing-mcp.md src frontend`

Expected: the new flag and settings names appear exactly once in the docs sections that describe runtime control.

---

## Completion Checks

- [ ] `cmake --build build --config Debug`
- [ ] `build/hdaw_tests.exe --gtest_filter="HttpTransport.*:McpServer.*:FrontendServer.*"`
- [ ] `cd frontend; npm test`
- [ ] `cd frontend; npm run build`
- [ ] Live HTTP POST `/mcp` starts/stops with GUI toggle or `--mcp-http`
- [ ] stdio MCP behavior unchanged
