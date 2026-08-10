import { describe, it, expect, beforeEach, vi } from "vitest";
import type { RpcClient } from "../rpc/client";
import { useLibraryStore, type LibraryInfo } from "../store/libraryStore";
import { useNotifyStore } from "../store/notifyStore";

// Mock RpcClient. `callImpl` lets each test override the call behavior.
function mockRpc(callImpl?: (method: string, params?: unknown) => Promise<unknown>): RpcClient {
  return {
    call: vi.fn(callImpl ?? (async () => undefined)),
  } as unknown as RpcClient;
}

describe("libraryStore", () => {
  beforeEach(() => {
    // Reset store state between tests
    useLibraryStore.setState({
      libraries: [],
      searchResults: [],
      searchQuery: "",
      scanProgress: {},
      loading: false,
    });
    useNotifyStore.getState().clear();
  });

  it("starts empty", () => {
    const s = useLibraryStore.getState();
    expect(s.libraries).toEqual([]);
    expect(s.searchResults).toEqual([]);
    expect(s.scanProgress).toEqual({});
    expect(s.loading).toBe(false);
  });

  it("loadLibraries fetches and stores the list", async () => {
    const fake: LibraryInfo[] = [
      { id: "lib1", name: "My MIDI", path: "/midi", type: "midi", lastScan: "2026-01-01", fileCount: 5, autoScan: true },
    ];
    const rpc = mockRpc(async (m) => {
      if (m === "library.list") return fake;
      return undefined;
    });
    await useLibraryStore.getState().loadLibraries(rpc);
    expect(useLibraryStore.getState().libraries).toEqual(fake);
    expect(useLibraryStore.getState().loading).toBe(false);
  });

  it("loadLibraries sets loading true then false", async () => {
    const rpc = mockRpc(async () => new Promise((r) => setTimeout(() => r([]), 10)));
    const p = useLibraryStore.getState().loadLibraries(rpc);
    expect(useLibraryStore.getState().loading).toBe(true);
    await p;
    expect(useLibraryStore.getState().loading).toBe(false);
  });

  it("loadLibraries handles non-array result as empty", async () => {
    const rpc = mockRpc(async () => null);
    await useLibraryStore.getState().loadLibraries(rpc);
    expect(useLibraryStore.getState().libraries).toEqual([]);
  });

  it("loadLibraries reports RPC errors", async () => {
    const rpc = mockRpc(async () => {
      throw new Error("boom");
    });
    await useLibraryStore.getState().loadLibraries(rpc);
    expect(useLibraryStore.getState().loading).toBe(false);
    expect(useNotifyStore.getState().toasts.length).toBeGreaterThan(0);
  });

  it("addLibrary calls library.add then reloads", async () => {
    const call = vi.fn(async (m: string) => {
      if (m === "library.add") return { id: "abc" };
      if (m === "library.list")
        return [{ id: "abc", name: "N", path: "P", type: "midi", lastScan: "", fileCount: 0, autoScan: false }];
      return undefined;
    });
    const rpc = { call } as unknown as RpcClient;
    const ok = await useLibraryStore.getState().addLibrary("N", "P", "midi", rpc);
    expect(ok).toBe(true);
    expect(call).toHaveBeenCalledWith("library.add", { name: "N", path: "P", type: "midi" });
    expect(useLibraryStore.getState().libraries).toHaveLength(1);
  });

  it("addLibrary returns false on error and reports", async () => {
    const rpc = mockRpc(async () => {
      throw new Error("nope");
    });
    const ok = await useLibraryStore.getState().addLibrary("N", "P", "midi", rpc);
    expect(ok).toBe(false);
    expect(useNotifyStore.getState().toasts.length).toBeGreaterThan(0);
  });

  it("removeLibrary calls library.remove then reloads", async () => {
    const call = vi.fn(async (m: string) => (m === "library.list" ? [] : undefined));
    const rpc = { call } as unknown as RpcClient;
    await useLibraryStore.getState().removeLibrary("xyz", rpc);
    expect(call).toHaveBeenCalledWith("library.remove", { id: "xyz" });
  });

  it("scanLibrary calls library.scan with id", async () => {
    const call = vi.fn(async () => undefined);
    const rpc = { call } as unknown as RpcClient;
    await useLibraryStore.getState().scanLibrary("id1", rpc);
    expect(call).toHaveBeenCalledWith("library.scan", { id: "id1" });
  });

  it("scanAll calls library.scan with empty params", async () => {
    const call = vi.fn(async () => undefined);
    const rpc = { call } as unknown as RpcClient;
    await useLibraryStore.getState().scanAll(rpc);
    expect(call).toHaveBeenCalledWith("library.scan", {});
  });

  it("search stores results array", async () => {
    const fake = [{ name: "a.mid", path: "/a", size: 100, durationSeconds: 1, key: "C" }];
    const call = vi.fn(async (m: string) => (m === "library.search" ? fake : undefined));
    const rpc = { call } as unknown as RpcClient;
    await useLibraryStore.getState().search("a", {}, rpc);
    expect(call).toHaveBeenCalledWith("library.search", expect.objectContaining({ query: "a" }));
    expect(useLibraryStore.getState().searchResults).toEqual(fake);
  });

  it("search passes filters through", async () => {
    const call = vi.fn(async () => []);
    const rpc = { call } as unknown as RpcClient;
    await useLibraryStore.getState().search("", { type: "midi", bpmMin: 120, limit: 10 }, rpc);
    expect(call).toHaveBeenCalledWith("library.search", { query: "", type: "midi", bpmMin: 120, limit: 10 });
  });

  it("search handles non-array result as empty", async () => {
    const rpc = mockRpc(async () => null);
    await useLibraryStore.getState().search("", {}, rpc);
    expect(useLibraryStore.getState().searchResults).toEqual([]);
  });

  it("setSearchQuery sets the query", () => {
    useLibraryStore.getState().setSearchQuery("hello");
    expect(useLibraryStore.getState().searchQuery).toBe("hello");
  });

  it("setAutoScan calls library.setAutoScan then reloads", async () => {
    const call = vi.fn(async (m: string) => (m === "library.list" ? [] : undefined));
    const rpc = { call } as unknown as RpcClient;
    await useLibraryStore.getState().setAutoScan("id2", true, rpc);
    expect(call).toHaveBeenCalledWith("library.setAutoScan", { id: "id2", enabled: true });
  });

  it("updateScanProgress sets per-library progress", () => {
    useLibraryStore
      .getState()
      .updateScanProgress({ libraryId: "L1", scanned: 5, total: 10, phase: "scanning" });
    expect(useLibraryStore.getState().scanProgress["L1"]).toEqual({
      libraryId: "L1",
      scanned: 5,
      total: 10,
      phase: "scanning",
    });
  });

  it("clearScanProgress removes a library's progress", () => {
    useLibraryStore
      .getState()
      .updateScanProgress({ libraryId: "L1", scanned: 5, total: 10, phase: "scanning" });
    useLibraryStore.getState().clearScanProgress("L1");
    expect(useLibraryStore.getState().scanProgress["L1"]).toBeUndefined();
  });
});
