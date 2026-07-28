import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { render, screen, fireEvent, cleanup } from "@testing-library/react";
import { AddTrackMenu } from "./AddTrackMenu";
import { rpc } from "../rpc";

vi.mock("../rpc", () => ({ rpc: { call: vi.fn() } }));

const mockedCall = rpc.call as unknown as ReturnType<typeof vi.fn>;

describe("AddTrackMenu", () => {
  beforeEach(() => {
    mockedCall.mockReset();
    mockedCall.mockResolvedValue(null);
  });

  afterEach(() => cleanup());

  it("does not open the popover until the trigger is clicked", () => {
    render(<AddTrackMenu />);
    expect(screen.queryByText("Audio Track")).toBeNull();
    fireEvent.click(screen.getByTitle("Add Track"));
    expect(screen.getByText("Audio Track")).toBeInTheDocument();
  });

  it("creates an audio track with trackType 0", () => {
    render(<AddTrackMenu />);
    fireEvent.click(screen.getByTitle("Add Track"));
    fireEvent.click(screen.getByText("Audio Track"));
    expect(mockedCall).toHaveBeenCalledWith("project.addTrack", { trackType: 0 });
  });

  it("creates a MIDI track with trackType 1 and closes the popover", () => {
    render(<AddTrackMenu />);
    fireEvent.click(screen.getByTitle("Add Track"));
    fireEvent.click(screen.getByText("MIDI Track"));
    expect(mockedCall).toHaveBeenCalledWith("project.addTrack", { trackType: 1 });
    expect(screen.queryByText("Audio Track")).toBeNull();
  });

  it("renders a custom label", () => {
    render(<AddTrackMenu label="+ Add Track" />);
    expect(screen.getByText("+ Add Track")).toBeInTheDocument();
  });
});
