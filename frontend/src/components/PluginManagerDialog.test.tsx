import { describe, it, expect, beforeEach, vi, afterEach } from "vitest";
import { render, screen, fireEvent, cleanup, waitFor } from "@testing-library/react";
import PluginManagerDialog from "./PluginManagerDialog";
import { rpc } from "../rpc";

vi.mock("../rpc", () => ({
  rpc: {
    call: vi.fn(),
    onNotification: vi.fn(() => vi.fn()),
  },
}));

const mockedCall = rpc.call as unknown as ReturnType<typeof vi.fn>;
const mockedOnNotification = rpc.onNotification as unknown as ReturnType<typeof vi.fn>;

const INSTRUMENTS = [
  { name: "Synth Pad", format: "VST3", manufacturer: "TestCo", fileOrIdentifier: "synth.vst3", isInstrument: true },
  { name: "Drum Machine", format: "CLAP", manufacturer: "OtherCo", fileOrIdentifier: "drums.clap", isInstrument: true },
];

const EFFECTS = [
  { name: "EQ Eight", format: "VST3", manufacturer: "TestCo", fileOrIdentifier: "eq.vst3", isInstrument: false },
  { name: "Reverb", format: "CLAP", manufacturer: "OtherCo", fileOrIdentifier: "reverb.clap", isInstrument: false },
];

const ALL_PLUGINS = [...INSTRUMENTS, ...EFFECTS];

function setupRpcDefaults() {
  mockedCall.mockImplementation(async (method: string, _params?: unknown) => {
    switch (method) {
      case "plugin.getPlugins":
        return ALL_PLUGINS;
      case "plugin.isBlacklisted":
        return false;
      case "plugin.getBlacklistReason":
        return "";
      case "plugin.getCustomScanDirs":
        return [];
      default:
        return null;
    }
  });
}

function setupWithBlacklisted(blacklistedIds: string[]) {
  mockedCall.mockImplementation(async (method: string, params?: Record<string, unknown>) => {
    switch (method) {
      case "plugin.getPlugins":
        return ALL_PLUGINS;
      case "plugin.isBlacklisted":
        return blacklistedIds.includes(params?.pluginID as string);
      case "plugin.getBlacklistReason":
        return params?.pluginID === "eq.vst3" ? "crash" : "";
      case "plugin.getCustomScanDirs":
        return [];
      default:
        return null;
    }
  });
}

function setupWithCustomPaths(paths: string[]) {
  mockedCall.mockImplementation(async (method: string) => {
    switch (method) {
      case "plugin.getPlugins":
        return ALL_PLUGINS;
      case "plugin.isBlacklisted":
        return false;
      case "plugin.getBlacklistReason":
        return "";
      case "plugin.getCustomScanDirs":
        return paths;
      default:
        return null;
    }
  });
}

describe("PluginManagerDialog", () => {
  const onClose = vi.fn();

  beforeEach(() => {
    vi.clearAllMocks();
    mockedOnNotification.mockReturnValue(vi.fn());
    setupRpcDefaults();
  });

  afterEach(() => {
    cleanup();
    vi.restoreAllMocks();
  });

  describe("dialog rendering", () => {
    it("shows 'Plugin Manager' title", () => {
      render(<PluginManagerDialog onClose={onClose} />);
      expect(screen.getByText("Plugin Manager")).toBeInTheDocument();
    });

    it("shows plugin count after load", async () => {
      render(<PluginManagerDialog onClose={onClose} />);
      await waitFor(() => {
        expect(screen.getByText("4 plugins")).toBeInTheDocument();
      });
    });

    it("shows Rescan button", () => {
      render(<PluginManagerDialog onClose={onClose} />);
      expect(screen.getByRole("button", { name: "Rescan" })).toBeInTheDocument();
    });

    it("shows close button", () => {
      render(<PluginManagerDialog onClose={onClose} />);
      expect(screen.getByText("\u00d7")).toBeInTheDocument();
    });

    it("shows filter tabs", () => {
      render(<PluginManagerDialog onClose={onClose} />);
      expect(screen.getByRole("button", { name: /All/ })).toBeInTheDocument();
      expect(screen.getByRole("button", { name: /Instruments/ })).toBeInTheDocument();
      expect(screen.getByRole("button", { name: /Effects/ })).toBeInTheDocument();
      expect(screen.getByRole("button", { name: /Blacklisted/ })).toBeInTheDocument();
    });

    it("shows search input", () => {
      render(<PluginManagerDialog onClose={onClose} />);
      expect(screen.getByPlaceholderText("Filter by name, manufacturer, format...")).toBeInTheDocument();
    });

    it("shows scan paths section", () => {
      render(<PluginManagerDialog onClose={onClose} />);
      expect(screen.getByText("Scan Paths")).toBeInTheDocument();
    });
  });

  describe("plugin list", () => {
    it("renders all plugin entries", async () => {
      render(<PluginManagerDialog onClose={onClose} />);
      await waitFor(() => {
        expect(screen.getByText("Synth Pad")).toBeInTheDocument();
        expect(screen.getByText("Drum Machine")).toBeInTheDocument();
        expect(screen.getByText("EQ Eight")).toBeInTheDocument();
        expect(screen.getByText("Reverb")).toBeInTheDocument();
      });
    });

    it("shows plugin manufacturer and format", async () => {
      render(<PluginManagerDialog onClose={onClose} />);
      await waitFor(() => {
        expect(screen.getAllByText(/TestCo/).length).toBeGreaterThanOrEqual(1);
        expect(screen.getAllByText(/OtherCo/).length).toBeGreaterThanOrEqual(1);
      });
    });

    it("shows Instrument tag for instruments", async () => {
      render(<PluginManagerDialog onClose={onClose} />);
      await waitFor(() => {
        const tags = screen.getAllByText("Instrument");
        expect(tags.length).toBe(2);
      });
    });

    it("shows Blacklist button for each plugin", async () => {
      render(<PluginManagerDialog onClose={onClose} />);
      await waitFor(() => {
        const blButtons = screen.getAllByRole("button", { name: "Blacklist" });
        expect(blButtons.length).toBe(4);
      });
    });

    it("fetches plugins on mount", async () => {
      render(<PluginManagerDialog onClose={onClose} />);
      await waitFor(() => {
        expect(mockedCall).toHaveBeenCalledWith("plugin.getPlugins");
      });
    });

    it("fetches blacklist status for each plugin", async () => {
      render(<PluginManagerDialog onClose={onClose} />);
      await waitFor(() => {
        expect(mockedCall).toHaveBeenCalledWith("plugin.isBlacklisted", { pluginID: "synth.vst3" });
        expect(mockedCall).toHaveBeenCalledWith("plugin.isBlacklisted", { pluginID: "drums.clap" });
        expect(mockedCall).toHaveBeenCalledWith("plugin.isBlacklisted", { pluginID: "eq.vst3" });
        expect(mockedCall).toHaveBeenCalledWith("plugin.isBlacklisted", { pluginID: "reverb.clap" });
      });
    });
  });

  describe("blacklisting", () => {
    it("shows Unblacklist button for blacklisted plugins", async () => {
      setupWithBlacklisted(["eq.vst3"]);
      render(<PluginManagerDialog onClose={onClose} />);
      await waitFor(() => {
        expect(screen.getByText("Unblacklist")).toBeInTheDocument();
      });
    });

    it("shows crash reason for blacklisted plugin", async () => {
      setupWithBlacklisted(["eq.vst3"]);
      render(<PluginManagerDialog onClose={onClose} />);
      await waitFor(() => {
        expect(screen.getByText(/crashed during scan/)).toBeInTheDocument();
      });
    });

    it("clicking Blacklist calls RPC and refreshes", async () => {
      setupWithBlacklisted([]);
      render(<PluginManagerDialog onClose={onClose} />);
      await waitFor(() => {
        expect(screen.getByText("Synth Pad")).toBeInTheDocument();
      });

      const blButtons = screen.getAllByRole("button", { name: "Blacklist" });
      fireEvent.click(blButtons[0]);

      await waitFor(() => {
        expect(mockedCall).toHaveBeenCalledWith("plugin.blacklistPlugin", { pluginID: "synth.vst3" });
      });
    });

    it("clicking Unblacklist calls RPC and refreshes", async () => {
      setupWithBlacklisted(["synth.vst3"]);
      render(<PluginManagerDialog onClose={onClose} />);
      await waitFor(() => {
        expect(screen.getByText("Unblacklist")).toBeInTheDocument();
      });

      fireEvent.click(screen.getByText("Unblacklist"));

      await waitFor(() => {
        expect(mockedCall).toHaveBeenCalledWith("plugin.unblacklistPlugin", { pluginID: "synth.vst3" });
      });
    });
  });

  describe("scanning", () => {
    it("clicking Rescan calls plugin.scanAll", async () => {
      render(<PluginManagerDialog onClose={onClose} />);
      await waitFor(() => {
        expect(screen.getByText("Synth Pad")).toBeInTheDocument();
      });

      fireEvent.click(screen.getByRole("button", { name: "Rescan" }));

      await waitFor(() => {
        expect(mockedCall).toHaveBeenCalledWith("plugin.scanAll");
      });
    });

    it("shows Scanning... text while loading", async () => {
      let resolveScan!: () => void;
      mockedCall.mockImplementation(async (method: string) => {
        switch (method) {
          case "plugin.getPlugins":
            return ALL_PLUGINS;
          case "plugin.isBlacklisted":
            return false;
          case "plugin.getBlacklistReason":
            return "";
          case "plugin.getCustomScanDirs":
            return [];
          case "plugin.scanAll":
            return new Promise<void>((resolve) => { resolveScan = resolve; });
          default:
            return null;
        }
      });

      render(<PluginManagerDialog onClose={onClose} />);
      await waitFor(() => {
        expect(screen.getByText("Synth Pad")).toBeInTheDocument();
      });

      fireEvent.click(screen.getByRole("button", { name: "Rescan" }));

      await waitFor(() => {
        expect(screen.getByText("Scanning...")).toBeInTheDocument();
      });

      resolveScan();
    });

    it("disables Rescan button during scan", async () => {
      mockedCall.mockImplementation(async (method: string) => {
        switch (method) {
          case "plugin.getPlugins":
            return ALL_PLUGINS;
          case "plugin.isBlacklisted":
            return false;
          case "plugin.getBlacklistReason":
            return "";
          case "plugin.getCustomScanDirs":
            return [];
          case "plugin.scanAll":
            return new Promise<void>(() => {});
          default:
            return null;
        }
      });

      render(<PluginManagerDialog onClose={onClose} />);
      await waitFor(() => {
        expect(screen.getByText("Synth Pad")).toBeInTheDocument();
      });

      fireEvent.click(screen.getByRole("button", { name: "Rescan" }));

      await waitFor(() => {
        const btn = screen.getByRole("button", { name: "Scanning..." });
        expect(btn).toBeDisabled();
      });
    });
  });

  describe("search and filter", () => {
    it("filters plugins by name", async () => {
      render(<PluginManagerDialog onClose={onClose} />);
      await waitFor(() => {
        expect(screen.getByText("Synth Pad")).toBeInTheDocument();
      });

      fireEvent.change(screen.getByPlaceholderText("Filter by name, manufacturer, format..."), {
        target: { value: "Synth" },
      });

      expect(screen.getByText("Synth Pad")).toBeInTheDocument();
      expect(screen.queryByText("Reverb")).not.toBeInTheDocument();
    });

    it("filters plugins by manufacturer", async () => {
      render(<PluginManagerDialog onClose={onClose} />);
      await waitFor(() => {
        expect(screen.getByText("Synth Pad")).toBeInTheDocument();
      });

      fireEvent.change(screen.getByPlaceholderText("Filter by name, manufacturer, format..."), {
        target: { value: "TestCo" },
      });

      expect(screen.getByText("Synth Pad")).toBeInTheDocument();
      expect(screen.getByText("EQ Eight")).toBeInTheDocument();
      expect(screen.queryByText("Reverb")).not.toBeInTheDocument();
    });

    it("filters plugins by format", async () => {
      render(<PluginManagerDialog onClose={onClose} />);
      await waitFor(() => {
        expect(screen.getByText("Synth Pad")).toBeInTheDocument();
      });

      fireEvent.change(screen.getByPlaceholderText("Filter by name, manufacturer, format..."), {
        target: { value: "CLAP" },
      });

      expect(screen.getByText("Drum Machine")).toBeInTheDocument();
      expect(screen.getByText("Reverb")).toBeInTheDocument();
      expect(screen.queryByText("Synth Pad")).not.toBeInTheDocument();
    });

    it("shows 'No plugins match filter' when nothing matches", async () => {
      render(<PluginManagerDialog onClose={onClose} />);
      await waitFor(() => {
        expect(screen.getByText("Synth Pad")).toBeInTheDocument();
      });

      fireEvent.change(screen.getByPlaceholderText("Filter by name, manufacturer, format..."), {
        target: { value: "zzzznotfound" },
      });

      expect(screen.getByText("No plugins match filter")).toBeInTheDocument();
    });

    it("shows 'No plugins found' when list is empty", async () => {
      mockedCall.mockImplementation(async (method: string) => {
        if (method === "plugin.getPlugins") return [];
        if (method === "plugin.getCustomScanDirs") return [];
        return null;
      });

      render(<PluginManagerDialog onClose={onClose} />);
      await waitFor(() => {
        expect(screen.getByText("No plugins found")).toBeInTheDocument();
      });
    });

    it("Instruments tab shows only instruments", async () => {
      render(<PluginManagerDialog onClose={onClose} />);
      await waitFor(() => {
        expect(screen.getByText("Synth Pad")).toBeInTheDocument();
      });

      fireEvent.click(screen.getByRole("button", { name: /Instruments/ }));

      expect(screen.getByText("Synth Pad")).toBeInTheDocument();
      expect(screen.getByText("Drum Machine")).toBeInTheDocument();
      expect(screen.queryByText("EQ Eight")).not.toBeInTheDocument();
      expect(screen.queryByText("Reverb")).not.toBeInTheDocument();
    });

    it("Effects tab shows only effects", async () => {
      render(<PluginManagerDialog onClose={onClose} />);
      await waitFor(() => {
        expect(screen.getByText("Synth Pad")).toBeInTheDocument();
      });

      fireEvent.click(screen.getByRole("button", { name: /Effects/ }));

      expect(screen.getByText("EQ Eight")).toBeInTheDocument();
      expect(screen.getByText("Reverb")).toBeInTheDocument();
      expect(screen.queryByText("Synth Pad")).not.toBeInTheDocument();
      expect(screen.queryByText("Drum Machine")).not.toBeInTheDocument();
    });

    it("All tab shows all plugins", async () => {
      render(<PluginManagerDialog onClose={onClose} />);
      await waitFor(() => {
        expect(screen.getByText("Synth Pad")).toBeInTheDocument();
      });

      fireEvent.click(screen.getByRole("button", { name: /Effects/ }));
      expect(screen.queryByText("Synth Pad")).not.toBeInTheDocument();

      fireEvent.click(screen.getByRole("button", { name: /All/ }));
      expect(screen.getByText("Synth Pad")).toBeInTheDocument();
      expect(screen.getByText("EQ Eight")).toBeInTheDocument();
    });
  });

  describe("close behavior", () => {
    it("clicking close button calls onClose", async () => {
      render(<PluginManagerDialog onClose={onClose} />);
      fireEvent.click(screen.getByText("\u00d7"));
      expect(onClose).toHaveBeenCalledTimes(1);
    });

    it("clicking overlay calls onClose", async () => {
      const { container } = render(<PluginManagerDialog onClose={onClose} />);
      const overlay = container.querySelector(".modal-overlay")!;
      fireEvent.click(overlay);
      expect(onClose).toHaveBeenCalledTimes(1);
    });

    it("clicking inside dialog does not close", async () => {
      const { container } = render(<PluginManagerDialog onClose={onClose} />);
      const dialog = container.querySelector(".plugin-manager")!;
      fireEvent.click(dialog);
      expect(onClose).not.toHaveBeenCalled();
    });

  });

  describe("custom scan paths", () => {
    it("fetches custom paths on mount", async () => {
      render(<PluginManagerDialog onClose={onClose} />);
      await waitFor(() => {
        expect(mockedCall).toHaveBeenCalledWith("plugin.getCustomScanDirs");
      });
    });

    it("shows existing custom paths", async () => {
      setupWithCustomPaths(["C:\\VSTPlugins", "/usr/lib/vst"]);
      render(<PluginManagerDialog onClose={onClose} />);
      await waitFor(() => {
        expect(screen.getByText("C:\\VSTPlugins")).toBeInTheDocument();
        expect(screen.getByText("/usr/lib/vst")).toBeInTheDocument();
      });
    });

    it("shows empty state when no custom paths", async () => {
      render(<PluginManagerDialog onClose={onClose} />);
      await waitFor(() => {
        expect(screen.getByText("No custom scan paths added")).toBeInTheDocument();
      });
    });

    it("shows Add button for new path", async () => {
      render(<PluginManagerDialog onClose={onClose} />);
      expect(screen.getByRole("button", { name: "Add" })).toBeInTheDocument();
    });
  });
});
