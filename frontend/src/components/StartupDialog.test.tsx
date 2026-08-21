import { render, screen, cleanup, fireEvent, waitFor } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import StartupDialog from "./StartupDialog";
import { rpc } from "../rpc";
import { VERSION } from "../version";

vi.mock("../rpc", () => ({ rpc: { call: vi.fn() } }));
vi.mock("../store/projectStore", () => ({
  useProjectStore: {
    getState: () => ({
      setLoadingProject: vi.fn(),
      addRecentProject: vi.fn(),
      syncDirtyFlag: vi.fn(),
    }),
  },
}));

beforeEach(() => {
  window.localStorage.clear();
});

afterEach(() => {
  cleanup();
});

describe("StartupDialog", () => {
  const onClose = vi.fn();

  it("renders without crashing", () => {
    render(<StartupDialog onClose={onClose} />);
    expect(screen.getByText("New Project")).toBeInTheDocument();
  });

  it("shows HDAW text", () => {
    render(<StartupDialog onClose={onClose} />);
    expect(screen.getByText("HDAW")).toBeInTheDocument();
    expect(screen.getByText(`v${VERSION}`)).toBeInTheDocument();
  });

  it("shows New Project button", () => {
    render(<StartupDialog onClose={onClose} />);
    expect(screen.getByRole("button", { name: /New Project/i })).toBeInTheDocument();
  });

  it("shows Open Project button", () => {
    render(<StartupDialog onClose={onClose} />);
    expect(screen.getByRole("button", { name: /Open Project/i })).toBeInTheDocument();
  });

  it("clicking New Project calls project.newProject RPC", async () => {
    vi.mocked(rpc.call).mockResolvedValue(undefined as never);
    render(<StartupDialog onClose={onClose} />);
    fireEvent.click(screen.getByRole("button", { name: /New Project/i }));
    await waitFor(() => {
      expect(rpc.call).toHaveBeenCalledWith("project.newProject");
    });
    expect(onClose).toHaveBeenCalled();
  });

  it("shows recent projects from localStorage", () => {
    const projects = [
      "C:\\Users\\test\\Music\\beat1.hdaw",
      "D:\\Projects\\song2.hdaw3",
    ];
    window.localStorage.setItem("hdaw_recent_projects", JSON.stringify(projects));
    render(<StartupDialog onClose={onClose} />);
    expect(screen.getByText("Recent Projects")).toBeInTheDocument();
    expect(screen.getByText("beat1.hdaw")).toBeInTheDocument();
    expect(screen.getByText("song2.hdaw3")).toBeInTheDocument();
  });

  it("renders without crashing with no recent projects", () => {
    render(<StartupDialog onClose={onClose} />);
    expect(screen.getByText("New Project")).toBeInTheDocument();
    expect(screen.queryByText("Recent Projects")).not.toBeInTheDocument();
  });

  it("shows up to 8 recent projects", () => {
    const projects = Array.from({ length: 12 }, (_, i) => `C:\\Projects\\track${i}.hdaw`);
    window.localStorage.setItem("hdaw_recent_projects", JSON.stringify(projects));
    render(<StartupDialog onClose={onClose} />);
    const buttons = screen.getAllByText(/track\d+\.hdaw/);
    expect(buttons).toHaveLength(8);
    expect(screen.queryByText("track8.hdaw")).not.toBeInTheDocument();
  });
});
