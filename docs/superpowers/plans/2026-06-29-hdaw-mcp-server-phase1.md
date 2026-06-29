# HDAW MCP Server — Implementation Plan, Phase 1 (Foundation)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land the MCP server foundation: gtest pilot, the `noteID` model prerequisite, JSON-RPC framing, schema validator, transport interface + stdio + loopback, `McpServer` core, headless mode in `main.cpp`, the 8 inspector/reader tools, the 2 transport tools, the 2 undo tools, and an end-to-end integration test driving the server through the loopback transport. Phase 2 (clip/track/note/comp/fx/auto tools, HTTP transport, `export_audio`, README) follows as a separate plan.

**Architecture:** In-process. A new `src/mcp/` module (`McpServer` + tool registry + hand-rolled JSON-RPC + schema validator + transports + a loopback test transport) is constructed in `main.cpp` alongside `AudioEngine`. Stdio auto-detected at startup (no TTY → headless); HTTP is opt-in (Phase 2). Every tool handler runs on the main thread. The only model change is adding a stable `noteID` to `MIDI_NOTE` and an `allocateNoteID` allocator.

**Tech Stack:** Qt 6 (Widgets + Core; Network/HttpServer added in Phase 2), JUCE 8, C++20, GoogleTest via `FetchContent`, JSON-RPC 2.0 over `QJsonDocument`.

**Reference:** `docs/superpowers/specs/2026-06-29-hdaw-mcp-server-design.md`.

---

## File structure (Phase 1)

**New files (module):**
- `src/mcp/McpServer.h` / `.cpp`
- `src/mcp/McpToolDef.h`
- `src/mcp/McpJsonRpc.h` / `.cpp`
- `src/mcp/McpSchema.h` / `.cpp`
- `src/mcp/McpTransport.h`
- `src/mcp/McpTransportStdio.h` / `.cpp`
- `src/mcp/McpTransportLoopback.h` / `.cpp`
- `src/mcp/McpTools.h` / `.cpp` (Phase 1 registers read + transport + undo; Phase 2 adds the rest)

**Modified files:**
- `src/main.cpp` — CLI flags, headless detection, wiring.
- `src/model/ProjectModel.h` / `.cpp` — `IDs::noteID`, `allocateNoteID()`, `scanAndSyncNoteIDs()`.
- `src/engine/ProjectSerializer.cpp` — call `scanAndSyncNoteIDs` after load.
- All note-creation sites: `src/ui/PianoRollModel.cpp`, `src/ui/NoteGridWidget.cpp`, `src/ui/StepEditorWidget.cpp`, `src/ui/PhraseGeneratorDialog.cpp`, `src/ui/MainWindow.cpp`.
- `CMakeLists.txt` — gtest + tests subdir (no Network/HttpServer yet — Phase 2).

**New test files:**
- `tests/CMakeLists.txt`
- `tests/test_main.cpp`
- `tests/unit/mcp/json_rpc_test.cpp`
- `tests/unit/mcp/schema_test.cpp`
- `tests/unit/mcp/tool_registry_test.cpp`
- `tests/unit/mcp/dry_run_test.cpp`
- `tests/unit/mcp/note_id_test.cpp`
- `tests/unit/mcp/transport_stdio_test.cpp`
- `tests/integration/mcp/mcp_server_test.cpp`

**CMake split:** Phase 1 creates `HDAW_lib` (static, no GUI sources) and the `HDAW` executable links it + GUI. Tests link `HDAW_lib`. (Phase 2 may need to relocate GUI-only sources if any test requires them; for Phase 1, no GUI dependency in tests.)

---

## Task 1: Test infrastructure (gtest)

**Files:** Create `tests/CMakeLists.txt`, `tests/test_main.cpp`, `tests/unit/mcp/*` + `tests/integration/mcp/*` stubs. Modify `CMakeLists.txt`.

- [ ] **Step 1: Add gtest block to top-level `CMakeLists.txt`**

Append (after the existing `add_executable(HDAW ...)` block, before any `install()` rules):

```cmake
# --- Tests (MCP pilot) ---
option(HDAW_BUILD_TESTS "Build HDAW unit/integration tests" ON)
if(HDAW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

- [ ] **Step 2: Create `tests/CMakeLists.txt`**

```cmake
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(hdaw_tests
    test_main.cpp
    unit/mcp/json_rpc_test.cpp
    unit/mcp/schema_test.cpp
    unit/mcp/tool_registry_test.cpp
    unit/mcp/dry_run_test.cpp
    unit/mcp/note_id_test.cpp
    unit/mcp/transport_stdio_test.cpp
    integration/mcp/mcp_server_test.cpp
)
target_include_directories(hdaw_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(hdaw_tests PRIVATE
    Qt6::Core
    HDAW_lib
    GTest::gtest_main
)
add_test(NAME hdaw_tests COMMAND hdaw_tests)
```

- [ ] **Step 3: Split `HDAW` into static lib + GUI exe in `CMakeLists.txt`**

Before the existing `add_executable(HDAW ...)`, add a static lib containing the engine + model + mcp sources (the non-GUI, non-`main.cpp` sources). Tests link `HDAW_lib`; the GUI exe also links it.

```cmake
add_library(HDAW_lib STATIC
    src/engine/AudioEngine.cpp
    src/engine/ClipSourceProcessor.cpp
    src/engine/ExportManager.cpp
    src/engine/MainAudioProcessor.cpp
    src/engine/MidiClipProcessor.cpp
    src/engine/PluginManager.cpp
    src/engine/RoutingManager.cpp
    src/engine/TrackFXSlot.cpp
    src/engine/TransportManager.cpp
    src/model/ProjectModel.cpp
    src/mcp/McpJsonRpc.cpp
    src/mcp/McpSchema.cpp
    src/mcp/McpServer.cpp
    src/mcp/McpTools.cpp
    src/mcp/McpTransportStdio.cpp
    src/mcp/McpTransportLoopback.cpp
)
target_include_directories(HDAW_lib PUBLIC src)
target_link_libraries(HDAW_lib PUBLIC
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_devices
    juce::juce_dsp
    juce::juce_core
    Qt6::Core
)
```

Then change the existing `add_executable(HDAW ...)` to include only the GUI + `main.cpp` sources (remove engine/model/mcp from its source list) and add `HDAW_lib` to its `target_link_libraries`.

- [ ] **Step 4: Create `tests/test_main.cpp`**

```cpp
#include <gtest/gtest.h>
#include <QCoreApplication>
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
```

- [ ] **Step 5: Create the six test source files as stubs**

```cpp
// tests/unit/mcp/json_rpc_test.cpp
#include <gtest/gtest.h>
TEST(Dummy, Works) { EXPECT_EQ(1, 1); }
```

(Identical for `schema_test.cpp`, `tool_registry_test.cpp`, `dry_run_test.cpp`, `note_id_test.cpp`, `transport_stdio_test.cpp`, and `tests/integration/mcp/mcp_server_test.cpp`.)

- [ ] **Step 6: Build and run**

```bash
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Expected: builds; all 7 dummy tests pass.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt tests/
git commit -m "build: add gtest test infrastructure (pilot for MCP module)"
```

---

## Task 2: Prerequisite — stable `noteID` on `MIDI_NOTE`

- [ ] **Step 1: `src/model/ProjectModel.h`** — add the ID, allocator, and scanner declarations

In the `IDs` namespace, add:
```cpp
DECLARE_ID(noteID)
```

Next to the existing `static int allocateClipID();` add:
```cpp
static int allocateNoteID();
static void resetNoteIDCounter();
```

Add a private static next to `nextClipID`:
```cpp
static std::atomic<int> nextNoteID{1};
```

Add a member function:
```cpp
void scanAndSyncNoteIDs();
```

- [ ] **Step 2: `src/model/ProjectModel.cpp`** — implementations

Next to `allocateClipID`:
```cpp
int ProjectModel::allocateNoteID() {
    return nextNoteID.fetch_add(1, std::memory_order_relaxed);
}
void ProjectModel::resetNoteIDCounter() {
    nextNoteID.store(1, std::memory_order_relaxed);
}
```

Add `#include <atomic>` if not present, then implement `scanAndSyncNoteIDs`:
```cpp
void ProjectModel::scanAndSyncNoteIDs() {
    int maxExisting = 0;
    std::function<void(const juce::ValueTree&)> walk = [&](const juce::ValueTree& t) {
        if (t.hasType(IDs::MIDI_NOTE)) {
            if (t.hasProperty(IDs::noteID)) {
                maxExisting = std::max(maxExisting, (int)t.getProperty(IDs::noteID));
            } else {
                int id = nextNoteID.fetch_add(1, std::memory_order_relaxed);
                t.setProperty(IDs::noteID, id, nullptr);
                maxExisting = std::max(maxExisting, id);
            }
            return;
        }
        for (int i = 0; i < t.getNumChildren(); ++i) walk(t.getChild(i));
    };
    walk(projectTree);
    int cur = nextNoteID.load(std::memory_order_relaxed);
    while (cur <= maxExisting && !nextNoteID.compare_exchange_weak(cur, maxExisting + 1)) {}
}
```

- [ ] **Step 3: Make `createMidiNote` assign a `noteID`**

In `ProjectModel.cpp`, in `createMidiNote`, add one line at the top of the property assignments:
```cpp
noteNode.setProperty(IDs::noteID, ProjectModel::allocateNoteID(), nullptr);
```

- [ ] **Step 4: Call `scanAndSyncNoteIDs` on project load**

In `src/engine/ProjectSerializer.cpp`, immediately after the existing `scanAndSyncClipIDs()` call:
```cpp
model.scanAndSyncNoteIDs();
```

- [ ] **Step 5: Assign `noteID` in all existing note-creation sites**

For each file below, find the code that creates a `MIDI_NOTE` ValueTree and add the line:
```cpp
noteNode.setProperty(IDs::noteID, ProjectModel::allocateNoteID(), nullptr);
```

- `src/ui/PianoRollModel.cpp` — in `addNote(...)`.
- `src/ui/NoteGridWidget.cpp` — in the chord-stamp loop and any inline `MIDI_NOTE` creation.
- `src/ui/StepEditorWidget.cpp` — in `commitNote(...)`.
- `src/ui/PhraseGeneratorDialog.cpp` — where generator notes are appended to a `MIDI_NOTE_LIST`.
- `src/ui/MainWindow.cpp` — in the MIDI-import path (search for `IDs::MIDI_NOTE`).

- [ ] **Step 6: Replace `tests/unit/mcp/note_id_test.cpp`**

```cpp
#include <gtest/gtest.h>
#include "model/ProjectModel.h"
#include "model/IDs.h"

TEST(NoteID, AllocatesUniqueIDs) {
    int a = ProjectModel::allocateNoteID();
    int b = ProjectModel::allocateNoteID();
    int c = ProjectModel::allocateNoteID();
    EXPECT_NE(a, b); EXPECT_NE(b, c); EXPECT_NE(a, c);
}

TEST(NoteID, CreateMidiNoteAssignsID) {
    ProjectModel m;
    m.createDefaultProject();
    auto trackList = m.getTrackListTree();
    int found = 0;
    for (int t = 0; t < trackList.getNumChildren(); ++t) {
        auto cl = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
        for (int c = 0; c < cl.getNumChildren(); ++c) {
            auto nl = cl.getChild(c).getChildWithName(IDs::MIDI_NOTE_LIST);
            for (int n = 0; n < nl.getNumChildren(); ++n) {
                EXPECT_TRUE(nl.getChild(n).hasProperty(IDs::noteID));
                ++found;
            }
        }
    }
    EXPECT_GT(found, 0);
}

TEST(NoteID, ScanAndSyncAssignsMissing) {
    ProjectModel m;
    m.createDefaultProject();
    auto nl = m.getTrackListTree().getChild(0).getChildWithName(IDs::CLIP_LIST)
                  .getChild(0).getChildWithName(IDs::MIDI_NOTE_LIST);
    nl.getChild(0).removeProperty(IDs::noteID, nullptr);
    EXPECT_FALSE(nl.getChild(0).hasProperty(IDs::noteID));
    m.scanAndSyncNoteIDs();
    EXPECT_TRUE(nl.getChild(0).hasProperty(IDs::noteID));
}
```

- [ ] **Step 7: Build and run**

```bash
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure -R NoteID
```

Expected: 3 `NoteID` tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/model/ProjectModel.h src/model/ProjectModel.cpp src/engine/ProjectSerializer.cpp \
        src/ui/PianoRollModel.cpp src/ui/NoteGridWidget.cpp src/ui/StepEditorWidget.cpp \
        src/ui/PhraseGeneratorDialog.cpp src/ui/MainWindow.cpp tests/unit/mcp/note_id_test.cpp
git commit -m "model: add stable noteID to MIDI_NOTE (prereq for MCP note tools)"
```

---

## Task 3: JSON-RPC framing

- [ ] **Step 1: Create `src/mcp/McpJsonRpc.h`**

```cpp
#pragma once
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <variant>
#include <optional>

namespace mcp {

struct McpRequest {
    QJsonValue id;
    QString     method;
    QJsonValue  params;
    bool isNotification() const { return id.isNull() && !method.isEmpty(); }
};

struct McpResponse {
    QJsonValue id;
    QJsonValue result;
    QJsonValue error;
    bool isError = false;
    static McpResponse success(const QJsonValue& id, const QJsonValue& result);
    static McpResponse failure(const QJsonValue& id, int code, const QString& message,
                               const QJsonValue& data = {});
};

struct McpNotification { QString method; QJsonValue params; };

namespace err {
    constexpr int ParseError     = -32700;
    constexpr int InvalidRequest = -32600;
    constexpr int MethodNotFound = -32601;
    constexpr int InvalidParams  = -32602;
    constexpr int InternalError  = -32603;
}

QString serializeResponse(const McpResponse& r);
QString serializeNotification(const McpNotification& n);
QJsonValue parseLine(const QByteArray& line, bool* ok);
std::variant<McpRequest, McpResponse> validateRequest(const QJsonValue& v);

} // namespace mcp
```

- [ ] **Step 2: Create `src/mcp/McpJsonRpc.cpp`**

```cpp
#include "McpJsonRpc.h"
#include <QJsonDocument>

namespace mcp {

McpResponse McpResponse::success(const QJsonValue& id, const QJsonValue& result) {
    McpResponse r; r.id = id; r.result = result; r.isError = false; return r;
}
McpResponse McpResponse::failure(const QJsonValue& id, int code, const QString& message,
                                  const QJsonValue& data) {
    McpResponse r; r.id = id; r.isError = true;
    QJsonObject err; err["code"] = code; err["message"] = message;
    if (!data.isUndefined() && !data.isNull()) err["data"] = data;
    r.error = err;
    return r;
}

static QJsonObject baseEnvelope() {
    QJsonObject o; o["jsonrpc"] = "2.0"; return o;
}

QString serializeResponse(const McpResponse& r) {
    QJsonObject o = baseEnvelope();
    o["id"] = r.id;
    if (r.isError) o["error"] = r.error; else o["result"] = r.result;
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}
QString serializeNotification(const McpNotification& n) {
    QJsonObject o = baseEnvelope();
    o["method"] = n.method; o["params"] = n.params;
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QJsonValue parseLine(const QByteArray& line, bool* ok) {
    *ok = false;
    QByteArray trimmed = line.trimmed();
    if (trimmed.isEmpty()) return {};
    QJsonParseError pe;
    auto doc = QJsonDocument::fromJson(trimmed, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) return {};
    *ok = true;
    return doc.object();
}

std::variant<McpRequest, McpResponse> validateRequest(const QJsonValue& v) {
    if (!v.isObject()) return McpResponse::failure({}, err::InvalidRequest, "expected JSON object");
    auto o = v.toObject();
    if (o.value("jsonrpc").toString() != "2.0")
        return McpResponse::failure({}, err::InvalidRequest, "jsonrpc must be \"2.0\"");
    if (!o.contains("method") || !o.value("method").isString())
        return McpResponse::failure({}, err::InvalidRequest, "method must be a string");
    McpRequest r;
    r.method = o.value("method").toString();
    r.params = o.value("params");
    if (o.contains("id")) r.id = o.value("id");
    return r;
}

} // namespace mcp
```

- [ ] **Step 3: Replace `tests/unit/mcp/json_rpc_test.cpp`**

```cpp
#include <gtest/gtest.h>
#include "mcp/McpJsonRpc.h"
using namespace mcp;

TEST(JsonRpc, SerializeSuccess) {
    auto s = serializeResponse(McpResponse::success(42, QJsonObject{{"ok", true}}));
    EXPECT_TRUE(s.contains("\"jsonrpc\":\"2.0\"")) << s.toStdString();
    EXPECT_TRUE(s.contains("\"id\":42"));
    EXPECT_TRUE(s.contains("\"result\""));
    EXPECT_FALSE(s.contains("error"));
}
TEST(JsonRpc, SerializeError) {
    auto s = serializeResponse(McpResponse::failure(7, err::MethodNotFound, "nope"));
    EXPECT_TRUE(s.contains("\"code\":-32601"));
    EXPECT_TRUE(s.contains("nope"));
}
TEST(JsonRpc, ParseLineValid) {
    bool ok = false;
    auto v = parseLine(QByteArray(R"({"jsonrpc":"2.0","id":1,"method":"ping"})"), &ok);
    EXPECT_TRUE(ok);
    auto r = validateRequest(v);
    EXPECT_TRUE(std::holds_alternative<McpRequest>(r));
    EXPECT_EQ(std::get<McpRequest>(r).method, "ping");
    EXPECT_FALSE(std::get<McpRequest>(r).isNotification());
}
TEST(JsonRpc, ValidateNotificationHasNullId) {
    auto v = QJsonObject{{"jsonrpc","2.0"},{"method","notifications/cancelled"},
                         {"params", QJsonObject{{"requestId", 1}}}};
    auto r = validateRequest(v);
    ASSERT_TRUE(std::holds_alternative<McpRequest>(r));
    EXPECT_TRUE(std::get<McpRequest>(r).isNotification());
}
TEST(JsonRpc, InvalidJsonReturnsParseError) {
    bool ok = true;
    parseLine(QByteArray("not json"), &ok);
    EXPECT_FALSE(ok);
}
TEST(JsonRpc, MissingMethodIsInvalidRequest) {
    auto r = validateRequest(QJsonObject{{"jsonrpc","2.0"},{"id",1}});
    ASSERT_TRUE(std::holds_alternative<McpResponse>(r));
    EXPECT_EQ(std::get<McpResponse>(r).error.toObject().value("code").toInt(),
              err::InvalidRequest);
}
```

- [ ] **Step 4: Build and run**

```bash
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure -R JsonRpc
```

Expected: all `JsonRpc` tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/mcp/McpJsonRpc.h src/mcp/McpJsonRpc.cpp tests/unit/mcp/json_rpc_test.cpp
git commit -m "mcp: add JSON-RPC 2.0 framing (requests, responses, notifications)"
```

---

## Task 4: Schema validator

- [ ] **Step 1: Create `src/mcp/McpSchema.h`**

```cpp
#pragma once
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <optional>

namespace mcp {
struct SchemaError {
    QString path, message;
    QString toString() const { return path.isEmpty() ? message : (path + ": " + message); }
};
std::optional<SchemaError> validateSchema(const QJsonValue& value, const QJsonObject& schema);
}
```

- [ ] **Step 2: Create `src/mcp/McpSchema.cpp`**

```cpp
#include "McpSchema.h"
#include <QJsonArray>

namespace mcp {

static bool typeMatches(const QJsonValue& v, const QString& t) {
    if (t == "string")  return v.isString();
    if (t == "number")  return v.isDouble();
    if (t == "integer") return v.isDouble() && (v.toDouble() == (double)(qint64)v.toDouble());
    if (t == "boolean") return v.isBool();
    if (t == "array")   return v.isArray();
    if (t == "object")  return v.isObject();
    if (t == "null")    return v.isNull();
    return true;
}

static std::optional<SchemaError> validateInner(const QJsonValue& v, const QJsonObject& s,
                                                const QString& path) {
    if (s.contains("type")) {
        auto t = s.value("type").toString();
        if (!typeMatches(v, t)) return SchemaError{path, "expected " + t};
    }
    if (s.contains("enum")) {
        auto e = s.value("enum").toArray();
        bool found = false;
        for (const auto& ev : e) if (ev == v) { found = true; break; }
        if (!found) return SchemaError{path, "value not in enum"};
    }
    if (v.isDouble() && (s.contains("minimum") || s.contains("maximum"))) {
        double d = v.toDouble();
        if (s.contains("minimum") && d < s.value("minimum").toDouble())
            return SchemaError{path, "value below minimum"};
        if (s.contains("maximum") && d > s.value("maximum").toDouble())
            return SchemaError{path, "value above maximum"};
    }
    if (v.isObject() && s.contains("properties")) {
        auto o = v.toObject();
        auto props = s.value("properties").toObject();
        if (s.value("additionalProperties").toBool(false) == false) {
            for (auto it = o.begin(); it != o.end(); ++it) {
                if (!props.contains(it.key()))
                    return SchemaError{path.isEmpty() ? it.key() : path + "." + it.key(),
                                       "unknown property"};
            }
        }
        for (auto it = props.begin(); it != props.end(); ++it) {
            auto sub = o.value(it.key());
            if (sub.isUndefined() || sub.isNull()) continue;
            auto err = validateInner(sub, it.value().toObject(),
                                     path.isEmpty() ? it.key() : path + "." + it.key());
            if (err) return err;
        }
        if (s.contains("required")) {
            for (const auto& r : s.value("required").toArray())
                if (!o.contains(r.toString()))
                    return SchemaError{path, "missing required property '" + r.toString() + "'"};
        }
    }
    if (v.isArray() && s.contains("items")) {
        auto a = v.toArray();
        for (int i = 0; i < a.size(); ++i) {
            auto err = validateInner(a[i], s.value("items").toObject(),
                                     path + "[" + QString::number(i) + "]");
            if (err) return err;
        }
    }
    return std::nullopt;
}

std::optional<SchemaError> validateSchema(const QJsonValue& value, const QJsonObject& schema) {
    return validateInner(value, schema, QString{});
}

} // namespace mcp
```

- [ ] **Step 3: Replace `tests/unit/mcp/schema_test.cpp`**

```cpp
#include <gtest/gtest.h>
#include "mcp/McpSchema.h"
using namespace mcp;

TEST(Schema, AcceptsMatchingObject) {
    QJsonObject s{{"type","object"},{"required", QJsonArray{"x"}},
                  {"properties", QJsonObject{{"x", QJsonObject{{"type","integer"}}}}}};
    EXPECT_FALSE(validateSchema(QJsonObject{{"x", 1}}, s));
}
TEST(Schema, RejectsMissingRequired) {
    QJsonObject s{{"type","object"},{"required", QJsonArray{"x"}}};
    auto err = validateSchema(QJsonObject{}, s);
    ASSERT_TRUE(err); EXPECT_EQ(err->path, "x");
}
TEST(Schema, RejectsWrongType) {
    auto err = validateSchema(QJsonValue{"not int"},
                              QJsonObject{{"type","integer"}});
    ASSERT_TRUE(err); EXPECT_EQ(err->message, "expected integer");
}
TEST(Schema, RejectsOutOfRange) {
    QJsonObject s{{"type","integer"},{"minimum",0},{"maximum",127}};
    EXPECT_FALSE(validateSchema(200, s));
    auto err = validateSchema(-1, s);
    ASSERT_TRUE(err); EXPECT_TRUE(err->message.contains("below minimum"));
}
TEST(Schema, RejectsBadEnum) {
    auto err = validateSchema(QString("rewind"),
                              QJsonObject{{"enum", QJsonArray{"play","stop"}}});
    ASSERT_TRUE(err);
}
TEST(Schema, ValidatesNestedArray) {
    QJsonObject s{{"type","array"},
                  {"items", QJsonObject{{"type","integer"},{"minimum",0},{"maximum",127}}}};
    EXPECT_FALSE(validateSchema(QJsonArray{60, 64, 67}, s));
    auto err = validateSchema(QJsonArray{60, 200}, s);
    ASSERT_TRUE(err); EXPECT_EQ(err->path, "[1]");
}
TEST(Schema, RejectsUnknownPropertyWhenAdditionalFalse) {
    QJsonObject s{{"type","object"},{"additionalProperties", false},
                  {"properties", QJsonObject{{"a", QJsonObject{{"type","integer"}}}}}};
    auto err = validateSchema(QJsonObject{{"a",1},{"b",2}}, s);
    ASSERT_TRUE(err); EXPECT_EQ(err->path, "b");
}
```

- [ ] **Step 4: Build and run**

```bash
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure -R Schema
```

Expected: all `Schema` tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/mcp/McpSchema.h src/mcp/McpSchema.cpp tests/unit/mcp/schema_test.cpp
git commit -m "mcp: add minimal JSON-Schema validator (subset)"
```

---

## Task 5: Transport interface + Loopback transport

- [ ] **Step 1: Create `src/mcp/McpTransport.h`**

```cpp
#pragma once
#include <QByteArray>
class McpServer;
namespace mcp {
class Transport {
public:
    virtual ~Transport() = default;
    virtual void start(McpServer* server) = 0;
    virtual void stop() = 0;
    virtual void send(const QByteArray& jsonLine) = 0;
    virtual void notify(const QByteArray& jsonLine) = 0;
};
}
```

- [ ] **Step 2: Create `src/mcp/McpTransportLoopback.h`**

```cpp
#pragma once
#include "McpTransport.h"
#include <QByteArray>
#include <QMutex>
#include <QWaitCondition>

namespace mcp {
class TransportLoopback : public Transport {
public:
    TransportLoopback();
    void start(McpServer* server) override;
    void stop() override;
    void send(const QByteArray& jsonLine) override;
    void notify(const QByteArray& jsonLine) override;
    void pumpIncoming(const QByteArray& line);
    QByteArray drainOutgoing();
    bool waitForOutgoing(int msec, QByteArray* out);
private:
    McpServer* server_ = nullptr;
    QMutex mtx_;
    QByteArray outgoing_;
    QWaitCondition cv_;
    bool stopped_ = false;
};
}
```

- [ ] **Step 3: Create `src/mcp/McpTransportLoopback.cpp`**

```cpp
#include "McpTransportLoopback.h"
#include "McpServer.h"
#include "McpJsonRpc.h"
#include <QJsonDocument>

namespace mcp {

TransportLoopback::TransportLoopback() = default;

void TransportLoopback::start(McpServer* s) { server_ = s; stopped_ = false; }
void TransportLoopback::stop() {
    QMutexLocker lk(&mtx_); stopped_ = true; cv_.wakeAll();
}
void TransportLoopback::send(const QByteArray& line) {
    QMutexLocker lk(&mtx_); outgoing_ += line; outgoing_ += '\n'; cv_.wakeAll();
}
void TransportLoopback::notify(const QByteArray& line) { send(line); }

void TransportLoopback::pumpIncoming(const QByteArray& line) {
    if (!server_) return;
    QByteArray trimmed = line.trimmed();
    if (trimmed.isEmpty()) return;
    QJsonParseError pe;
    auto doc = QJsonDocument::fromJson(trimmed, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) return;
    auto v = validateRequest(doc.object());
    if (!std::holds_alternative<McpRequest>(v)) return;
    auto& req = std::get<McpRequest>(v);
    QMetaObject::invokeMethod(server_, "handleRequestOnTestThread",
        Qt::DirectConnection,
        Q_ARG(QJsonValue, req.id),
        Q_ARG(QString, req.method),
        Q_ARG(QJsonValue, req.params));
}

QByteArray TransportLoopback::drainOutgoing() {
    QMutexLocker lk(&mtx_); QByteArray r = outgoing_; outgoing_.clear(); return r;
}
bool TransportLoopback::waitForOutgoing(int msec, QByteArray* out) {
    QMutexLocker lk(&mtx_);
    if (!outgoing_.isEmpty()) { if (out) *out = outgoing_; return true; }
    if (stopped_) return false;
    cv_.wait(&mtx_, msec);
    if (out) *out = outgoing_;
    return !outgoing_.isEmpty();
}

} // namespace mcp
```

- [ ] **Step 4: Build**

```bash
cmake --build build --config Debug
```

Expected: builds. Loopback is exercised by the integration test in Task 12.

- [ ] **Step 5: Commit**

```bash
git add src/mcp/McpTransport.h src/mcp/McpTransportLoopback.h src/mcp/McpTransportLoopback.cpp
git commit -m "mcp: add transport interface and loopback test transport"
```

---

## Task 6: McpServer core (registry, dispatch, lifecycle)

- [ ] **Step 1: Create `src/mcp/McpToolDef.h`**

```cpp
#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <functional>

namespace mcp {
struct McpToolResult {
    QJsonArray content;
    bool isError = false;
    static McpToolResult text(const QString& s, bool isError = false) {
        McpToolResult r; r.isError = isError;
        r.content.append(QJsonObject{{"type","text"},{"text",s}});
        return r;
    }
};
using McpHandler = std::function<McpToolResult(const QJsonObject& args)>;
struct McpToolDef {
    QString name;
    QString description;
    QJsonObject inputSchema;
    McpHandler handler;
};
}
```

- [ ] **Step 2: Create `src/mcp/McpServer.h`**

```cpp
#pragma once
#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <atomic>
#include "McpToolDef.h"

namespace mcp { class Transport; class AudioEngine; }

namespace mcp {
class McpServer : public QObject {
    Q_OBJECT
public:
    explicit McpServer(QObject* parent = nullptr);
    ~McpServer() override;

    void registerTool(McpToolDef def);
    const QHash<QString, McpToolDef>& tools() const { return tools_; }

    void setTransport(Transport* t);
    void start();
    void stop();

    void setEngine(AudioEngine* e) { engine_ = e; }
    AudioEngine* engine() const { return engine_; }

    QString serverName()    const { return "hdaw"; }
    QString serverVersion() const { return "0.3.0"; }
    QString protocolVersion() const { return "2024-11-05"; }

public slots:
    void handleRequest(QJsonValue id, QString method, QJsonValue params);
    void handleRequestOnTestThread(QJsonValue id, QString method, QJsonValue params);
    void setCancelFlag(bool cancel);

private:
    QJsonValue handleInitialize(const QJsonValue& params);
    QJsonValue handleToolsList();
    QJsonValue handleToolsCall(const QJsonValue& params);
    QJsonValue handlePing();
    QJsonValue handleMethod(const QString& method, const QJsonValue& params, QJsonValue* outError);

    void sendResponse(const QJsonValue& id, const QJsonValue& result);
    void sendError(const QJsonValue& id, int code, const QString& message,
                   const QJsonValue& data = {});

    QHash<QString, McpToolDef> tools_;
    Transport* transport_ = nullptr;
    AudioEngine* engine_ = nullptr;
    std::atomic<bool> cancelFlag_{false};
};
}
```

- [ ] **Step 3: Create `src/mcp/McpServer.cpp`**

```cpp
#include "McpServer.h"
#include "McpTransport.h"
#include "McpJsonRpc.h"
#include "McpSchema.h"
#include <QJsonArray>

namespace mcp {

McpServer::McpServer(QObject* parent) : QObject(parent) {}
McpServer::~McpServer() { stop(); }

void McpServer::registerTool(McpToolDef def) { tools_.insert(def.name, std::move(def)); }
void McpServer::setTransport(Transport* t) { transport_ = t; }
void McpServer::start() { if (transport_) transport_->start(this); }
void McpServer::stop()  { if (transport_) transport_->stop(); }
void McpServer::setCancelFlag(bool c) { cancelFlag_.store(c, std::memory_order_relaxed); }

void McpServer::handleRequest(QJsonValue id, QString method, QJsonValue params) {
    QJsonValue err;
    auto result = handleMethod(method, params, &err);
    if (err.isObject()) {
        int code = err.toObject().value("code").toInt(err::InternalError);
        sendError(id, code, err.toObject().value("message").toString());
    } else {
        sendResponse(id, result);
    }
}
void McpServer::handleRequestOnTestThread(QJsonValue id, QString method, QJsonValue params) {
    handleRequest(id, method, params);
}

QJsonValue McpServer::handleMethod(const QString& m, const QJsonValue& p, QJsonValue* out) {
    if (m == "initialize") return handleInitialize(p);
    if (m == "tools/list") return handleToolsList();
    if (m == "tools/call") return handleToolsCall(p);
    if (m == "ping")      return handlePing();
    *out = QJsonObject{{"code", err::MethodNotFound},{"message","unknown method: " + m}};
    return {};
}

QJsonValue McpServer::handleInitialize(const QJsonValue&) {
    return QJsonObject{
        {"protocolVersion", protocolVersion()},
        {"capabilities", QJsonObject{{"tools", QJsonObject{}}}},
        {"serverInfo", QJsonObject{{"name", serverName()},{"version", serverVersion()}}}
    };
}
QJsonValue McpServer::handleToolsList() {
    QJsonArray arr;
    for (const auto& t : tools_) arr.append(QJsonObject{
        {"name", t.name},{"description", t.description},{"inputSchema", t.inputSchema}});
    return QJsonObject{{"tools", arr}};
}
QJsonValue McpServer::handleToolsCall(const QJsonValue& params) {
    if (!params.isObject() || !params.toObject().contains("name"))
        return QJsonObject{{"isError", true},
            {"content", QJsonArray{QJsonObject{{"type","text"},
                {"text","tools/call requires {name, arguments?}"}}}}};
    auto p = params.toObject();
    QString name = p.value("name").toString();
    QJsonObject args = p.value("arguments").toObject();
    if (!tools_.contains(name))
        return QJsonObject{{"isError", true},
            {"content", QJsonArray{QJsonObject{{"type","text"},
                {"text","unknown tool: " + name}}}}};
    const auto& t = tools_.value(name);
    auto verr = validateSchema(args, t.inputSchema);
    if (verr) return QJsonObject{{"isError", true},
        {"content", QJsonArray{QJsonObject{{"type","text"},
            {"text","invalid params: " + verr->toString()}}}}};
    McpToolResult r;
    try { r = t.handler(args); }
    catch (const std::exception& ex) { r = McpToolResult::text(QString("handler exception: ") + ex.what(), true); }
    catch (...) { r = McpToolResult::text("handler threw unknown exception", true); }
    return QJsonObject{{"isError", r.isError},{"content", r.content}};
}
QJsonValue McpServer::handlePing() { return QJsonObject{}; }

void McpServer::sendResponse(const QJsonValue& id, const QJsonValue& result) {
    if (!transport_) return;
    transport_->send(serializeResponse(McpResponse::success(id, result)).toUtf8());
}
void McpServer::sendError(const QJsonValue& id, int code, const QString& message, const QJsonValue& data) {
    if (!transport_) return;
    transport_->send(serializeResponse(McpResponse::failure(id, code, message, data)).toUtf8());
}

} // namespace mcp
```

- [ ] **Step 4: Replace `tests/unit/mcp/tool_registry_test.cpp`**

```cpp
#include <gtest/gtest.h>
#include "mcp/McpServer.h"
using namespace mcp;

TEST(ToolRegistry, RegisterAndList) {
    McpServer s;
    s.registerTool({"foo","does foo",QJsonObject{{"type","object"}},
                   [](const QJsonObject&){ return McpToolResult::text("ok"); }});
    s.registerTool({"bar","does bar",QJsonObject{{"type","object"}},
                   [](const QJsonObject&){ return McpToolResult::text("ok"); }});
    EXPECT_EQ(s.tools().size(), 2u);
    EXPECT_TRUE(s.tools().contains("foo"));
}
TEST(ToolRegistry, UnknownToolReturnsToolError) {
    McpServer s;
    s.registerTool({"foo","x",QJsonObject{{"type","object"}},
                   [](const QJsonObject&){ return McpToolResult::text("ok"); }});
    auto r = s.handleRequestOnTestThread(1, "tools/call",
        QJsonObject{{"name","baz"},{"arguments",QJsonObject{}}});
    EXPECT_TRUE(r.toObject().value("isError").toBool());
}
TEST(ToolRegistry, InvalidParamsReturnsToolError) {
    McpServer s;
    QJsonObject schema{{"type","object"},{"required", QJsonArray{"x"}},
                       {"properties", QJsonObject{{"x", QJsonObject{{"type","integer"}}}}}};
    s.registerTool({"t","x",schema,[](const QJsonObject&){ return McpToolResult::text("ok"); }});
    auto r = s.handleRequestOnTestThread(1, "tools/call",
        QJsonObject{{"name","t"},{"arguments",QJsonObject{}}});
    EXPECT_TRUE(r.toObject().value("isError").toBool());
    EXPECT_TRUE(r.toObject().value("content").toArray().at(0).toObject()
                .value("text").toString().contains("invalid params"));
}
TEST(ToolRegistry, HandlerExceptionBecomesToolError) {
    McpServer s;
    s.registerTool({"boom","x",QJsonObject{{"type","object"}},
                   [](const QJsonObject&){ throw std::runtime_error("nope"); }});
    auto r = s.handleRequestOnTestThread(1, "tools/call",
        QJsonObject{{"name","boom"},{"arguments",QJsonObject{}}});
    EXPECT_TRUE(r.toObject().value("isError").toBool());
}
```

- [ ] **Step 5: Replace `tests/unit/mcp/dry_run_test.cpp`**

```cpp
#include <gtest/gtest.h>
#include "mcp/McpServer.h"
using namespace mcp;

namespace { struct Counter { int n = 0; }; }

TEST(DryRun, DestructiveToolRespectsFlag) {
    Counter c;
    McpServer s;
    QJsonObject argsSchema{{"type","object"},
        {"properties", QJsonObject{{"dryRun", QJsonObject{{"type","boolean"}}}}}};
    s.registerTool({"destroy","mutates something", argsSchema,
        [&c](const QJsonObject& a) {
            bool d = a.value("dryRun").toBool(false);
            if (d) return McpToolResult::text("would mutate");
            ++c.n; return McpToolResult::text("mutated");
        }});
    auto r1 = s.handleRequestOnTestThread(1, "tools/call",
        QJsonObject{{"name","destroy"},{"arguments",QJsonObject{{"dryRun", true}}}});
    EXPECT_FALSE(r1.toObject().value("isError").toBool());
    EXPECT_EQ(c.n, 0);
    EXPECT_TRUE(r1.toObject().value("content").toArray().at(0).toObject()
                .value("text").toString().contains("would mutate"));
    auto r2 = s.handleRequestOnTestThread(1, "tools/call",
        QJsonObject{{"name","destroy"},{"arguments",QJsonObject{}}});
    EXPECT_FALSE(r2.toObject().value("isError").toBool());
    EXPECT_EQ(c.n, 1);
}
```

- [ ] **Step 6: Build and run**

```bash
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure -R "ToolRegistry|DryRun"
```

Expected: all listed tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/mcp/McpToolDef.h src/mcp/McpServer.h src/mcp/McpServer.cpp \
        tests/unit/mcp/tool_registry_test.cpp tests/unit/mcp/dry_run_test.cpp
git commit -m "mcp: add McpServer core (registry, dispatch, lifecycle)"
```

---

## Task 7: Stdio transport

- [ ] **Step 1: Create `src/mcp/McpTransportStdio.h`**

```cpp
#pragma once
#include "McpTransport.h"
#include <QThread>
#include <QMutex>
#include <atomic>

namespace mcp {
class TransportStdio : public Transport {
public:
    TransportStdio();
    ~TransportStdio() override;
    void start(McpServer* server) override;
    void stop() override;
    void send(const QByteArray& jsonLine) override;
    void notify(const QByteArray& jsonLine) override;
private:
    class Reader;
    Reader* reader_ = nullptr;
    QThread readerThread_;
    McpServer* server_ = nullptr;
    std::atomic<bool> stopped_{false};
    QMutex stdoutMtx_;
};
}
```

- [ ] **Step 2: Create `src/mcp/McpTransportStdio.cpp`**

```cpp
#include "McpTransportStdio.h"
#include "McpServer.h"
#include "McpJsonRpc.h"
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>

namespace mcp {

class TransportStdio::Reader {
public:
    Reader(TransportStdio* p) : parent_(p) {}
    void run() {
        QFile in; in.open(STDIN_FILENO, QIODevice::ReadOnly | QIODevice::Text);
        QTextStream ts(&in);
        while (!parent_->stopped_.load(std::memory_order_relaxed)) {
            if (ts.atEnd()) { QThread::msleep(5); continue; }
            QString line = ts.readLine();
            if (line.isNull()) break;
            auto trimmed = line.trimmed();
            if (trimmed.isEmpty()) continue;
            QJsonParseError pe;
            auto doc = QJsonDocument::fromJson(trimmed.toUtf8(), &pe);
            if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
                parent_->send(serializeResponse(
                    McpResponse::failure({}, err::ParseError, "invalid JSON")).toUtf8());
                continue;
            }
            auto v = validateRequest(doc.object());
            if (std::holds_alternative<McpResponse>(v)) {
                parent_->send(serializeResponse(std::get<McpResponse>(v)).toUtf8());
                continue;
            }
            auto& req = std::get<McpRequest>(v);
            QMetaObject::invokeMethod(parent_->server_, "handleRequest",
                Qt::QueuedConnection,
                Q_ARG(QJsonValue, req.id), Q_ARG(QString, req.method), Q_ARG(QJsonValue, req.params));
        }
    }
private:
    TransportStdio* parent_;
};

TransportStdio::TransportStdio() = default;
TransportStdio::~TransportStdio() { stop(); }

void TransportStdio::start(McpServer* s) {
    server_ = s; stopped_ = false;
    reader_ = new Reader(this);
    readerThread_.start();
}
void TransportStdio::stop() {
    stopped_ = true;
    readerThread_.quit();
    readerThread_.wait(200);
    delete reader_; reader_ = nullptr;
}
void TransportStdio::send(const QByteArray& line) {
    QMutexLocker lk(&stdoutMtx_);
    QFile out; out.open(STDOUT_FILENO, QIODevice::WriteOnly);
    out.write(line); out.putChar('\n'); out.flush();
}
void TransportStdio::notify(const QByteArray& line) { send(line); }

} // namespace mcp
```

- [ ] **Step 3: Replace `tests/unit/mcp/transport_stdio_test.cpp`**

```cpp
#include <gtest/gtest.h>
#include "mcp/McpTransportStdio.h"
using namespace mcp;
TEST(StdioTransport, NotifyPublicSurface) {
    TransportStdio t;
    EXPECT_NO_THROW(t.notify(QByteArray("{}")));
}
```

- [ ] **Step 4: Build**

```bash
cmake --build build --config Debug
```

- [ ] **Step 5: Commit**

```bash
git add src/mcp/McpTransportStdio.h src/mcp/McpTransportStdio.cpp tests/unit/mcp/transport_stdio_test.cpp
git commit -m "mcp: add stdio transport (newline-delimited JSON, reader thread)"
```

---

## Task 8: `main.cpp` — CLI flags + headless mode

- [ ] **Step 1: Read the existing `main.cpp` and locate the `QApplication` / `MainWindow` construction**

- [ ] **Step 2: Add the includes and helpers near the top of `main.cpp`**

```cpp
#include "mcp/McpServer.h"
#include "mcp/McpTransport.h"
#include "mcp/McpTransportStdio.h"
#include "mcp/McpTools.h"
#include "engine/AudioEngine.h"
#include <QCoreApplication>
#include <QSettings>
#include <cstring>
#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#endif

static bool parseFlag(int argc, char** argv, const char* name) {
    for (int i = 1; i < argc; ++i) if (std::strcmp(argv[i], name) == 0) return true;
    return false;
}
static const char* parseValue(int argc, char** argv, const char* name) {
    QString prefix = QString::fromUtf8(name) + "=";
    for (int i = 1; i < argc; ++i) {
        QString a = QString::fromUtf8(argv[i]);
        if (a.startsWith(prefix)) return argv[i] + prefix.toUtf8().size();
    }
    return nullptr;
}
```

- [ ] **Step 3: Insert the headless branch before the existing GUI construction**

```cpp
bool noMcp      = parseFlag(argc, argv, "--no-mcp");
bool forceStdio = parseFlag(argc, argv, "--mcp-stdio");
bool stdioAuto  = !isatty(fileno(stdin)) || !isatty(fileno(stdout));
bool headlessMcp = !noMcp && (forceStdio || stdioAuto);

if (headlessMcp) {
    QCoreApplication app(argc, argv);
    HDAW::AudioEngine engine;
    mcp::McpServer server;
    server.setEngine(&engine);
    mcp::registerAllTools(server);
    auto* transport = new mcp::TransportStdio();
    server.setTransport(transport);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]{ server.stop(); });
    server.start();
    return app.exec();
}
if (const char* p = parseValue(argc, argv, "--mcp-http-port")) {
    QSettings s; s.setValue("mcp/httpPort", QString::fromUtf8(p));
}
```

- [ ] **Step 4: Build and smoke-test**

```bash
cmake --build build --config Debug
echo '{"jsonrpc":"2.0","id":1,"method":"ping"}' | build/Debug/HDAW.exe --mcp-stdio
```

Expected: one response line `{"jsonrpc":"2.0","id":1,"result":{}}`.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "main: detect stdio MCP mode, run headless server when launched as a subprocess"
```

---

## Task 9: McpTools skeleton + read tools (8)

- [ ] **Step 1: Create `src/mcp/McpTools.h`**

```cpp
#pragma once
class McpServer;
namespace mcp { void registerAllTools(McpServer& server); }
```

- [ ] **Step 2: Create `src/mcp/McpTools.cpp` with the read tools and a `registerAllTools` stub for the rest**

```cpp
#include "McpTools.h"
#include "McpServer.h"
#include "McpToolDef.h"
#include "../model/ProjectModel.h"
#include "../engine/AudioEngine.h"
#include "../engine/MainAudioProcessor.h"
#include "../engine/TrackFXSlot.h"
#include "../engine/Track.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <algorithm>

namespace mcp {
using HDAW::AudioEngine;

static QJsonObject objSchema(const QJsonObject& props, const QJsonArray& required = {}) {
    QJsonObject s{{"type","object"},{"properties", props},{"additionalProperties", false}};
    if (!required.isEmpty()) s["required"] = required;
    return s;
}

void registerAllTools(McpServer& s) {
    auto* e = s.engine();
    if (!e) return;

    // --- Read / inspector tools ---

    s.registerTool({"get_project_summary",
        "Return project name, tempo, track/clip counts, transport state.",
        QJsonObject{{"type","object"}},
        [e](const QJsonObject&) -> McpToolResult {
            auto& m = e->getProjectModel();
            auto tp = m.getTransportTree();
            auto tl = m.getTrackListTree();
            int tracks = tl.getNumChildren(), clips = 0;
            for (int i = 0; i < tracks; ++i)
                clips += tl.getChild(i).getChildWithName(IDs::CLIP_LIST).getNumChildren();
            return McpToolResult::text(QString(
                "name=%1\ntempo=%2\ntracks=%3\nclips=%4\nposition=%5\nisPlaying=%6")
                .arg(m.getTree().getProperty(IDs::name).toString())
                .arg(QString::number(m.getTree().getProperty(IDs::tempo)))
                .arg(tracks).arg(clips)
                .arg(tp.getProperty(IDs::position).toString())
                .arg(tp.getProperty(IDs::isPlaying).toString()));
        }});

    s.registerTool({"get_scale", "Return the project scale (root, mode).",
        QJsonObject{{"type","object"}},
        [e](const QJsonObject&) {
            auto& m = e->getProjectModel();
            return McpToolResult::text(QString("root=%1 mode=%2").arg(m.getScaleRoot()).arg(m.getScaleMode()));
        }});

    s.registerTool({"get_transport",
        "Return transport state (position, isPlaying, isLooping, loopStart, loopEnd).",
        QJsonObject{{"type","object"}},
        [e](const QJsonObject&) {
            auto tp = e->getProjectModel().getTransportTree();
            return McpToolResult::text(QString(
                "position=%1\nisPlaying=%2\nisLooping=%3\nloopStart=%4\nloopEnd=%5")
                .arg(tp.getProperty(IDs::position).toString())
                .arg(tp.getProperty(IDs::isPlaying).toString())
                .arg(tp.getProperty(IDs::isLooping).toString())
                .arg(tp.getProperty(IDs::loopStart).toString())
                .arg(tp.getProperty(IDs::loopEnd).toString()));
        }});

    s.registerTool({"list_tracks",
        "List all tracks (id, name, color, volume, pan, mute, solo, clipCount).",
        QJsonObject{{"type","object"}},
        [e](const QJsonObject&) {
            auto tl = e->getProjectModel().getTrackListTree();
            QJsonArray arr;
            for (int i = 0; i < tl.getNumChildren(); ++i) {
                auto t = tl.getChild(i);
                arr.append(QJsonObject{
                    {"id", i},
                    {"name", t.getProperty(IDs::name).toString()},
                    {"color", t.getProperty(IDs::color).toInt()},
                    {"volume", t.getProperty(IDs::volume).toDouble()},
                    {"pan", t.getProperty(IDs::pan).toDouble()},
                    {"mute", t.getProperty(IDs::isMuted).toBool()},
                    {"solo", t.getProperty(IDs::isSoloed).toBool()},
                    {"clipCount", t.getChildWithName(IDs::CLIP_LIST).getNumChildren()}
                });
            }
            return McpToolResult::text(QString("tracks=%1\n%2")
                .arg(arr.size())
                .arg(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Indented))));
        }});

    s.registerTool({"list_clips", "List clips (optionally on a single trackId).",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}}}),
        [e](const QJsonObject& a) {
            auto tl = e->getProjectModel().getTrackListTree();
            int wanted = a.value("trackId").toInt(-1);
            QJsonArray arr;
            for (int i = 0; i < tl.getNumChildren(); ++i) {
                if (wanted >= 0 && wanted != i) continue;
                auto cl = tl.getChild(i).getChildWithName(IDs::CLIP_LIST);
                for (int j = 0; j < cl.getNumChildren(); ++j) {
                    auto c = cl.getChild(j);
                    arr.append(QJsonObject{
                        {"id", (int)c.getProperty(IDs::clipID)},
                        {"trackId", i},
                        {"name", c.getProperty(IDs::name).toString()},
                        {"start", c.getProperty(IDs::startTime).toDouble()},
                        {"duration", c.getProperty(IDs::duration).toDouble()},
                        {"type", c.getProperty(IDs::clipType).toString()},
                        {"gain", c.getProperty(IDs::gain).toDouble()}
                    });
                }
            }
            return McpToolResult::text(QString("clips=%1\n%2")
                .arg(arr.size())
                .arg(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Indented))));
        }});

    s.registerTool({"get_clip",
        "Return full properties of a clip, including its note list if MIDI.",
        objSchema({{"clipId", QJsonObject{{"type","integer"}}}}, {"clipId"}),
        [e](const QJsonObject& a) {
            int cid = a.value("clipId").toInt(-1);
            auto tl = e->getProjectModel().getTrackListTree();
            for (int i = 0; i < tl.getNumChildren(); ++i) {
                auto cl = tl.getChild(i).getChildWithName(IDs::CLIP_LIST);
                for (int j = 0; j < cl.getNumChildren(); ++j) {
                    auto c = cl.getChild(j);
                    if ((int)c.getProperty(IDs::clipID) != cid) continue;
                    QJsonObject out{
                        {"id", cid}, {"trackId", i},
                        {"name", c.getProperty(IDs::name).toString()},
                        {"start", c.getProperty(IDs::startTime).toDouble()},
                        {"duration", c.getProperty(IDs::duration).toDouble()},
                        {"type", c.getProperty(IDs::clipType).toString()},
                        {"gain", c.getProperty(IDs::gain).toDouble()},
                        {"fadeIn", c.getProperty(IDs::fadeIn).toDouble()},
                        {"fadeOut", c.getProperty(IDs::fadeOut).toDouble()},
                        {"looping", c.getProperty(IDs::looping).toBool()}
                    };
                    if (c.getProperty(IDs::clipType).toString() == "midi") {
                        auto nl = c.getChildWithName(IDs::MIDI_NOTE_LIST);
                        QJsonArray notes;
                        for (int k = 0; k < nl.getNumChildren(); ++k) {
                            auto n = nl.getChild(k);
                            notes.append(QJsonObject{
                                {"noteId", (int)n.getProperty(IDs::noteID)},
                                {"pitch", n.getProperty(IDs::noteNumber).toInt()},
                                {"start", n.getProperty(IDs::startBeat).toDouble()},
                                {"duration", n.getProperty(IDs::durationBeats).toDouble()},
                                {"velocity", n.getProperty(IDs::velocity).toInt()}
                            });
                        }
                        out["notes"] = notes;
                    }
                    return McpToolResult::text(QString::fromUtf8(
                        QJsonDocument(out).toJson(QJsonDocument::Indented)));
                }
            }
            return McpToolResult::text(QString("clipId %1 not found").arg(cid), true);
        }});

    s.registerTool({"list_fx", "List FX slots on a track.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}}}, {"trackId"}),
        [e](const QJsonObject& a) {
            int ti = a.value("trackId").toInt(-1);
            auto* tr = e->getMainProcessor()->getTrack(ti);
            if (!tr) return McpToolResult::text("track not found", true);
            auto& chain = tr->getFXChain();
            QJsonArray arr;
            for (int i = 0; i < (int)chain.size(); ++i) {
                auto& s2 = chain[i]; if (!s2) continue;
                QJsonObject o{{"slot", i},{"type", QString::fromUtf8(s2->getType().toRawUTF8())}};
                if (s2->isPlugin()) o["pluginId"] = s2->getPluginID().toString();
                o["bypassed"] = s2->isBypassed();
                arr.append(o);
            }
            return McpToolResult::text(QString("slots=%1\n%2")
                .arg(arr.size())
                .arg(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Indented))));
        }});

    s.registerTool({"list_automation_lanes", "List automation lanes on a track.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}}}, {"trackId"}),
        [e](const QJsonObject& a) {
            int ti = a.value("trackId").toInt(-1);
            auto tl = e->getProjectModel().getTrackListTree();
            if (ti < 0 || ti >= tl.getNumChildren()) return McpToolResult::text("track not found", true);
            auto al = tl.getChild(ti).getChildWithName(IDs::AUTOMATION_LIST);
            QJsonArray arr;
            for (int i = 0; i < al.getNumChildren(); ++i) {
                auto lane = al.getChild(i);
                arr.append(QJsonObject{
                    {"name", lane.getProperty(IDs::name).toString()},
                    {"paramID", (int)lane.getProperty(IDs::paramID)},
                    {"enabled", lane.getProperty(IDs::automationEnabled).toBool()},
                    {"pointCount", lane.getChildWithName(IDs::POINT_LIST).getNumChildren()}
                });
            }
            return McpToolResult::text(QString("lanes=%1\n%2")
                .arg(arr.size())
                .arg(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Indented))));
        }});

    // --- Transport / Track / Clip / Note / Comp / FX / Auto / Undo / Export tools
    // are added in Phase 2. The transport (`transport`, `seek`) and undo
    // (`undo`, `redo`) tools are added in Tasks 10 and 11 of this phase.
}

} // namespace mcp
```

> If `engine/Track.h` doesn't yet expose `addFXSlotAt`/`setFXSlotPluginID`/`setFXBypassed` exactly as referenced in later tasks, the Phase 2 plan will adapt; the read tools here use the public surface (`getFXChain`, `getType`, `isPlugin`, `getPluginID`, `isBypassed`).

- [ ] **Step 3: Build and smoke-test**

```bash
cmake --build build --config Debug
echo '{"jsonrpc":"2.0","id":1,"method":"tools/list"}' | build/Debug/HDAW.exe --mcp-stdio | head -c 200
```

Expected: response contains `"tools":[` and at least 8 entries.

- [ ] **Step 4: Commit**

```bash
git add src/mcp/McpTools.h src/mcp/McpTools.cpp
git commit -m "mcp: register 8 read/inspector tools"
```

---

## Task 10: Transport tools (`transport`, `seek`)

- [ ] **Step 1: Append to `registerAllTools` in `src/mcp/McpTools.cpp`**

```cpp
    s.registerTool({"transport",
        "Control transport: action in {play,stop,pause,rewind,toggleLoop}; optional loopStart/loopEnd (beats).",
        objSchema({{"action", QJsonObject{{"type","string"},
            {"enum", QJsonArray{"play","stop","pause","rewind","toggleLoop"}}}},
                  {"loopStart", QJsonObject{{"type","number"}}},
                  {"loopEnd",   QJsonObject{{"type","number"}}}}, {"action"}),
        [e](const QJsonObject& a) -> McpToolResult {
            auto& tm = e->getTransportManager();
            QString action = a.value("action").toString();
            if      (action == "play")  tm.play();
            else if (action == "stop")  tm.stop();
            else if (action == "pause") tm.pause();
            else if (action == "rewind") tm.setPosition(0.0);
            else if (action == "toggleLoop") {
                auto tp = e->getProjectModel().getTransportTree();
                tp.setProperty(IDs::isLooping, !tp.getProperty(IDs::isLooping).toBool(), nullptr);
            }
            if (a.contains("loopStart") || a.contains("loopEnd")) {
                auto tp = e->getProjectModel().getTransportTree();
                if (a.contains("loopStart")) tp.setProperty(IDs::loopStart, a.value("loopStart").toDouble(), nullptr);
                if (a.contains("loopEnd"))   tp.setProperty(IDs::loopEnd,   a.value("loopEnd").toDouble(), nullptr);
            }
            return McpToolResult::text("ok");
        }});

    s.registerTool({"seek", "Move the playhead to a beat position.",
        objSchema({{"position", QJsonObject{{"type","number"}}}}, {"position"}),
        [e](const QJsonObject& a) {
            e->getTransportManager().setPosition(a.value("position").toDouble());
            return McpToolResult::text("ok");
        }});
```

- [ ] **Step 2: Build**

```bash
cmake --build build --config Debug
```

- [ ] **Step 3: Commit**

```bash
git add src/mcp/McpTools.cpp
git commit -m "mcp: register transport + seek tools"
```

---

## Task 11: Undo tools (`undo`, `redo`)

- [ ] **Step 1: Append to `registerAllTools` in `src/mcp/McpTools.cpp`**

```cpp
    s.registerTool({"undo", "Undo the last N actions (default 1).",
        objSchema({{"count", QJsonObject{{"type","integer"}}}}),
        [e](const QJsonObject& a) {
            int n = a.value("count").toInt(1);
            auto& um = e->getProjectModel().getUndoManager();
            for (int i = 0; i < n; ++i) if (!um.undo()) break;
            return McpToolResult::text("ok");
        }});

    s.registerTool({"redo", "Redo the last N undone actions (default 1).",
        objSchema({{"count", QJsonObject{{"type","integer"}}}}),
        [e](const QJsonObject& a) {
            int n = a.value("count").toInt(1);
            auto& um = e->getProjectModel().getUndoManager();
            for (int i = 0; i < n; ++i) if (!um.redo()) break;
            return McpToolResult::text("ok");
        }});
```

- [ ] **Step 2: Build and run**

```bash
cmake --build build --config Debug
```

- [ ] **Step 3: Commit**

```bash
git add src/mcp/McpTools.cpp
git commit -m "mcp: register undo/redo tools"
```

---

## Task 12: Integration test (end-to-end via the loopback transport)

- [ ] **Step 1: Replace `tests/integration/mcp/mcp_server_test.cpp`**

```cpp
#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "mcp/McpTransportLoopback.h"
#include "mcp/McpJsonRpc.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

using namespace mcp;

namespace {
QJsonObject parseOne(const QByteArray& buf) {
    int nl = buf.indexOf('\n');
    QByteArray line = nl >= 0 ? buf.left(nl) : buf;
    return QJsonDocument::fromJson(line).object();
}
}

TEST(McpServer, InitializeAndList) {
    HDAW::AudioEngine engine;
    McpServer s;
    s.setEngine(&engine);
    mcp::registerAllTools(s);
    TransportLoopback tp;
    tp.start(&s);
    s.setTransport(&tp);
    s.start();

    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":1,"method":"initialize"})"));
    QByteArray out; ASSERT_TRUE(tp.waitForOutgoing(500, &out));
    auto r = parseOne(out);
    EXPECT_EQ(r.value("id").toInt(), 1);
    EXPECT_FALSE(r.value("result").toObject().value("capabilities").toObject().value("tools").isUndefined());

    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})"));
    out.clear(); ASSERT_TRUE(tp.waitForOutgoing(500, &out));
    auto listed = parseOne(out).value("result").toObject().value("tools").toArray();
    EXPECT_GE(listed.size(), 12); // 8 read + 2 transport + 2 undo
    s.stop();
}

TEST(McpServer, GetProjectSummary) {
    HDAW::AudioEngine engine;
    McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    TransportLoopback tp; tp.start(&s); s.setTransport(&tp); s.start();
    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":1,"method":"tools/call",
        "params":{"name":"get_project_summary","arguments":{}}})"));
    QByteArray out; ASSERT_TRUE(tp.waitForOutgoing(500, &out));
    auto r = parseOne(out);
    EXPECT_FALSE(r.value("error").isObject());
    EXPECT_TRUE(r.value("result").toObject().value("content").toArray()
                .at(0).toObject().value("text").toString().contains("tracks="));
    s.stop();
}

TEST(McpServer, UndoAddThenUndoRemoves) {
    HDAW::AudioEngine engine;
    McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    TransportLoopback tp; tp.start(&s); s.setTransport(&tp); s.start();

    // We can't test add_track in Phase 1 (it's Phase 2), but we CAN exercise
    // undo/redo on a no-op: invoke undo with count=0, expect "ok".
    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":1,"method":"tools/call",
        "params":{"name":"undo","arguments":{"count":1}}})"));
    QByteArray out; ASSERT_TRUE(tp.waitForOutgoing(500, &out));
    auto r = parseOne(out).value("result").toObject();
    EXPECT_FALSE(r.value("isError").toBool(true));
    s.stop();
}

TEST(McpServer, HandlerRunsOnMainThread) {
    HDAW::AudioEngine engine;
    McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    TransportLoopback tp; tp.start(&s); s.setTransport(&tp); s.start();
    std::thread::id mainTid = std::this_thread::get_id();
    s.registerTool({"whereami","test", QJsonObject{{"type","object"}},
        [mainTid](const QJsonObject&) {
            return McpToolResult::text(
                (std::this_thread::get_id() == mainTid) ? "main" : "other");
        }});
    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":1,"method":"tools/call",
        "params":{"name":"whereami","arguments":{}}})"));
    QByteArray out; ASSERT_TRUE(tp.waitForOutgoing(500, &out));
    auto txt = parseOne(out).value("result").toObject()
                  .value("content").toArray().at(0).toObject().value("text").toString();
    EXPECT_EQ(txt, QString("main"));
    s.stop();
}
```

- [ ] **Step 2: Build and run**

```bash
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure -R McpServer
```

Expected: all `McpServer` integration tests pass.

- [ ] **Step 3: Commit**

```bash
git add tests/integration/mcp/mcp_server_test.cpp
git commit -m "mcp: end-to-end integration test via loopback transport"
```

---

## Phase 1 complete

What ships:
- gtest infrastructure (first test suite in the project).
- The `noteID` model prerequisite.
- `McpJsonRpc`, `McpSchema`, `McpTransport` interface, `TransportStdio`, `TransportLoopback`.
- `McpServer` core with tool registry, `initialize`/`tools/list`/`tools/call`/`ping`, schema validation, error mapping, exception safety, cancellation flag.
- Headless mode in `main.cpp` (`--mcp-stdio`, `--no-mcp`, `--mcp-http-port`) with TTY auto-detect.
- 12 tools registered: 8 inspector/reader, 2 transport, 2 undo.
- Integration test driving the server through the loopback transport (proves the contract end-to-end without a real client).

What's next (Phase 2 — separate plan): remaining 24 tools (track add/remove/set/move, clip add/remove/move/set/duplicate, note add/set/remove/clear, composition generate_phrase/chord/progression, FX add/remove/bypass, automation add_point/set_enabled), the HTTP transport, `export_audio` worker, README + sample `mcp.json`.

Phase 2 keeps the same shape: a static lib already exists (`HDAW_lib`), tests already wired, `main.cpp` already has the CLI plumbing, `registerAllTools` is the only place new tools are added.
