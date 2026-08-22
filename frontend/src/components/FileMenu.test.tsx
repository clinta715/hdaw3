import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen, fireEvent, cleanup } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import FileMenu from "./FileMenu";
import { rpc } from "../rpc";
import { useProjectStore } from "../store/projectStore";

vi.mock("../rpc", () => ({
  rpc: { call: vi.fn().mockResolvedValue({}) },
}));

vi.mock("../store/notifyStore", () => ({
  reportRpcError: vi.fn(),
  useNotifyStore: { getState: () => ({ push: vi.fn() }) },
}));

vi.mock("./ImportDialog", () => ({
  default: ({ mode, onClose }: { mode: string; onClose: () => void }) => (
    <div data-testid="import-dialog">
      Import Dialog ({mode})
      <button onClick={onClose}>Close Import</button>
    </div>
  ),
}));

vi.mock("./ExportDialog", () => ({
  default: ({ onClose }: { onClose: () => void }) => (
    <div data-testid="export-dialog">
      Export Dialog
      <button onClick={onClose}>Close Export</button>
    </div>
  ),
}));

const mockedCall = rpc.call as unknown as ReturnType<typeof vi.fn>;

function openMenu() {
  fireEvent.click(screen.getByRole("button", { name: "File" }));
}

describe("FileMenu", () => {
  beforeEach(() => {
    window.localStorage.clear();
    mockedCall.mockReset();
    mockedCall.mockResolvedValue({});
    useProjectStore.setState({
      filePath: null,
      isDirty: false,
      recentProjects: [],
      loadingProject: false,
    });
  });

  afterEach(() => {
    cleanup();
  });

  describe("menu toggle", () => {
    it("opens the dropdown when the File button is clicked", () => {
      render(<FileMenu />);
      expect(screen.queryByText("New Project")).not.toBeInTheDocument();
      openMenu();
      expect(screen.getByText("New Project")).toBeInTheDocument();
    });

    it("closes the dropdown when the File button is clicked again", () => {
      render(<FileMenu />);
      openMenu();
      expect(screen.getByText("New Project")).toBeInTheDocument();
      openMenu();
      expect(screen.queryByText("New Project")).not.toBeInTheDocument();
    });
  });

  describe("menu items", () => {
    it("shows New Project item with shortcut", () => {
      render(<FileMenu />);
      openMenu();
      expect(screen.getByText("New Project")).toBeInTheDocument();
      expect(screen.getByText("Ctrl+N")).toBeInTheDocument();
    });

    it("shows Open item with shortcut", () => {
      render(<FileMenu />);
      openMenu();
      expect(screen.getByText("Open...")).toBeInTheDocument();
      expect(screen.getByText("Ctrl+O")).toBeInTheDocument();
    });

    it("shows Open Recent submenu item", () => {
      render(<FileMenu />);
      openMenu();
      expect(screen.getByText("Open Recent")).toBeInTheDocument();
    });

    it("shows Save item with shortcut", () => {
      render(<FileMenu />);
      openMenu();
      expect(screen.getByText("Save")).toBeInTheDocument();
      expect(screen.getByText("Ctrl+S")).toBeInTheDocument();
    });

    it("shows Save As item with shortcut", () => {
      render(<FileMenu />);
      openMenu();
      expect(screen.getByText("Save As...")).toBeInTheDocument();
      expect(screen.getByText("Ctrl+Shift+S")).toBeInTheDocument();
    });

    it("shows Import Audio item with shortcut", () => {
      render(<FileMenu />);
      openMenu();
      expect(screen.getByText("Import Audio...")).toBeInTheDocument();
      expect(screen.getByText("Ctrl+Shift+I")).toBeInTheDocument();
    });

    it("shows Import MIDI item with shortcut", () => {
      render(<FileMenu />);
      openMenu();
      expect(screen.getByText("Import MIDI...")).toBeInTheDocument();
      expect(screen.getByText("Ctrl+Shift+M")).toBeInTheDocument();
    });

    it("shows Export Audio item with shortcut", () => {
      render(<FileMenu />);
      openMenu();
      expect(screen.getByText("Export Audio...")).toBeInTheDocument();
      expect(screen.getByText("Ctrl+E")).toBeInTheDocument();
    });

    it("shows Relink All Missing Files item", () => {
      render(<FileMenu />);
      openMenu();
      expect(screen.getByText("Relink All Missing Files...")).toBeInTheDocument();
    });
  });

  describe("clicking menu items", () => {
    it("clicking New calls project.newProject", async () => {
      render(<FileMenu />);
      openMenu();
      fireEvent.click(screen.getByText("New Project"));
      expect(mockedCall).toHaveBeenCalledWith("project.newProject");
    });

    it("clicking Save with filePath calls project.saveProject", async () => {
      useProjectStore.setState({ filePath: "/test/project.hdaw" });
      render(<FileMenu />);
      openMenu();
      fireEvent.click(screen.getByText("Save"));
      expect(mockedCall).toHaveBeenCalledWith("project.saveProject", {
        filePath: "/test/project.hdaw",
      });
    });

    it("clicking Export Audio opens export dialog", () => {
      render(<FileMenu />);
      openMenu();
      fireEvent.click(screen.getByText("Export Audio..."));
      expect(screen.getByTestId("export-dialog")).toBeInTheDocument();
    });

    it("clicking Import Audio opens import dialog in audio mode", () => {
      render(<FileMenu />);
      openMenu();
      fireEvent.click(screen.getByText("Import Audio..."));
      expect(screen.getByTestId("import-dialog")).toBeInTheDocument();
      expect(screen.getByText(/Import Dialog.*audio/)).toBeInTheDocument();
    });

    it("clicking Import MIDI opens import dialog in midi mode", () => {
      render(<FileMenu />);
      openMenu();
      fireEvent.click(screen.getByText("Import MIDI..."));
      expect(screen.getByTestId("import-dialog")).toBeInTheDocument();
      expect(screen.getByText(/Import Dialog.*midi/)).toBeInTheDocument();
    });

    it("menu closes after clicking an item", () => {
      render(<FileMenu />);
      openMenu();
      fireEvent.click(screen.getByText("New Project"));
      expect(screen.queryByText("New Project")).not.toBeInTheDocument();
    });
  });

  describe("keyboard shortcuts", () => {
    it("Ctrl+N calls project.newProject", async () => {
      const user = userEvent.setup();
      render(<FileMenu />);
      await user.keyboard("{Control>}n{/Control}");
      expect(mockedCall).toHaveBeenCalledWith("project.newProject");
    });

    it("Ctrl+O triggers open handler (prompt in browser mode)", async () => {
      const user = userEvent.setup();
      const promptSpy = vi.spyOn(window, "prompt").mockReturnValue("/test/project.hdaw");
      render(<FileMenu />);
      await user.keyboard("{Control>}o{/Control}");
      expect(mockedCall).toHaveBeenCalledWith("project.loadProject", {
        filePath: "/test/project.hdaw",
      });
      promptSpy.mockRestore();
    });

    it("Ctrl+S calls project.saveProject when filePath is set", async () => {
      const user = userEvent.setup();
      useProjectStore.setState({ filePath: "/test/project.hdaw" });
      render(<FileMenu />);
      await user.keyboard("{Control>}s{/Control}");
      expect(mockedCall).toHaveBeenCalledWith("project.saveProject", {
        filePath: "/test/project.hdaw",
      });
    });

    it("Ctrl+S prompts for save path when no filePath", async () => {
      const user = userEvent.setup();
      const promptSpy = vi.spyOn(window, "prompt").mockReturnValue("/new/path.hdaw");
      render(<FileMenu />);
      await user.keyboard("{Control>}s{/Control}");
      expect(mockedCall).toHaveBeenCalledWith("project.saveProject", {
        filePath: "/new/path.hdaw",
      });
      promptSpy.mockRestore();
    });

    it("Ctrl+Shift+S triggers Save As", async () => {
      const user = userEvent.setup();
      const promptSpy = vi.spyOn(window, "prompt").mockReturnValue("/saveas/project.hdaw");
      render(<FileMenu />);
      await user.keyboard("{Control>}{Shift>}s{/Shift}{/Control}");
      expect(mockedCall).toHaveBeenCalledWith("project.saveProject", {
        filePath: "/saveas/project.hdaw",
      });
      promptSpy.mockRestore();
    });

    it("Ctrl+E opens export dialog", async () => {
      const user = userEvent.setup();
      render(<FileMenu />);
      await user.keyboard("{Control>}e{/Control}");
      expect(screen.getByTestId("export-dialog")).toBeInTheDocument();
    });

    it("Ctrl+Shift+I opens import audio dialog", async () => {
      const user = userEvent.setup();
      render(<FileMenu />);
      await user.keyboard("{Control>}{Shift>}i{/Shift}{/Control}");
      expect(screen.getByTestId("import-dialog")).toBeInTheDocument();
      expect(screen.getByText(/Import Dialog.*audio/)).toBeInTheDocument();
    });

    it("Ctrl+Shift+M opens import MIDI dialog", async () => {
      const user = userEvent.setup();
      render(<FileMenu />);
      await user.keyboard("{Control>}{Shift>}m{/Shift}{/Control}");
      expect(screen.getByTestId("import-dialog")).toBeInTheDocument();
      expect(screen.getByText(/Import Dialog.*midi/)).toBeInTheDocument();
    });

    it("shortcuts are ignored when typing in an input", async () => {
      const user = userEvent.setup();
      render(
        <div>
          <FileMenu />
          <input data-testid="test-input" />
        </div>
      );
      const input = screen.getByTestId("test-input");
      input.focus();
      await user.keyboard("{Control>}n{/Control}");
      expect(mockedCall).not.toHaveBeenCalledWith("project.newProject");
    });
  });

  describe("recent projects", () => {
    it("shows No recent projects when list is empty", () => {
      useProjectStore.setState({ recentProjects: [] });
      render(<FileMenu />);
      openMenu();
      fireEvent.mouseEnter(screen.getByText("Open Recent").closest(".fm-submenu-item")!);
      expect(screen.getByText("No recent projects")).toBeInTheDocument();
    });

    it("shows recent project paths when available", () => {
      useProjectStore.setState({
        recentProjects: ["/projects/song1.hdaw", "/projects/song2.hdaw"],
      });
      render(<FileMenu />);
      openMenu();
      fireEvent.mouseEnter(screen.getByText("Open Recent").closest(".fm-submenu-item")!);
      expect(screen.getByText("song1.hdaw")).toBeInTheDocument();
      expect(screen.getByText("song2.hdaw")).toBeInTheDocument();
    });

    it("clicking a recent project loads it", () => {
      useProjectStore.setState({
        recentProjects: ["/projects/song1.hdaw"],
      });
      render(<FileMenu />);
      openMenu();
      fireEvent.mouseEnter(screen.getByText("Open Recent").closest(".fm-submenu-item")!);
      fireEvent.click(screen.getByText("song1.hdaw"));
      expect(mockedCall).toHaveBeenCalledWith("project.loadProject", {
        filePath: "/projects/song1.hdaw",
      });
    });

    it("shows Clear Recent button when projects exist", () => {
      useProjectStore.setState({
        recentProjects: ["/projects/song1.hdaw"],
      });
      render(<FileMenu />);
      openMenu();
      fireEvent.mouseEnter(screen.getByText("Open Recent").closest(".fm-submenu-item")!);
      expect(screen.getByText("Clear Recent")).toBeInTheDocument();
    });
  });

  describe("click outside", () => {
    it("clicking outside the menu closes it", () => {
      render(<FileMenu />);
      openMenu();
      expect(screen.getByText("New Project")).toBeInTheDocument();
      fireEvent.mouseDown(document.body);
      expect(screen.queryByText("New Project")).not.toBeInTheDocument();
    });
  });
});
