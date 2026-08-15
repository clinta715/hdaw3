# `--ui` Unified Launcher Flag

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `--ui` flag to `HDAW.exe` that starts the engine, serves the HTML frontend from bundled Qt resources, and opens the system browser — one executable, no Electron dependency.

**Architecture:** The `--ui` mode reuses the existing `FrontendServer` (WebSocket on port 8766) and adds a lightweight `QHttpServer` instance to serve the bundled React frontend from Qt resources. The HTTP server dynamically injects the WebSocket port into `index.html` before serving it. CSS/JS assets are served from resources with proper MIME types. The system browser opens automatically via `QDesktopServices::openUrl()`.

**Tech Stack:** Qt 6 (QHttpServer, QDesktopServices), Qt Resource System (`.qrc`), existing `FrontendServer`, Vite static build output.

---

## File Map

| File | Action | Purpose |
|------|--------|---------|
| `src/resources/frontend.qrc` | Create | Qt resource file bundling `frontend/dist/` |
| `src/frontend/UiHttpServer.h` | Create | HTTP server class serving static frontend from resources |
| `src/frontend/UiHttpServer.cpp` | Create | Implementation: route handlers for `/`, `/assets/*`, port injection |
| `CMakeLists.txt` | Modify | Add `frontend.qrc` to `HDAW` target; add `UiHttpServer.{h,cpp}` |
| `src/main.cpp` | Modify | Add `--ui` flag handling; wire up `UiHttpServer` + `FrontendServer` + browser open |
| `frontend/src/rpc.ts` | Modify | Read WebSocket port from injected `window.__HDAW_WS_PORT__` |

---

### Task 1: Create the Qt resource file for the frontend

**Files:**
- Create: `src/resources/frontend.qrc`

The Vite build outputs `frontend/dist/` with three files: `index.html`, `assets/index-*.js`, `assets/index-*.css`. These get bundled as Qt resources so the exe is self-contained.

- [ ] **Step 1: Create `frontend.qrc`**

```xml
<!-- src/resources/frontend.qrc -->
<RCC>
    <qresource prefix="/ui">
        <file alias="index.html">../../frontend/dist/index.html</file>
    </qresource>
    <qresource prefix="/ui/assets">
        <file alias="index.js">../../frontend/dist/assets/index-BTxqAnBE.js</file>
        <file alias="index.css">../../frontend/dist/assets/index-BKApOiHU.css</file>
    </qresource>
</RCC>
```

Note: The Vite output filenames contain content hashes that change on every build. The aliases (`index.js`, `index.css`) decouple the resource path from the hash. The `index.html` in `dist/` references the hashed filenames, so we must also patch it — but we'll serve `index.html` dynamically (Task 3) so we can fix the asset paths at serve time.

- [ ] **Step 2: Verify the resource file compiles**

Add the `.qrc` to `CMakeLists.txt` in Task 4. For now, just create the file.

- [ ] **Step 3: Commit**

```bash
git add src/resources/frontend.qrc
git commit -m "resources: add frontend dist as Qt resource bundle"
```

---

### Task 2: Create `UiHttpServer` — the static file server

**Files:**
- Create: `src/frontend/UiHttpServer.h`
- Create: `src/frontend/UiHttpServer.cpp`

This class owns a `QHttpServer` that serves the bundled React frontend from Qt resources. It handles three route patterns:
- `GET /` → serves `index.html` with the WebSocket port injected
- `GET /assets/<filename>` → serves the aliased JS/CSS from resources
- Everything else → 404

- [ ] **Step 1: Create `UiHttpServer.h`**

```cpp
#pragma once
#include <QObject>
#include <QHttpServer>
#include <QTcpServer>
#include <memory>

namespace frontend {

// Lightweight HTTP server that serves the bundled HTML frontend from Qt
// resources. Used by the --ui flag to provide a self-contained DAW
// experience: the engine, WebSocket server, and frontend are all in
// HDAW.exe — no Electron or external browser dependencies needed.
//
// Usage:
//   UiHttpServer httpServer(wsPort);
//   if (httpServer.start(8765)) {
//       QDesktopServices::openUrl(QUrl(QString("http://127.0.0.1:%1").arg(httpServer.port())));
//   }
class UiHttpServer : public QObject {
    Q_OBJECT
public:
    // wsPort: the port the FrontendServer (WebSocket) is listening on.
    // The injected <script> sets window.__HDAW_WS_PORT__ = wsPort so the
    // React app connects to the right WebSocket endpoint.
    explicit UiHttpServer(quint16 wsPort, QObject* parent = nullptr);
    ~UiHttpServer() override;

    // Bind to the given HTTP port on loopback. Returns false if the port
    // is in use. Idempotent.
    bool start(quint16 port);

    // Stop the HTTP server.
    void stop();

    // The actual bound HTTP port.
    quint16 port() const;

private:
    QHttpServer server_;
    std::unique_ptr<QTcpServer> tcp_;
    quint16 wsPort_;
};

} // namespace frontend
```

- [ ] **Step 2: Create `UiHttpServer.cpp`**

```cpp
#include "UiHttpServer.h"
#include <QFile>
#include <QByteArray>
#include <QHostAddress>
#include <QHttpServerResponse>
#include <QHttpServerRequest>
#include <QMimeDatabase>

namespace frontend {

UiHttpServer::UiHttpServer(quint16 wsPort, QObject* parent)
    : QObject(parent), wsPort_(wsPort) {}

UiHttpServer::~UiHttpServer() { stop(); }

bool UiHttpServer::start(quint16 port) {
    // Serve index.html at the root, with the WebSocket port injected.
    server_.route("/", QHttpServerRequest::Method::Get,
        [this](const QHttpServerRequest&) -> QHttpServerResponse {
            QFile f(":/ui/index.html");
            if (!f.open(QIODevice::ReadOnly)) {
                return QHttpServerResponse(QHttpServerResponse::StatusCode::NotFound);
            }
            QByteArray html = f.readAll();
            // Inject the WebSocket port before the closing </head> tag.
            // The frontend's rpc.ts reads window.__HDAW_WS_PORT__ to know
            // which port to connect to.
            QByteArray injection = QString(
                "<script>window.__HDAW_WS_PORT__ = %1;</script>\n"
            ).arg(wsPort_).toUtf8();
            html.replace("</head>", injection + "</head>");

            // Fix hashed asset paths: the dist/index.html references
            // ./assets/index-BTxqAnBE.js and ./assets/index-BKApOiHU.css
            // but our resource aliases are index.js and index.css.
            // Replace the hashed filenames with our aliases.
            // We do this with a simple regex-free approach: find the
            // assets/ references and replace everything between
            // "assets/" and the closing quote.
            auto fixAssetPath = [](QByteArray& h, const char* ext, const char* alias) {
                // Find: src="./assets/index-XXXXX.ext" or href="./assets/index-XXXXX.ext"
                const QByteArray marker = QByteArray("assets/index-");
                int pos = 0;
                while ((pos = h.indexOf(marker, pos)) != -1) {
                    int start = pos;  // position of "assets/"
                    // Find the closing quote after the extension
                    int extPos = h.indexOf(ext, pos);
                    if (extPos == -1) break;
                    int endQuote = extPos + int(qstrlen(ext));
                    if (endQuote < h.size() && (h[endQuote] == '"' || h[endQuote] == '\'')) {
                        // Replace "assets/index-XXXXX.ext" with "assets/alias"
                        h.replace(start, endQuote - start, QByteArray("assets/") + alias);
                        pos = start + QByteArray("assets/").size() + QByteArray(alias).size();
                    } else {
                        pos = extPos + int(qstrlen(ext));
                    }
                }
            };
            fixAssetPath(html, ".js", "index.js");
            fixAssetPath(html, ".css", "index.css");

            return QHttpServerResponse("text/html", html);
        });

    // Serve static assets from Qt resources.
    // Routes like GET /assets/index.js → :/ui/assets/index.js
    server_.route("/assets/<arg>", QHttpServerRequest::Method::Get,
        [](const QString& filename) -> QHttpServerResponse {
            QString resourcePath = ":/ui/assets/" + filename;
            QFile f(resourcePath);
            if (!f.open(QIODevice::ReadOnly)) {
                return QHttpServerResponse(QHttpServerResponse::StatusCode::NotFound);
            }
            QByteArray data = f.readAll();

            // Determine MIME type from the filename.
            QMimeDatabase db;
            QMimeType mime = db.mimeTypeForFile(filename);
            return QHttpServerResponse(mime.name(), data);
        });

    // Bind the TCP socket.
    tcp_ = std::make_unique<QTcpServer>();
    if (!tcp_->listen(QHostAddress::LocalHost, port)) {
        tcp_.reset();
        return false;
    }
    if (!server_.bind(tcp_.get())) {
        tcp_->close();
        tcp_.reset();
        return false;
    }
    return true;
}

void UiHttpServer::stop() {
    if (tcp_) {
        tcp_->close();
        tcp_.reset();
    }
    server_.disconnect();
}

quint16 UiHttpServer::port() const {
    return tcp_ ? tcp_->serverPort() : 0;
}

} // namespace frontend
```

- [ ] **Step 3: Commit**

```bash
git add src/frontend/UiHttpServer.h src/frontend/UiHttpServer.cpp
git commit -m "frontend: add UiHttpServer for serving bundled HTML frontend"
```

---

### Task 3: Make the frontend read the injected WebSocket port

**Files:**
- Modify: `frontend/src/rpc.ts`

The frontend currently hardcodes `api?.rpcPort ?? 8766`. In `--ui` mode there's no Electron preload, so `api` is undefined and it defaults to 8766. That's correct for the default case, but we should also read from the injected `window.__HDAW_WS_PORT__` for robustness (e.g. if the WS port is overridden).

- [ ] **Step 1: Update `rpc.ts` to read the injected port**

Current code (`frontend/src/rpc.ts`):
```typescript
import { RpcClient } from "./rpc/client";

const api = (window as any).__HDAW_ELECTRON_API__ as { rpcPort?: number } | undefined;
const port = api?.rpcPort ?? 8766;
export const rpc = new RpcClient(port);
```

Change to:
```typescript
import { RpcClient } from "./rpc/client";

const api = (window as any).__HDAW_ELECTRON_API__ as { rpcPort?: number } | undefined;
const injected = (window as any).__HDAW_WS_PORT__ as number | undefined;
const port = api?.rpcPort ?? injected ?? 8766;
export const rpc = new RpcClient(port);
```

This adds one line. The priority chain: Electron preload API > injected window variable > hardcoded default.

- [ ] **Step 2: Rebuild the frontend dist**

```bash
cd frontend && npm run build
```

This regenerates `frontend/dist/` with the updated JS. The `.qrc` file references the dist output, so the next CMake build picks it up.

- [ ] **Step 3: Commit**

```bash
git add frontend/src/rpc.ts
git commit -m "frontend: read WebSocket port from injected window.__HDAW_WS_PORT__"
```

---

### Task 4: Wire up the build — add resources and sources to CMakeLists.txt

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add `frontend.qrc` and `UiHttpServer` to the `HDAW` target**

In the `add_executable(HDAW ...)` block, add after the existing `resources.qrc` line:

```cmake
    src/resources/frontend.qrc
```

Add the new source files after the existing frontend sources:

```cmake
    src/frontend/UiHttpServer.h
    src/frontend/UiHttpServer.cpp
```

The full diff for the `add_executable(HDAW ...)` block:

```cmake
add_executable(HDAW
    src/main.cpp
    # ... all existing sources ...
    src/resources/resources.rc
    src/resources/resources.qrc
    src/resources/frontend.qrc          # <-- ADD
    # ... existing sources continue ...
    src/frontend/UiHttpServer.h         # <-- ADD
    src/frontend/UiHttpServer.cpp       # <-- ADD
)
```

- [ ] **Step 2: Verify the build compiles**

```bash
cmake --build build --config Debug
```

Expected: clean compile. If the `.qrc` paths are wrong, `rcc` will error with "file not found" — fix the relative paths in `frontend.qrc`.

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: add frontend.qrc and UiHttpServer to HDAW target"
```

---

### Task 5: Add `--ui` flag to `main.cpp`

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Add the `UiHttpServer` include**

At the top of `main.cpp`, after the existing `#include "frontend/FrontendServer.h"`:

```cpp
#include "frontend/UiHttpServer.h"
#include <QDesktopServices>
#include <QUrl>
```

- [ ] **Step 2: Add `--ui` flag parsing**

In the flag parsing section, after the existing `noMcp` flag:

```cpp
    const bool uiMode = parseFlag(argc, argv, "--ui");
```

Update the mode log:

```cpp
    HDAW_LOG("main", QString("Mode: %1").arg(
        headlessMcp ? "HEADLESS MCP (--mcp-stdio)" :
        headlessFrontend ? "HEADLESS FRONTEND (--headless)" :
        uiMode ? "UI (--ui)" :
        "GUI"));
```

- [ ] **Step 3: Add the `--ui` mode handler**

Insert a new mode block after the `headlessFrontend` block (after line 130) and before the `--mcp-http-port` block. The `--ui` mode:

1. Creates `QApplication` (needed for `QDesktopServices`)
2. Initializes the engine
3. Starts the `FrontendServer` (WebSocket on 8766)
4. Starts the `UiHttpServer` (HTTP on 8765)
5. Opens the system browser

```cpp
    if (uiMode) {
        QApplication app(argc, argv);
        app.setWindowIcon(QIcon(":/app.ico"));
        app.setStyleSheet(getGlobalStyleSheet());

        quint16 wsPort = kDefaultFrontendPort;  // 8766
        quint16 httpPort = 8765;
        if (const char* p = parseValue(argc, argv, "--port")) {
            bool ok = false;
            auto parsed = QString::fromUtf8(p).toUShort(&ok);
            if (ok) wsPort = parsed;
        }
        if (const char* p = parseValue(argc, argv, "--http-port")) {
            bool ok = false;
            auto parsed = QString::fromUtf8(p).toUShort(&ok);
            if (ok) httpPort = parsed;
        }

        AudioEngine engine;
        engine.initialize();
        engine.getPluginManager().loadCache();

        // Start the WebSocket server for the frontend RPC.
        frontend::FrontendServer wsServer(engine);
        if (!wsServer.start(wsPort)) {
            HDAW_LOG("main", QString("FrontendServer failed to bind port %1").arg(wsPort));
            return 1;
        }
        HDAW_LOG("main", QString("WebSocket server on ws://127.0.0.1:%1").arg(wsServer.port()));

        // Start the HTTP server to serve the bundled frontend.
        frontend::UiHttpServer httpServer(wsPort);
        if (!httpServer.start(httpPort)) {
            HDAW_LOG("main", QString("UiHttpServer failed to bind port %1").arg(httpPort));
            return 1;
        }
        HDAW_LOG("main", QString("HTTP server on http://127.0.0.1:%1").arg(httpServer.port()));

        // Open the system browser.
        QUrl url(QString("http://127.0.0.1:%1").arg(httpServer.port()));
        QDesktopServices::openUrl(url);
        HDAW_LOG("main", QString("Opened browser: %1").arg(url.toString()));

        QObject::connect(&app, &QCoreApplication::aboutToQuit, [&] {
            httpServer.stop();
            wsServer.stop();
            engine.shutdown();
        });

        return app.exec();
    }
```

- [ ] **Step 4: Update the usage comment at the top of `main.cpp`**

Update the comment block at the top to document the new flag:

```cpp
// HDAW — Qt GUI desktop DAW.
// Usage:
//   HDAW.exe                    (default: Qt GUI)
//   HDAW.exe --ui               (engine + HTML frontend in browser)
//   HDAW.exe --ui --port=9000   (custom WebSocket port)
//   HDAW.exe --ui --http-port=9001 (custom HTTP port)
//   HDAW.exe --mcp-stdio        (headless MCP over stdin/stdout)
//   HDAW.exe --headless         (headless WebSocket server)
//   HDAW.exe --no-mcp           (GUI without MCP server)
```

- [ ] **Step 5: Build and test**

```bash
cmake --build build --config Debug
build\Debug\HDAW.exe --ui
```

Expected:
1. Console log shows "Mode: UI (--ui)"
2. WebSocket server starts on port 8766
3. HTTP server starts on port 8765
4. System browser opens to `http://127.0.0.1:8765`
5. The React frontend loads and connects to the WebSocket
6. The DAW is fully functional in the browser

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp
git commit -m "main: add --ui flag for unified engine+browser launcher"
```

---

### Task 6: Handle the case where frontend dist hasn't been built

**Files:**
- Modify: `src/frontend/UiHttpServer.cpp`

If the user runs `HDAW.exe --ui` without first running `npm run build` in the frontend directory, the Qt resources will contain stale or missing files. The `.qrc` references files at build time (CMake configure time), so if the files don't exist, `rcc` fails at compile time — which is actually a good guard. But if the dist is stale, the user gets an old frontend.

Add a compile-time check and a runtime fallback message.

- [ ] **Step 1: Add a CMake check for the frontend dist**

At the end of the `HDAW` target section in `CMakeLists.txt`, add:

```cmake
# Verify the frontend dist exists when building with UI support.
# If missing, the .qrc will fail to compile — this gives a clearer message.
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/frontend/dist/index.html")
    message(WARNING
        "frontend/dist/index.html not found. "
        "Run 'cd frontend && npm run build' before building HDAW with --ui support. "
        "The HDAW target will fail to link without the frontend resources."
    )
endif()
```

- [ ] **Step 2: Add a runtime "not built" fallback in `UiHttpServer`**

In `UiHttpServer.cpp`, in the `/` route handler, if the resource file can't be opened, return a helpful error page instead of a bare 404:

```cpp
            QFile f(":/ui/index.html");
            if (!f.open(QIODevice::ReadOnly)) {
                QByteArray errorHtml = R"(
<!DOCTYPE html>
<html><head><title>HDAW</title></head>
<body style="background:#141416;color:#ccc;font-family:sans-serif;padding:40px">
<h1>HDAW Frontend Not Built</h1>
<p>The HTML frontend has not been compiled into this executable.</p>
<p>Run <code>cd frontend && npm run build</code>, then rebuild HDAW.</p>
</body></html>
                )";
                return QHttpServerResponse("text/html", errorHtml);
            }
```

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt src/frontend/UiHttpServer.cpp
git commit -m "build: add frontend dist check; runtime fallback for missing frontend"
```

---

### Task 7: Update `--help` / documentation

**Files:**
- Modify: `README.md` (if it documents CLI flags)
- Modify: `AGENTS.md` (CLI flags section)

- [ ] **Step 1: Update the CLI flags documentation in `AGENTS.md`**

In the "CLI flags" section under "Build", add the `--ui` flag:

```markdown
- CLI flags: `--ui` starts the engine and serves the HTML frontend in the
  system browser (one executable, no Electron needed),
  `--ui --port=N` overrides the WebSocket port, `--ui --http-port=N`
  overrides the HTTP serving port, `--mcp-stdio` forces headless stdio
  MCP server, `--no-mcp` disables MCP entirely, `--mcp-http-port=N`
  overrides the HTTP server's bind port for this launch. Without any
  flag the Qt GUI starts; if `stdin`/`stdout` are not TTYs the stdio
  MCP server starts automatically and the GUI is skipped.
```

- [ ] **Step 2: Commit**

```bash
git add AGENTS.md
git commit -m "docs: document --ui flag in AGENTS.md"
```

---

## Testing Checklist

After implementation, verify these scenarios:

| Test | Expected |
|------|----------|
| `HDAW.exe` (no flags) | Qt GUI launches as before |
| `HDAW.exe --ui` | Engine starts, browser opens, frontend loads and connects |
| `HDAW.exe --ui --port=9000` | WebSocket on 9000, frontend connects to 9000 |
| `HDAW.exe --ui --http-port=9001` | HTTP on 9001, browser opens to :9001 |
| `HDAW.exe --mcp-stdio` | MCP over stdin/stdout as before |
| `HDAW.exe --headless` | WebSocket server as before |
| Close browser, reopen tab | Frontend reconnects to the still-running engine |
| Kill engine process | Browser shows disconnect, frontend auto-reconnects |
| Play/stop/record in browser | All transport controls work |
| Add tracks, clips, notes | All CRUD operations work via RPC |
| VU meters move during playback | Push notifications working |
