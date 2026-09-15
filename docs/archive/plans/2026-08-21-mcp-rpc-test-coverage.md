# MCP/RPC Test Coverage Gap Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the MCP test coverage gap — 126 of 179 MCP tools (70%) have zero protocol-level tests. Add round-trip tests through the MCP loopback transport for every untested tool, grouped by domain.

**Architecture:** All tests use the existing `GuiFuncTest` fixture pattern: `AudioEngine` + `McpServer` + `TransportLoopback`. Each test calls a tool via `call()` / `callText()`, asserts on the response, and verifies state via read-back tools (`list_tracks`, `list_clips`, `get_clip`, `get_transport`, `get_arranger_regions`, etc.). Engine-level tests already cover the command layer — these tests verify the MCP wiring (parameter parsing, handler dispatch, response serialization).

**Tech Stack:** C++17, gtest, Qt 5/6 (QJson*), JUCE 8, HDAW MCP server (loopback transport)

---

## File Structure

All new tests go into **one file**: `tests/integration/mcp/mcp_coverage_test.cpp`
This keeps the new coverage self-contained and avoids merge conflicts with the existing test files. The file uses the same `GuiFuncTest` fixture as `mcp_functionality_test.cpp`.

**Modify:** `tests/integration/mcp/CMakeLists.txt` (or equivalent build file) to add the new test source.

---

## Task 1: Scaffold the test file + region ops (ripple_delete, insert_silence, duplicate_region, loop_clip)

**Files:**
- Create: `tests/integration/mcp/mcp_coverage_test.cpp`
- Modify: build file to include the new test source

These are the highest-priority untested tools — destructive time-range operations that shift content.

- [ ] **Step 1: Create the test file with fixture and helpers**

```cpp
#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "mcp/McpTransportLoopback.h"
#include "mcp/McpJsonRpc.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace {

QJsonObject parseOne(const QByteArray& buf) {
    int nl = buf.indexOf('\n');
    QByteArray line = nl >= 0 ? buf.left(nl) : buf;
    return QJsonDocument::fromJson(line).object();
}

class McpCoverageTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        server = std::make_unique<mcp::McpServer>();
        server->setEngine(engine.get());
        mcp::registerAllTools(*server);
        loopback = std::make_unique<mcp::TransportLoopback>();
        server->setTransport(loopback.get());
        server->start();
    }

    void TearDown() override {
        server->stop();
        server->setTransport(nullptr);
        loopback.reset();
        server.reset();
        engine.reset();
    }

    QJsonObject call(const char* method, const QJsonObject& args = {}) {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = nextId_++;
        req["method"] = "tools/call";
        req["params"] = QJsonObject{{"name", method}, {"arguments", args}};
        loopback->drainOutgoing();
        loopback->pumpIncoming(QJsonDocument(req).toJson(QJsonDocument::Compact));
        QByteArray out;
        if (!loopback->waitForOutgoing(500, &out)) return {};
        auto resp = parseOne(out);
        return resp.value("result").toObject();
    }

    QJsonValue callText(const char* method, const QJsonObject& args = {}) {
        auto r = call(method, args);
        auto content = r.value("content").toArray();
        if (content.isEmpty()) return QJsonValue();
        return content[0].toObject().value("text");
    }

    bool isError(const QJsonObject& r) {
        return r.value("isError").toBool(false);
    }

    QString text(const QJsonObject& r) {
        auto content = r.value("content").toArray();
        if (content.isEmpty()) return {};
        return content[0].toObject().value("text").toString();
    }

    QJsonArray trackList() {
        auto t = callText("list_tracks");
        return QJsonDocument::fromJson(t.toString().toUtf8()).array();
    }

    QJsonArray clipList() {
        auto t = callText("list_clips");
        return QJsonDocument::fromJson(t.toString().toUtf8()).array();
    }

    int trackCount() { return trackList().size(); }
    int clipCount() { return clipList().size(); }

    QJsonObject findClip(int clipId) {
        auto clips = clipList();
        for (const auto& c : clips) {
            if (c.toObject().value("id").toInt() == clipId)
                return c.toObject();
        }
        return {};
    }

    QJsonObject findTrack(int index) {
        auto tracks = trackList();
        for (const auto& t : tracks) {
            if (t.toObject().value("id").toInt() == index)
                return t.toObject();
        }
        return {};
    }

    QJsonArray getNotes(int clipId) {
        auto r = callText("get_clip", {{"clipId", clipId}});
        auto obj = QJsonDocument::fromJson(r.toString().toUtf8()).object();
        return obj.value("notes").toArray();
    }

    // Add a MIDI clip and return its clipId
    int addMidiClip(int trackId, double start, double length, const QString& name = {}) {
        QJsonObject args{{"trackId", trackId}, {"start", start}, {"length", length}};
        if (!name.isEmpty()) args["name"] = name;
        auto r = call("add_midi_clip", args);
        if (isError(r)) return -1;
        QString resp = text(r);
        return resp.mid(resp.indexOf('=') + 1).toInt();
    }

    // Add a note and return its noteId
    int addNote(int clipId, int pitch, double start, double duration, int velocity = 100) {
        auto r = call("add_note", {{"clipId", clipId}, {"pitch", pitch},
                                   {"start", start}, {"duration", duration}, {"velocity", velocity}});
        if (isError(r)) return -1;
        QString resp = text(r);
        return resp.mid(resp.indexOf('=') + 1).toInt();
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;
    std::unique_ptr<mcp::TransportLoopback> loopback;
    int nextId_ = 1;
};
```

- [ ] **Step 2: Add ripple_delete test**

```cpp
// ============================================================================
// REGION OPS
// ============================================================================

TEST_F(McpCoverageTest, RippleDelete) {
    // Create two clips on track 0: one at 0-4, one at 4-8
    int clip1 = addMidiClip(0, 0.0, 4.0, "A");
    int clip2 = addMidiClip(0, 4.0, 4.0, "B");
    ASSERT_GT(clip1, 0);
    ASSERT_GT(clip2, 0);

    int before = clipCount();
    // Ripple-delete beats 0-4 — should remove clip1 and shift clip2 left
    auto r = call("ripple_delete", {{"startBeat", 0.0}, {"endBeat", 4.0}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();

    // Clip2 should now start at 0
    auto c = findClip(clip2);
    EXPECT_FALSE(c.isEmpty());
    EXPECT_NEAR(c.value("start").toDouble(), 0.0, 0.01);
}
```

- [ ] **Step 3: Add insert_silence test**

```cpp
TEST_F(McpCoverageTest, InsertSilence) {
    int clip1 = addMidiClip(0, 0.0, 4.0, "A");
    ASSERT_GT(clip1, 0);

    // Insert 4 beats of silence at beat 0 — clip should shift to beat 4
    auto r = call("insert_silence", {{"startBeat", 0.0}, {"endBeat", 4.0}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();

    auto c = findClip(clip1);
    EXPECT_FALSE(c.isEmpty());
    EXPECT_NEAR(c.value("start").toDouble(), 4.0, 0.01);
}
```

- [ ] **Step 4: Add duplicate_region test**

```cpp
TEST_F(McpCoverageTest, DuplicateRegion) {
    int clip1 = addMidiClip(0, 0.0, 4.0, "A");
    ASSERT_GT(clip1, 0);

    int before = clipCount();
    // Duplicate region [0, 4) and paste after beat 4
    auto r = call("duplicate_region", {{"startBeat", 0.0}, {"endBeat", 4.0}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();

    // Should have one more clip
    EXPECT_GE(clipCount(), before + 1);
}
```

- [ ] **Step 5: Add loop_clip test**

```cpp
TEST_F(McpCoverageTest, LoopClip) {
    int clipId = addMidiClip(0, 0.0, 4.0, "Loop");
    ASSERT_GT(clipId, 0);
    addNote(clipId, 60, 0.0, 1.0);

    // Loop 3 times -> duration should become 12
    auto r = call("loop_clip", {{"clipId", clipId}, {"repetitions", 3}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();

    auto c = findClip(clipId);
    EXPECT_FALSE(c.isEmpty());
    EXPECT_NEAR(c.value("duration").toDouble(), 12.0, 0.01);

    // Notes should be tripled
    auto notes = getNotes(clipId);
    EXPECT_EQ(notes.size(), 3);
}
```

- [ ] **Step 6: Build and run**

Run: `cmake --build build --config Debug; if ($?) { build/Debug/hdaw_tests.exe --gtest_filter=McpCoverageTest.* }`
Expected: All 4 tests PASS.

---

## Task 2: Track ops (move_track, duplicate_track, add_track_with_fx)

**Files:**
- Modify: `tests/integration/mcp/mcp_coverage_test.cpp`

- [ ] **Step 1: Add move_track test**

```cpp
// ============================================================================
// TRACK OPS
// ============================================================================

TEST_F(McpCoverageTest, MoveTrack) {
    call("add_track", {{"name", "A"}});
    call("add_track", {{"name", "B"}});
    call("add_track", {{"name", "C"}});
    int count = trackCount();

    // Move last track to index 0
    auto r = call("move_track", {{"trackId", count - 1}, {"newIndex", 0}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();

    auto t = findTrack(0);
    EXPECT_EQ(t.value("name").toString().toStdString(), "C");
}
```

- [ ] **Step 2: Add duplicate_track test**

```cpp
TEST_F(McpCoverageTest, DuplicateTrack) {
    call("add_midi_clip", {{"trackId", 0}, {"start", 0.0}, {"length", 4.0}});
    int before = trackCount();

    auto r = call("duplicate_track", {{"trackId", 0}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
    EXPECT_EQ(trackCount(), before + 1);

    // Duplicated track should have same clip count
    auto t = findTrack(trackCount() - 1);
    EXPECT_EQ(t.value("clipCount").toInt(), 1);
}
```

- [ ] **Step 3: Add add_track_with_fx test**

```cpp
TEST_F(McpCoverageTest, AddTrackWithFx) {
    int before = trackCount();
    auto r = call("add_track_with_fx", {{"name", "EQ Track"}, {"fxType", "eq"}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
    EXPECT_EQ(trackCount(), before + 1);

    // Verify FX was added
    auto fxText = callText("list_fx", {{"trackId", trackCount() - 1}});
    auto fxArr = QJsonDocument::fromJson(fxText.toString().toUtf8()).array();
    EXPECT_GE(fxArr.size(), 1);
}
```

- [ ] **Step 4: Build and run**

Run: `cmake --build build --config Debug; if ($?) { build/Debug/hdaw_tests.exe --gtest_filter=McpCoverageTest.MoveTrack:McpCoverageTest.DuplicateTrack:McpCoverageTest.AddTrackWithFx }`
Expected: All 3 tests PASS.

---

## Task 3: Note operators (10 tools + set_clip_seed + set_note_velocities)

**Files:**
- Modify: `tests/integration/mcp/mcp_coverage_test.cpp`

- [ ] **Step 1: Add note operators test block**

```cpp
// ============================================================================
// NOTE OPERATORS
// ============================================================================

TEST_F(McpCoverageTest, SetNoteChance) {
    int clipId = addMidiClip(0, 0.0, 4.0);
    int noteId = addNote(clipId, 60, 0.0, 1.0);
    ASSERT_GT(noteId, 0);

    auto r = call("set_note_chance", {{"noteId", noteId}, {"chance", 0.5}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(McpCoverageTest, SetNoteRepeatCount) {
    int clipId = addMidiClip(0, 0.0, 4.0);
    int noteId = addNote(clipId, 60, 0.0, 1.0);
    ASSERT_GT(noteId, 0);

    auto r = call("set_note_repeat_count", {{"noteId", noteId}, {"repeatCount", 4}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(McpCoverageTest, SetNoteRepeatRate) {
    int clipId = addMidiClip(0, 0.0, 4.0);
    int noteId = addNote(clipId, 60, 0.0, 1.0);
    ASSERT_GT(noteId, 0);

    auto r = call("set_note_repeat_rate", {{"noteId", noteId}, {"repeatRate", 0.25}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(McpCoverageTest, SetNoteRepeatCurve) {
    int clipId = addMidiClip(0, 0.0, 4.0);
    int noteId = addNote(clipId, 60, 0.0, 1.0);
    ASSERT_GT(noteId, 0);

    auto r = call("set_note_repeat_curve", {{"noteId", noteId}, {"repeatCurve", 0.5}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(McpCoverageTest, SetNoteOccurrence) {
    int clipId = addMidiClip(0, 0.0, 4.0);
    int noteId = addNote(clipId, 60, 0.0, 1.0);
    ASSERT_GT(noteId, 0);

    auto r = call("set_note_occurrence", {{"noteId", noteId}, {"occurrence", 5}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(McpCoverageTest, SetNoteRecurrence) {
    int clipId = addMidiClip(0, 0.0, 4.0);
    int noteId = addNote(clipId, 60, 0.0, 1.0);
    ASSERT_GT(noteId, 0);

    auto r = call("set_note_recurrence", {{"noteId", noteId}, {"recurrence", 1}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(McpCoverageTest, SetNoteGain) {
    int clipId = addMidiClip(0, 0.0, 4.0);
    int noteId = addNote(clipId, 60, 0.0, 1.0);
    ASSERT_GT(noteId, 0);

    auto r = call("set_note_gain", {{"noteId", noteId}, {"gain", 1.5}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(McpCoverageTest, SetNotePan) {
    int clipId = addMidiClip(0, 0.0, 4.0);
    int noteId = addNote(clipId, 60, 0.0, 1.0);
    ASSERT_GT(noteId, 0);

    auto r = call("set_note_pan", {{"noteId", noteId}, {"pan", -0.5}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(McpCoverageTest, SetNotePitchOffset) {
    int clipId = addMidiClip(0, 0.0, 4.0);
    int noteId = addNote(clipId, 60, 0.0, 1.0);
    ASSERT_GT(noteId, 0);

    auto r = call("set_note_pitch_offset", {{"noteId", noteId}, {"pitchOffset", 2.0}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(McpCoverageTest, SetNoteTimbre) {
    int clipId = addMidiClip(0, 0.0, 4.0);
    int noteId = addNote(clipId, 60, 0.0, 1.0);
    ASSERT_GT(noteId, 0);

    auto r = call("set_note_timbre", {{"noteId", noteId}, {"timbre", 0.7}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(McpCoverageTest, SetNotePressure) {
    int clipId = addMidiClip(0, 0.0, 4.0);
    int noteId = addNote(clipId, 60, 0.0, 1.0);
    ASSERT_GT(noteId, 0);

    auto r = call("set_note_pressure", {{"noteId", noteId}, {"pressure", 0.8}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(McpCoverageTest, SetClipSeed) {
    int clipId = addMidiClip(0, 0.0, 4.0);
    ASSERT_GT(clipId, 0);

    auto r = call("set_clip_seed", {{"clipId", clipId}, {"seed", 42}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(McpCoverageTest, SetNoteVelocitiesAbsolute) {
    int clipId = addMidiClip(0, 0.0, 4.0);
    addNote(clipId, 60, 0.0, 1.0, 80);
    addNote(clipId, 62, 1.0, 1.0, 90);
    addNote(clipId, 64, 2.0, 1.0, 70);

    auto r = call("set_note_velocities", {{"clipId", clipId}, {"velocity", 100}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();

    auto notes = getNotes(clipId);
    for (const auto& n : notes)
        EXPECT_EQ(n.toObject().value("velocity").toInt(), 100);
}

TEST_F(McpCoverageTest, SetNoteVelocitiesRelative) {
    int clipId = addMidiClip(0, 0.0, 4.0);
    addNote(clipId, 60, 0.0, 1.0, 80);

    auto r = call("set_note_velocities", {{"clipId", clipId}, {"velocityOffset", 10}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();

    auto notes = getNotes(clipId);
    EXPECT_EQ(notes[0].toObject().value("velocity").toInt(), 90);
}

TEST_F(McpCoverageTest, SetNoteVelocitiesRandom) {
    int clipId = addMidiClip(0, 0.0, 4.0);
    addNote(clipId, 60, 0.0, 1.0, 80);

    auto r = call("set_note_velocities", {
        {"clipId", clipId}, {"velocityMin", 60}, {"velocityMax", 120}
    });
    EXPECT_FALSE(isError(r)) << text(r).toStdString();

    auto notes = getNotes(clipId);
    int vel = notes[0].toObject().value("velocity").toInt();
    EXPECT_GE(vel, 60);
    EXPECT_LE(vel, 120);
}
```

- [ ] **Step 2: Build and run**

Run: `cmake --build build --config Debug; if ($?) { build/Debug/hdaw_tests.exe --gtest_filter=McpCoverageTest.SetNote*:McpCoverageTest.SetClipSeed }`
Expected: All 15 tests PASS.

---

## Task 4: Tempo points (4 tools)

**Files:**
- Modify: `tests/integration/mcp/mcp_coverage_test.cpp`

- [ ] **Step 1: Add tempo points test block**

```cpp
// ============================================================================
// TEMPO POINTS
// ============================================================================

TEST_F(McpCoverageTest, AddTempoPoint) {
    auto r = call("add_tempo_point", {{"timeSeconds", 2.0}, {"bpm", 140.0}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();

    auto t = QJsonDocument::fromJson(callText("get_transport").toString().toUtf8()).object();
    auto points = t.value("tempoPoints").toArray();
    ASSERT_GE(points.size(), 2); // default + added
}

TEST_F(McpCoverageTest, RemoveTempoPoint) {
    call("add_tempo_point", {{"timeSeconds", 2.0}, {"bpm", 140.0}});
    auto r = call("remove_tempo_point", {{"index", 1}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(McpCoverageTest, SetTempoPointBpm) {
    call("add_tempo_point", {{"timeSeconds", 2.0}, {"bpm", 140.0}});
    auto r = call("set_tempo_point_bpm", {{"index", 1}, {"bpm", 160.0}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(McpCoverageTest, SetTempoPointTime) {
    call("add_tempo_point", {{"timeSeconds", 2.0}, {"bpm", 140.0}});
    auto r = call("set_tempo_point_time", {{"index", 1}, {"timeSeconds", 4.0}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}
```

- [ ] **Step 2: Build and run**

Run: `cmake --build build --config Debug; if ($?) { build/Debug/hdaw_tests.exe --gtest_filter=McpCoverageTest.*TempoPoint* }`
Expected: All 4 tests PASS.

---

## Task 5: Arranger domain (16 tools)

**Files:**
- Modify: `tests/integration/mcp/mcp_coverage_test.cpp`

- [ ] **Step 1: Add arranger test block**

```cpp
// ============================================================================
// ARRANGER
// ============================================================================

TEST_F(McpCoverageTest, ArrangerRegionCrud) {
    // Add region
    auto addR = call("add_arranger_region", {{"name", "Intro"}, {"startTime", 0.0}, {"duration", 8.0}});
    EXPECT_FALSE(isError(addR)) << text(addR).toStdString();
    QString regionId = text(addR);

    // List regions
    auto listR = QJsonDocument::fromJson(
        callText("get_arranger_regions").toString().toUtf8()).array();
    ASSERT_EQ(listR.size(), 1);
    EXPECT_EQ(listR[0].toObject().value("name").toString().toStdString(), "Intro");

    // Rename
    auto nameR = call("set_arranger_region_name", {{"regionID", regionId}, {"name", "Verse"}});
    EXPECT_FALSE(isError(nameR)) << text(nameR).toStdString();

    // Move/resize
    auto boundsR = call("set_arranger_region_bounds", {
        {"regionID", regionId}, {"startTime", 4.0}, {"duration", 16.0}
    });
    EXPECT_FALSE(isError(boundsR)) << text(boundsR).toStdString();

    // Recolor
    auto colorR = call("set_arranger_region_color", {{"regionID", regionId}, {"color", 0xFFFF0000}});
    EXPECT_FALSE(isError(colorR)) << text(colorR).toStdString();

    // Verify
    listR = QJsonDocument::fromJson(
        callText("get_arranger_regions").toString().toUtf8()).array();
    EXPECT_EQ(listR[0].toObject().value("name").toString().toStdString(), "Verse");
    EXPECT_NEAR(listR[0].toObject().value("startTime").toDouble(), 4.0, 0.01);
    EXPECT_NEAR(listR[0].toObject().value("duration").toDouble(), 16.0, 0.01);

    // Remove
    auto removeR = call("remove_arranger_region", {{"regionID", regionId}});
    EXPECT_FALSE(isError(removeR)) << text(removeR).toStdString();
    listR = QJsonDocument::fromJson(
        callText("get_arranger_regions").toString().toUtf8()).array();
    EXPECT_EQ(listR.size(), 0);
}

TEST_F(McpCoverageTest, ArrangerChainCrud) {
    // Add chain
    auto addR = call("add_arranger_chain", {{"name", "Arrangement A"}});
    EXPECT_FALSE(isError(addR)) << text(addR).toStdString();
    QString chainId = text(addR);

    // List chains
    auto listR = QJsonDocument::fromJson(
        callText("get_arranger_chains").toString().toUtf8()).array();
    ASSERT_EQ(listR.size(), 1);
    EXPECT_EQ(listR[0].toObject().value("name").toString().toStdString(), "Arrangement A");

    // Rename
    auto nameR = call("set_arranger_chain_name", {{"chainID", chainId}, {"name", "Main"}});
    EXPECT_FALSE(isError(nameR)) << text(nameR).toStdString();

    // Add second chain, activate it
    auto addR2 = call("add_arranger_chain", {{"name", "Alt"}});
    QString chainId2 = text(addR2);
    auto actR = call("set_arranger_chain_active", {{"chainID", chainId2}});
    EXPECT_FALSE(isError(actR)) << text(actR).toStdString();

    // Verify only one active
    listR = QJsonDocument::fromJson(
        callText("get_arranger_chains").toString().toUtf8()).array();
    int activeCount = 0;
    for (const auto& c : listR)
        if (c.toObject().value("isActive").toBool()) activeCount++;
    EXPECT_EQ(activeCount, 1);

    // Remove
    auto removeR = call("remove_arranger_chain", {{"chainID", chainId}});
    EXPECT_FALSE(isError(removeR)) << text(removeR).toStdString();
}

TEST_F(McpCoverageTest, ArrangerChainEntries) {
    // Setup: region + chain
    auto regR = call("add_arranger_region", {{"name", "A"}, {"startTime", 0.0}, {"duration", 4.0}});
    QString regionId = text(regR);
    auto chainR = call("add_arranger_chain", {{"name", "C"}});
    QString chainId = text(chainR);

    // Add entry
    auto addEntry = call("add_chain_entry", {{"chainID", chainId}, {"regionID", regionId}});
    EXPECT_FALSE(isError(addEntry)) << text(addEntry).toStdString();

    // Add second entry
    auto regR2 = call("add_arranger_region", {{"name", "B"}, {"startTime", 4.0}, {"duration", 4.0}});
    QString regionId2 = text(regR2);
    call("add_chain_entry", {{"chainID", chainId}, {"regionID", regionId2}});

    // Verify 2 entries
    auto chains = QJsonDocument::fromJson(
        callText("get_arranger_chains").toString().toUtf8()).array();
    auto entries = chains[0].toObject().value("entries").toArray();
    ASSERT_EQ(entries.size(), 2);

    // Reorder
    auto reorderR = call("reorder_chain_entry", {
        {"chainID", chainId}, {"fromIndex", 0}, {"toIndex", 1}
    });
    EXPECT_FALSE(isError(reorderR)) << text(reorderR).toStdString();

    // Set repeat
    auto repeatR = call("set_chain_entry_repeat", {
        {"chainID", chainId}, {"entryIndex", 0}, {"repeatCount", 3}
    });
    EXPECT_FALSE(isError(repeatR)) << text(repeatR).toStdString();

    // Remove entry
    auto removeEntry = call("remove_chain_entry", {{"chainID", chainId}, {"entryIndex", 0}});
    EXPECT_FALSE(isError(removeEntry)) << text(removeEntry).toStdString();
}

TEST_F(McpCoverageTest, FlattenArranger) {
    // Add region + chain + entry, then flatten
    auto regR = call("add_arranger_region", {{"name", "A"}, {"startTime", 0.0}, {"duration", 4.0}});
    QString regionId = text(regR);
    auto chainR = call("add_arranger_chain", {{"name", "C"}});
    QString chainId = text(chainR);
    call("add_chain_entry", {{"chainID", chainId}, {"regionID", regionId}});

    int clipsBefore = clipCount();
    auto r = call("flatten_arranger");
    EXPECT_FALSE(isError(r)) << text(r).toStdString();

    // Should have created clips from the chain
    EXPECT_GT(clipCount(), clipsBefore);
}
```

- [ ] **Step 2: Build and run**

Run: `cmake --build build --config Debug; if ($?) { build/Debug/hdaw_tests.exe --gtest_filter=McpCoverageTest.Arranger* }`
Expected: All 4 tests PASS.

---

## Task 6: Sends (4 tools)

**Files:**
- Modify: `tests/integration/mcp/mcp_coverage_test.cpp`

- [ ] **Step 1: Add sends test block**

```cpp
// ============================================================================
// SENDS
// ============================================================================

TEST_F(McpCoverageTest, SendAddSetRemove) {
    // Add a second track as send target
    call("add_track", {{"name", "FX Bus"}});
    int targetTrack = trackCount() - 1;

    // Add a send from track 0 to target track
    // (Sends are added via ValueTree — the MCP set_track_send_* tools
    //  operate on existing sends. We need to add one via the model first.)
    // For now, test the read path and the set operations.
    auto sendsR = callText("get_track_sends", {{"trackId", 0}});
    auto sends = QJsonDocument::fromJson(sendsR.toString().toUtf8()).array();
    // Default project has no sends — this tests the empty case
    EXPECT_EQ(sends.size(), 0);
}
```

Note: The send tools (`set_track_send_level`, `set_track_send_mode`, `set_track_send_bypassed`) require a send to exist first. The engine-level `send_test.cpp` creates sends via ValueTree manipulation. The MCP test should verify the tools don't crash on a track with no sends, and if the test fixture can add sends (e.g. via `add_track_with_fx` + model manipulation), test the full round-trip.

- [ ] **Step 2: Build and run**

Run: `cmake --build build --config Debug; if ($?) { build/Debug/hdaw_tests.exe --gtest_filter=McpCoverageTest.Send* }`
Expected: PASS.

---

## Task 7: MIDI FX (6 tools)

**Files:**
- Modify: `tests/integration/mcp/mcp_coverage_test.cpp`

- [ ] **Step 1: Add MIDI FX test block**

```cpp
// ============================================================================
// MIDI FX
// ============================================================================

TEST_F(McpCoverageTest, MidiFxAddListBypassRemove) {
    // Add arpeggiator
    auto addR = call("add_midi_fx", {{"trackId", 0}, {"fxType", "arpeggiator"}});
    EXPECT_FALSE(isError(addR)) << text(addR).toStdString();
    int slot = text(addR).mid(text(addR).indexOf('=') + 1).toInt();

    // List params
    auto paramsR = callText("list_midi_fx_params", {{"trackId", 0}, {"slotIndex", slot}});
    auto params = QJsonDocument::fromJson(paramsR.toString().toUtf8()).array();
    EXPECT_GT(params.size(), 0);

    // Bypass
    auto bypassR = call("set_midi_fx_bypass", {
        {"trackId", 0}, {"slotIndex", slot}, {"bypassed", true}
    });
    EXPECT_FALSE(isError(bypassR)) << text(bypassR).toStdString();

    // Set param by name
    auto setR = call("set_midi_fx_param", {
        {"trackId", 0}, {"slotIndex", slot}, {"paramName", "rate"}, {"value", 0.5}
    });
    EXPECT_FALSE(isError(setR)) << text(setR).toStdString();

    // Set param normalized
    auto setNormR = call("set_midi_fx_param_normalized", {
        {"trackId", 0}, {"slotIndex", slot}, {"paramIndex", 0}, {"value", 0.75}
    });
    EXPECT_FALSE(isError(setNormR)) << text(setNormR).toStdString();

    // Remove
    auto removeR = call("remove_midi_fx", {{"trackId", 0}, {"slotIndex", slot}});
    EXPECT_FALSE(isError(removeR)) << text(removeR).toStdString();
}
```

- [ ] **Step 2: Build and run**

Run: `cmake --build build --config Debug; if ($?) { build/Debug/hdaw_tests.exe --gtest_filter=McpCoverageTest.MidiFx* }`
Expected: PASS.

---

## Task 8: Session (5 tools)

**Files:**
- Modify: `tests/integration/mcp/mcp_coverage_test.cpp`

- [ ] **Step 1: Add session test block**

```cpp
// ============================================================================
// SESSION
// ============================================================================

TEST_F(McpCoverageTest, SessionClipLifecycle) {
    // Create a session clip
    auto createR = call("session_create_clip", {{"trackIndex", 0}, {"sceneIndex", 0}});
    EXPECT_FALSE(isError(createR)) << text(createR).toStdString();

    // Get clip states
    auto statesR = callText("session_get_clip_states");
    auto states = QJsonDocument::fromJson(statesR.toString().toUtf8()).array();
    EXPECT_GT(states.size(), 0);

    // Set clip scene
    int clipId = text(createR).mid(text(createR).indexOf('=') + 1).toInt();
    auto setSceneR = call("session_set_clip_scene", {{"clipId", clipId}, {"sceneIndex", 1}});
    EXPECT_FALSE(isError(setSceneR)) << text(setSceneR).toStdString();

    // Launch scene
    auto launchR = call("session_launch_scene", {{"sceneIndex", 1}});
    EXPECT_FALSE(isError(launchR)) << text(launchR).toStdString();

    // Stop all
    auto stopR = call("session_stop_all");
    EXPECT_FALSE(isError(stopR)) << text(stopR).toStdString();
}
```

- [ ] **Step 2: Build and run**

Run: `cmake --build build --config Debug; if ($?) { build/Debug/hdaw_tests.exe --gtest_filter=McpCoverageTest.Session* }`
Expected: PASS.

---

## Task 9: Library (7 tools)

**Files:**
- Modify: `tests/integration/mcp/mcp_coverage_test.cpp`

- [ ] **Step 1: Add library test block**

```cpp
// ============================================================================
// LIBRARY
// ============================================================================

TEST_F(McpCoverageTest, LibraryAddListSearchRemove) {
    // Create a temp dir with a test file
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    // Add library
    auto addR = call("add_library", {
        {"name", "TestLib"}, {"path", dir.path()}, {"type", "midi"}
    });
    EXPECT_FALSE(isError(addR)) << text(addR).toStdString();
    QString libId = text(addR);

    // List libraries
    auto listR = QJsonDocument::fromJson(
        callText("list_libraries").toString().toUtf8()).array();
    EXPECT_GT(listR.size(), 0);

    // Set autoscan
    auto autoscanR = call("set_library_autoscan", {{"id", libId}, {"enabled", true}});
    EXPECT_FALSE(isError(autoscanR)) << text(autoscanR).toStdString();

    // Scan
    auto scanR = call("scan_library", {{"id", libId}});
    EXPECT_FALSE(isError(scanR)) << text(scanR).toStdString();

    // Search (empty library)
    auto searchR = call("search_library", {{"query", "test"}});
    EXPECT_FALSE(isError(searchR)) << text(searchR).toStdString();

    // Remove
    auto removeR = call("remove_library", {{"id", libId}});
    EXPECT_FALSE(isError(removeR)) << text(removeR).toStdString();

    // Verify removed
    listR = QJsonDocument::fromJson(
        callText("list_libraries").toString().toUtf8()).array();
    for (const auto& l : listR)
        EXPECT_NE(l.toObject().value("id").toString(), libId);
}
```

- [ ] **Step 2: Build and run**

Run: `cmake --build build --config Debug; if ($?) { build/Debug/hdaw_tests.exe --gtest_filter=McpCoverageTest.Library* }`
Expected: PASS.

---

## Task 10: Remaining tools (set_automation_points, generate_arrangement, project_info)

**Files:**
- Modify: `tests/integration/mcp/mcp_coverage_test.cpp`

- [ ] **Step 1: Add remaining tools test block**

```cpp
// ============================================================================
// REMAINING TOOLS
// ============================================================================

TEST_F(McpCoverageTest, SetAutomationPoints) {
    // Add automation lane for Volume
    auto laneR = call("add_automation_lane", {{"trackId", 0}, {"lane", "Volume"}});
    EXPECT_FALSE(isError(laneR)) << text(laneR).toStdString();

    // Set points (replace mode)
    QJsonArray points;
    points.append(QJsonObject{{"time", 0.0}, {"value", 0.8}});
    points.append(QJsonObject{{"time", 4.0}, {"value", 0.5}});
    auto setR = call("set_automation_points", {
        {"trackId", 0}, {"lane", "Volume"}, {"points", points}
    });
    EXPECT_FALSE(isError(setR)) << text(setR).toStdString();

    // Append mode
    QJsonArray morePoints;
    morePoints.append(QJsonObject{{"time", 8.0}, {"value", 0.3}});
    auto appendR = call("set_automation_points", {
        {"trackId", 0}, {"lane", "Volume"}, {"points", morePoints}, {"mode", "append"}
    });
    EXPECT_FALSE(isError(appendR)) << text(appendR).toStdString();
}

TEST_F(McpCoverageTest, GenerateArrangement) {
    auto r = call("generate_arrangement", {{"bars", 8}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();

    // Should have created tracks/clips
    EXPECT_GT(trackCount(), 3); // default has 3
}

TEST_F(McpCoverageTest, ProjectInfo) {
    auto r = call("project_info");
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}
```

- [ ] **Step 2: Build and run**

Run: `cmake --build build --config Debug; if ($?) { build/Debug/hdaw_tests.exe --gtest_filter=McpCoverageTest.SetAutomationPoints:McpCoverageTest.GenerateArrangement:McpCoverageTest.ProjectInfo }`
Expected: All 3 tests PASS.

---

## Task 11: Run full suite + verify no regressions

**Files:** None (verification only)

- [ ] **Step 1: Run all new tests**

Run: `cmake --build build --config Debug; if ($?) { build/Debug/hdaw_tests.exe --gtest_filter=McpCoverageTest.* }`
Expected: All tests PASS (count should be ~35).

- [ ] **Step 2: Run the full MCP test suite to verify no regressions**

Run: `build/Debug/hdaw_tests.exe --gtest_filter=McpServer.*:GuiFuncTest.*:McpCoverageTest.*`
Expected: All tests PASS. No regressions in existing MCP tests.

- [ ] **Step 3: Run the full test suite**

Run: `build/Debug/hdaw_tests.exe`
Expected: No new failures.

---

## Test Count Summary

| Task | Tests | Tools Covered |
|------|-------|---------------|
| 1: Region ops | 4 | ripple_delete, insert_silence, duplicate_region, loop_clip |
| 2: Track ops | 3 | move_track, duplicate_track, add_track_with_fx |
| 3: Note operators | 15 | 10 note operators + set_clip_seed + set_note_velocities (3 modes) |
| 4: Tempo points | 4 | add/remove/set_bpm/set_time |
| 5: Arranger | 4 | 16 arranger tools (CRUD + entries + flatten) |
| 6: Sends | 1 | get_track_sends (set ops need send fixture) |
| 7: MIDI FX | 1 | 6 MIDI FX tools (add/list/bypass/set/remove) |
| 8: Session | 1 | 5 session tools |
| 9: Library | 1 | 7 library tools |
| 10: Remaining | 3 | set_automation_points, generate_arrangement, project_info |
| **Total** | **37** | **~60 tools** |

Remaining untested after this plan (~66 tools): Settings domain (audio/MIDI device, preview, blacklist, FX snapshots), FM synth MCP tools, plugin preset management, pool_list, get_waveform_peaks, list_clip_takes, switch_clip_take. These are lower-risk (read-only or settings) and can be a follow-up plan.
