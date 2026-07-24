import { describe, it, expect, beforeEach } from "vitest";
import { useBrowserStore } from "../store/browserStore";

describe("browserStore", () => {
  beforeEach(() => {
    localStorage.clear();
    useBrowserStore.setState({
      folders: [],
      favorites: [],
      expandedPaths: new Set(),
      selectedFile: null,
      searchQuery: "",
      visible: false,
    });
  });

  it("adds a folder", () => {
    useBrowserStore.getState().addFolder("C:\\Music");
    expect(useBrowserStore.getState().folders).toEqual(["C:\\Music"]);
  });

  it("does not add duplicate folders", () => {
    useBrowserStore.getState().addFolder("C:\\Music");
    useBrowserStore.getState().addFolder("C:\\Music");
    expect(useBrowserStore.getState().folders).toEqual(["C:\\Music"]);
  });

  it("removes a folder", () => {
    useBrowserStore.getState().addFolder("C:\\Music");
    useBrowserStore.getState().addFolder("C:\\Samples");
    useBrowserStore.getState().removeFolder("C:\\Music");
    expect(useBrowserStore.getState().folders).toEqual(["C:\\Samples"]);
  });

  it("clears expanded paths when removing a folder", () => {
    useBrowserStore.getState().addFolder("C:\\Music");
    useBrowserStore.getState().toggleExpanded("C:\\Music");
    expect(useBrowserStore.getState().expandedPaths.has("C:\\Music")).toBe(true);
    useBrowserStore.getState().removeFolder("C:\\Music");
    expect(useBrowserStore.getState().expandedPaths.has("C:\\Music")).toBe(false);
  });

  it("adds a favorite", () => {
    useBrowserStore.getState().addFavorite("C:\\Music", "My Music");
    const favs = useBrowserStore.getState().favorites;
    expect(favs).toHaveLength(1);
    expect(favs[0].path).toBe("C:\\Music");
    expect(favs[0].label).toBe("My Music");
  });

  it("derives label from path when not provided", () => {
    useBrowserStore.getState().addFavorite("C:\\Users\\Samples");
    const favs = useBrowserStore.getState().favorites;
    expect(favs[0].label).toBe("Samples");
  });

  it("does not add duplicate favorites", () => {
    useBrowserStore.getState().addFavorite("C:\\Music");
    useBrowserStore.getState().addFavorite("C:\\Music");
    expect(useBrowserStore.getState().favorites).toHaveLength(1);
  });

  it("removes a favorite", () => {
    useBrowserStore.getState().addFavorite("C:\\Music");
    useBrowserStore.getState().addFavorite("C:\\Samples");
    useBrowserStore.getState().removeFavorite("C:\\Music");
    expect(useBrowserStore.getState().favorites).toHaveLength(1);
    expect(useBrowserStore.getState().favorites[0].path).toBe("C:\\Samples");
  });

  it("moves a favorite", () => {
    useBrowserStore.getState().addFavorite("C:\\A", "A");
    useBrowserStore.getState().addFavorite("C:\\B", "B");
    useBrowserStore.getState().addFavorite("C:\\C", "C");
    useBrowserStore.getState().moveFavorite(0, 2);
    const favs = useBrowserStore.getState().favorites;
    expect(favs[0].path).toBe("C:\\B");
    expect(favs[1].path).toBe("C:\\C");
    expect(favs[2].path).toBe("C:\\A");
  });

  it("ignores out-of-range moveFavorite", () => {
    useBrowserStore.getState().addFavorite("C:\\A");
    useBrowserStore.getState().moveFavorite(-1, 0);
    useBrowserStore.getState().moveFavorite(0, 99);
    expect(useBrowserStore.getState().favorites).toHaveLength(1);
  });

  it("toggles expanded paths", () => {
    useBrowserStore.getState().toggleExpanded("/path");
    expect(useBrowserStore.getState().expandedPaths.has("/path")).toBe(true);
    useBrowserStore.getState().toggleExpanded("/path");
    expect(useBrowserStore.getState().expandedPaths.has("/path")).toBe(false);
  });

  it("sets selected file", () => {
    useBrowserStore.getState().setSelectedFile("C:\\file.wav");
    expect(useBrowserStore.getState().selectedFile).toBe("C:\\file.wav");
    useBrowserStore.getState().setSelectedFile(null);
    expect(useBrowserStore.getState().selectedFile).toBeNull();
  });

  it("sets search query", () => {
    useBrowserStore.getState().setSearchQuery("kick");
    expect(useBrowserStore.getState().searchQuery).toBe("kick");
  });

  it("toggles visibility", () => {
    expect(useBrowserStore.getState().visible).toBe(false);
    useBrowserStore.getState().toggleVisible();
    expect(useBrowserStore.getState().visible).toBe(true);
    useBrowserStore.getState().toggleVisible();
    expect(useBrowserStore.getState().visible).toBe(false);
  });

  it("persists folders to localStorage", () => {
    useBrowserStore.getState().addFolder("C:\\Music");
    const stored = JSON.parse(localStorage.getItem("hdaw_browser_folders")!);
    expect(stored).toEqual(["C:\\Music"]);
  });

  it("persists favorites to localStorage", () => {
    useBrowserStore.getState().addFavorite("C:\\Music", "Music");
    const stored = JSON.parse(localStorage.getItem("hdaw_browser_favorites")!);
    expect(stored).toEqual([{ path: "C:\\Music", label: "Music" }]);
  });
});
