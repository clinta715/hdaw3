import { render, screen } from "@testing-library/react";
import { describe, it, expect, vi } from "vitest";
import UndoHistory from "./UndoHistory";

vi.mock("../rpc", () => ({
  rpc: {
    call: vi.fn().mockResolvedValue([]),
  },
}));

vi.mock("../store/projectStore", () => ({
  useProjectStore: Object.assign(
    (selector: (s: Record<string, unknown>) => unknown) => selector({ snapshot: { tracks: [] } }),
    { setState: vi.fn() }
  ),
}));

describe("UndoHistory", () => {
  it("renders the History title", () => {
    render(<UndoHistory />);
    expect(screen.getByText("History")).toBeTruthy();
  });

  it("shows undo and redo buttons", () => {
    render(<UndoHistory />);
    expect(screen.getByText("Undo")).toBeTruthy();
    expect(screen.getByText("Redo")).toBeTruthy();
  });

  it("shows empty state when no actions", () => {
    render(<UndoHistory />);
    expect(screen.getByText("No actions yet")).toBeTruthy();
  });
});
