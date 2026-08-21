import { render, cleanup } from "@testing-library/react";
import { afterEach, describe, it, expect } from "vitest";
import { useProjectStore } from "../store/projectStore";
import { LoadingOverlay } from "./LoadingOverlay";

afterEach(() => {
  cleanup();
  useProjectStore.setState({
    loadingProject: false,
    loadProgress: null,
  });
});

describe("LoadingOverlay", () => {
  it("renders nothing when not loading", () => {
    const { container } = render(<LoadingOverlay />);
    expect(container.innerHTML).toBe("");
  });

  it("shows spinner when loading", () => {
    useProjectStore.setState({ loadingProject: true, loadProgress: { message: "Loading...", percent: 0 } });
    const { container } = render(<LoadingOverlay />);
    expect(container.querySelector(".loading-overlay__spinner")).not.toBeNull();
  });

  it("shows progress percentage text when loading with percent", () => {
    useProjectStore.setState({
      loadingProject: true,
      loadProgress: { message: "Loading...", percent: 0.5 },
    });
    const { container } = render(<LoadingOverlay />);
    const barFill = container.querySelector(".loading-overlay__bar-fill");
    expect(barFill).not.toBeNull();
    expect(barFill?.getAttribute("style")).toContain("50%");
  });

  it("shows progress bar when percent is between 0 and 1", () => {
    useProjectStore.setState({
      loadingProject: true,
      loadProgress: { message: "Loading...", percent: 0.75 },
    });
    const { container } = render(<LoadingOverlay />);
    expect(container.querySelector(".loading-overlay__bar-fill")).not.toBeNull();
  });

  it("hides progress bar when percent is 0", () => {
    useProjectStore.setState({
      loadingProject: true,
      loadProgress: { message: "Loading...", percent: 0 },
    });
    const { container } = render(<LoadingOverlay />);
    expect(container.querySelector(".loading-overlay__bar-track")).toBeNull();
  });

  it("renders without crashing", () => {
    useProjectStore.setState({
      loadingProject: true,
      loadProgress: { message: "Loading...", percent: 0.3 },
    });
    const { container } = render(<LoadingOverlay />);
    expect(container.querySelector(".loading-overlay")).not.toBeNull();
  });
});
