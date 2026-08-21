import { describe, it, expect, beforeEach, vi } from "vitest";
import { useUiStore, MIN_BOTTOM_PANEL_H } from "../store/uiStore";
import { ClipSnapshot } from "../rpc/types";

const makeClip = (clipId: number, trackIndex: number): ClipSnapshot => ({
  clipId,
  trackIndex,
  name: `Clip ${clipId}`,
  sourceFile: "",
  startBeat: 0,
  durationBeats: 4,
  offset: 0,
  gain: 1,
  fadeIn: 0,
  fadeOut: 0,
  looping: false,
  muted: false,
  isMidi: true,
  sourceBpm: 0,
  stretchMode: 0,
  stretchRatio: 1,
  sourceDuration: 0,
  isGhost: false,
  ghostSourceId: 0,
  gainEnvelope: [],
});

describe("uiStore", () => {
  beforeEach(() => {
    localStorage.removeItem("hdaw_bottom_panel_h");
    localStorage.removeItem("hdaw_bottom_panel_h_per_tab");
    localStorage.removeItem("hdaw_view_mode");
    localStorage.removeItem("hdaw_last_bottom_tab");
    localStorage.removeItem("hdaw_snapEnabled");
    localStorage.removeItem("hdaw_snapDivision");
    localStorage.removeItem("hdaw_snapGridOffset");
    localStorage.removeItem("hdaw_snapToEvents");
    useUiStore.setState({
      selectedClipIds: new Set(),
      lastSelectedClipId: null,
      selectedTrackIndex: null,
      clipClipboard: [],
      activeBottomTab: "mixer",
      snapEnabled: true,
      snapDivision: 1,
      snapGridOffset: false,
      snapToEvents: false,
      showPhraseGenerator: false,
      bottomPanelHeights: {},
      viewMode: "arrange",
      statusHint: null,
    });
  });

  it("selects a single clip", () => {
    useUiStore.getState().selectClip(42, 0);
    const state = useUiStore.getState();
    expect(state.selectedClipIds.has(42)).toBe(true);
    expect(state.selectedClipIds.size).toBe(1);
    expect(state.lastSelectedClipId).toBe(42);
    expect(state.selectedTrackIndex).toBe(0);
  });

  it("clears selection when selecting null", () => {
    useUiStore.getState().selectClip(42, 0);
    useUiStore.getState().selectClip(null);
    const state = useUiStore.getState();
    expect(state.selectedClipIds.size).toBe(0);
    expect(state.lastSelectedClipId).toBeNull();
  });

  it("toggles clip selection", () => {
    useUiStore.getState().selectClip(1, 0);
    useUiStore.getState().toggleClipSelection(2);
    expect(useUiStore.getState().selectedClipIds.size).toBe(2);

    useUiStore.getState().toggleClipSelection(1);
    expect(useUiStore.getState().selectedClipIds.size).toBe(1);
    expect(useUiStore.getState().selectedClipIds.has(2)).toBe(true);
  });

  it("clears selection", () => {
    useUiStore.getState().selectClip(1, 0);
    useUiStore.getState().toggleClipSelection(2);
    useUiStore.getState().clearSelection();
    expect(useUiStore.getState().selectedClipIds.size).toBe(0);
  });

  it("sets active bottom tab", () => {
    useUiStore.getState().setActiveBottomTab("pianoRoll");
    expect(useUiStore.getState().activeBottomTab).toBe("pianoRoll");
  });

  it("sets snap state", () => {
    useUiStore.getState().setSnapEnabled(false);
    expect(useUiStore.getState().snapEnabled).toBe(false);

    useUiStore.getState().setSnapDivision(4);
    expect(useUiStore.getState().snapDivision).toBe(4);
  });

  it("selects range of clips", () => {
    const clips = [makeClip(1, 0), makeClip(2, 0), makeClip(3, 0)];
    useUiStore.getState().selectClip(1, 0);
    useUiStore.getState().selectRange(1, 3, clips);
    expect(useUiStore.getState().selectedClipIds.size).toBe(3);
  });

  it("selects all clips", () => {
    const clips = [makeClip(1, 0), makeClip(2, 0), makeClip(3, 0)];
    useUiStore.getState().selectAllClips(clips);
    expect(useUiStore.getState().selectedClipIds.size).toBe(3);
  });

  it("sets clipboard", () => {
    const clips = [makeClip(1, 0)];
    useUiStore.getState().setClipboard(clips);
    expect(useUiStore.getState().clipClipboard).toEqual(clips);
  });

  it("sets and persists the bottom panel height", () => {
    useUiStore.getState().setBottomPanelHeight(320);
    expect(useUiStore.getState().bottomPanelHeight).toBe(320);
    expect(localStorage.getItem("hdaw_bottom_panel_h")).toBe("320");
  });

  it("sets and persists a per-tab bottom panel height", () => {
    useUiStore.getState().setBottomPanelHeightForTab("piano-roll", 340);
    expect(useUiStore.getState().bottomPanelHeights["piano-roll"]).toBe(340);
    expect(localStorage.getItem("hdaw_bottom_panel_h_per_tab")).toBe(
      JSON.stringify({ "piano-roll": 340 })
    );
  });

  it("effectiveBottomPanelHeight prefers per-tab, then tab default, then global", () => {
    useUiStore.setState({ bottomPanelHeight: 200 });
    expect(useUiStore.getState().effectiveBottomPanelHeight("piano-roll")).toBe(300);
    expect(useUiStore.getState().effectiveBottomPanelHeight("mixer")).toBe(200);

    useUiStore.getState().setBottomPanelHeight(260);
    expect(useUiStore.getState().effectiveBottomPanelHeight("mixer")).toBe(260);

    useUiStore.getState().setBottomPanelHeightForTab("piano-roll", 340);
    expect(useUiStore.getState().effectiveBottomPanelHeight("piano-roll")).toBe(340);
  });

  it("ignores an invalid tab id in setBottomPanelHeightForTab", () => {
    useUiStore.getState().setBottomPanelHeightForTab("not-a-real-tab", 400);
    expect(useUiStore.getState().bottomPanelHeights).toEqual({});
    expect(localStorage.getItem("hdaw_bottom_panel_h_per_tab")).toBeNull();
  });

  it("clamps per-tab heights below the minimum", () => {
    useUiStore.getState().setBottomPanelHeightForTab("mixer", 50);
    expect(useUiStore.getState().bottomPanelHeights["mixer"]).toBe(MIN_BOTTOM_PANEL_H);
  });

  it("filters invalid entries when loading persisted per-tab heights", async () => {
    localStorage.setItem(
      "hdaw_bottom_panel_h_per_tab",
      JSON.stringify({ "piano-roll": 320, "bogus-tab": 400, "mixer": -5, "automation": "tall" })
    );
    vi.resetModules();
    const fresh = await import("../store/uiStore");
    expect(fresh.useUiStore.getState().bottomPanelHeights).toEqual({ "piano-roll": 320 });
  });

  it("exposes a sane minimum for the bottom panel", () => {
    expect(MIN_BOTTOM_PANEL_H).toBeGreaterThanOrEqual(80);
  });

  it("setViewMode persists the mode", () => {
    useUiStore.getState().setViewMode("session");
    expect(useUiStore.getState().viewMode).toBe("session");
    expect(localStorage.getItem("hdaw_view_mode")).toBe("session");
  });

  it("restores a valid stored view mode on init", async () => {
    localStorage.setItem("hdaw_view_mode", "session");
    vi.resetModules();
    const fresh = await import("../store/uiStore");
    expect(fresh.useUiStore.getState().viewMode).toBe("session");
  });

  it("falls back to arrange when stored view mode is invalid", async () => {
    localStorage.setItem("hdaw_view_mode", "bogus");
    vi.resetModules();
    const fresh = await import("../store/uiStore");
    expect(fresh.useUiStore.getState().viewMode).toBe("arrange");
  });

  it("selectBottomTab updates the tab and persists it", () => {
    useUiStore.getState().selectBottomTab("automation");
    expect(useUiStore.getState().activeBottomTab).toBe("automation");
    expect(localStorage.getItem("hdaw_last_bottom_tab")).toBe("automation");
  });

  it("restores a valid stored bottom tab on init", async () => {
    localStorage.setItem("hdaw_last_bottom_tab", "automation");
    vi.resetModules();
    const fresh = await import("../store/uiStore");
    expect(fresh.useUiStore.getState().activeBottomTab).toBe("automation");
  });

  it("ignores an unknown tab in selectBottomTab", () => {
    useUiStore.getState().selectBottomTab("not-a-real-tab");
    expect(useUiStore.getState().activeBottomTab).toBe("mixer");
    expect(localStorage.getItem("hdaw_last_bottom_tab")).toBeNull();
  });

  it("falls back to mixer when stored bottom tab is invalid", async () => {
    localStorage.setItem("hdaw_last_bottom_tab", "bogus");
    vi.resetModules();
    const fresh = await import("../store/uiStore");
    expect(fresh.useUiStore.getState().activeBottomTab).toBe("mixer");
  });

  it("setActiveBottomTab does not persist (auto-switch path)", () => {
    useUiStore.getState().setActiveBottomTab("piano-roll");
    expect(useUiStore.getState().activeBottomTab).toBe("piano-roll");
    expect(localStorage.getItem("hdaw_last_bottom_tab")).toBeNull();
  });

  it("setSnapEnabled persists to localStorage", () => {
    useUiStore.getState().setSnapEnabled(false);
    expect(useUiStore.getState().snapEnabled).toBe(false);
    expect(localStorage.getItem("hdaw_snapEnabled")).toBe("false");

    useUiStore.getState().setSnapEnabled(true);
    expect(useUiStore.getState().snapEnabled).toBe(true);
    expect(localStorage.getItem("hdaw_snapEnabled")).toBe("true");
  });

  it("setSnapDivision persists to localStorage", () => {
    useUiStore.getState().setSnapDivision(4);
    expect(useUiStore.getState().snapDivision).toBe(4);
    expect(localStorage.getItem("hdaw_snapDivision")).toBe("4");
  });

  it("setSnapGridOffset persists to localStorage", () => {
    useUiStore.getState().setSnapGridOffset(true);
    expect(useUiStore.getState().snapGridOffset).toBe(true);
    expect(localStorage.getItem("hdaw_snapGridOffset")).toBe("true");
  });

  it("setSnapToEvents persists to localStorage", () => {
    useUiStore.getState().setSnapToEvents(true);
    expect(useUiStore.getState().snapToEvents).toBe(true);
    expect(localStorage.getItem("hdaw_snapToEvents")).toBe("true");
  });

  it("restores snap settings from localStorage on init", async () => {
    localStorage.setItem("hdaw_snapEnabled", "false");
    localStorage.setItem("hdaw_snapDivision", "8");
    localStorage.setItem("hdaw_snapGridOffset", "true");
    localStorage.setItem("hdaw_snapToEvents", "true");
    vi.resetModules();
    const fresh = await import("../store/uiStore");
    expect(fresh.useUiStore.getState().snapEnabled).toBe(false);
    expect(fresh.useUiStore.getState().snapDivision).toBe(8);
    expect(fresh.useUiStore.getState().snapGridOffset).toBe(true);
    expect(fresh.useUiStore.getState().snapToEvents).toBe(true);
  });

  it("uses defaults when localStorage is empty", async () => {
    vi.resetModules();
    const fresh = await import("../store/uiStore");
    expect(fresh.useUiStore.getState().snapEnabled).toBe(true);
    expect(fresh.useUiStore.getState().snapDivision).toBe(1);
    expect(fresh.useUiStore.getState().snapGridOffset).toBe(false);
    expect(fresh.useUiStore.getState().snapToEvents).toBe(false);
  });
});
