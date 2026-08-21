import React from "react";
import { render, screen, fireEvent, cleanup, act } from "@testing-library/react";
import { describe, it, expect, vi, afterEach } from "vitest";
import { ArrangerChainEditor } from "./ArrangerChainEditor";
import { useArrangerStore } from "../store/arrangerStore";
import { rpc } from "../rpc";

vi.mock("../rpc", () => ({ rpc: { call: vi.fn() } }));

afterEach(() => {
  cleanup();
  useArrangerStore.setState({ regions: [], chains: [] });
});

const makeRegion = (overrides: Partial<{ regionID: string; name: string; startTime: number; duration: number; color: number }> = {}) => ({
  regionID: "r1",
  name: "Region A",
  startTime: 0,
  duration: 4,
  color: 0xff0000,
  ...overrides,
});

const makeChain = (overrides: Partial<{ chainID: string; name: string; isActive: boolean; entries: Array<{ regionID: string; repeatCount: number }> }> = {}) => ({
  chainID: "c1",
  name: "Arrangement A",
  isActive: true,
  entries: [],
  ...overrides,
});

describe("ArrangerChainEditor", () => {
  it("renders without crashing", () => {
    render(<ArrangerChainEditor />);
    expect(screen.getByText("Chain:")).toBeInTheDocument();
  });

  it("shows a chain selector or chain list area", () => {
    render(<ArrangerChainEditor />);
    expect(screen.getByText("Chain:")).toBeInTheDocument();
    expect(screen.getByRole("combobox")).toBeInTheDocument();
  });

  it("shows a create chain button", () => {
    render(<ArrangerChainEditor />);
    expect(screen.getByText("+ New")).toBeInTheDocument();
  });

  it("clicking create chain calls addArrangerChain RPC", async () => {
    vi.mocked(rpc.call).mockResolvedValue(undefined);
    render(<ArrangerChainEditor />);
    await act(async () => {
      fireEvent.click(screen.getByText("+ New"));
    });
    expect(rpc.call).toHaveBeenCalledWith("project.addArrangerChain", { name: "Arrangement A" });
  });

  it("shows available regions column", () => {
    render(<ArrangerChainEditor />);
    expect(screen.getByText(/Available Regions/)).toBeInTheDocument();
  });

  it("shows flatten button", () => {
    render(<ArrangerChainEditor />);
    expect(screen.getByText("Flatten")).toBeInTheDocument();
  });

  it("flatten button calls flattenArrangerChain RPC", async () => {
    vi.mocked(rpc.call).mockResolvedValue(undefined);
    useArrangerStore.setState({
      chains: [makeChain({ entries: [{ regionID: "r1", repeatCount: 1 }] })],
    });
    render(<ArrangerChainEditor />);
    await act(async () => {
      fireEvent.click(screen.getByText("Flatten"));
    });
    expect(rpc.call).toHaveBeenCalledWith("project.flattenArranger", {});
  });

  it("renders without crashing with existing chains", () => {
    useArrangerStore.setState({
      chains: [makeChain({ chainID: "c1", name: "My Chain" })],
    });
    render(<ArrangerChainEditor />);
    expect(screen.getByText("Chain:")).toBeInTheDocument();
    expect(screen.getByRole("option", { name: /My Chain/ })).toBeInTheDocument();
  });

  it("renders with regions in the available list", () => {
    useArrangerStore.setState({
      regions: [makeRegion({ regionID: "r1", name: "Intro" })],
    });
    render(<ArrangerChainEditor />);
    expect(screen.getByText("Intro")).toBeInTheDocument();
  });

  it("chain entries list shows empty state when no entries", () => {
    render(<ArrangerChainEditor />);
    expect(screen.getByText("Double-click a region on the right to add it")).toBeInTheDocument();
  });
});
