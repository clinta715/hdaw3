import { describe, it, expect, vi, afterEach } from "vitest";
import { render, screen, fireEvent, cleanup } from "@testing-library/react";
import EnvelopeGenerateControl from "./EnvelopeGenerateControl";

describe("EnvelopeGenerateControl", () => {
  afterEach(() => cleanup());

  it("renders without crashing", () => {
    render(<EnvelopeGenerateControl onGenerate={vi.fn()} />);
    expect(screen.getByText("Hide")).toBeInTheDocument();
  });

  it("shows Generate button when collapsed", () => {
    render(<EnvelopeGenerateControl onGenerate={vi.fn()} collapsed />);
    expect(screen.getByText("Generate")).toBeInTheDocument();
  });

  it("expands controls when Generate clicked", () => {
    render(<EnvelopeGenerateControl onGenerate={vi.fn()} collapsed />);
    fireEvent.click(screen.getByText("Generate"));
    expect(screen.getByText("Shape")).toBeInTheDocument();
    expect(screen.getByText("Apply")).toBeInTheDocument();
  });

  it("collapses controls when Hide clicked", () => {
    render(<EnvelopeGenerateControl onGenerate={vi.fn()} />);
    expect(screen.getByText("Shape")).toBeInTheDocument();
    fireEvent.click(screen.getByText("Hide"));
    expect(screen.queryByText("Shape")).not.toBeInTheDocument();
  });

  it("changing shape select updates internal state", () => {
    render(<EnvelopeGenerateControl onGenerate={vi.fn()} />);
    const select = screen.getByLabelText("Shape") as HTMLSelectElement;
    fireEvent.change(select, { target: { value: "sine" } });
    expect(select.value).toBe("sine");
  });

  it("clicking Apply calls onGenerate with correct params", () => {
    const onGenerate = vi.fn();
    render(<EnvelopeGenerateControl onGenerate={onGenerate} />);
    fireEvent.click(screen.getByText("Apply"));
    expect(onGenerate).toHaveBeenCalledWith({
      shape: "ramp",
      start: 0,
      end: 16,
      startValue: 0,
      endValue: 1,
      cycles: 1,
      seed: 0,
    });
  });

  it("Apply passes updated values after user edits", () => {
    const onGenerate = vi.fn();
    render(<EnvelopeGenerateControl onGenerate={onGenerate} />);
    const select = screen.getByLabelText("Shape") as HTMLSelectElement;
    fireEvent.change(select, { target: { value: "triangle" } });
    const endInput = screen.getByLabelText("End") as HTMLInputElement;
    fireEvent.change(endInput, { target: { value: "32" } });
    fireEvent.click(screen.getByText("Apply"));
    expect(onGenerate).toHaveBeenCalledWith(
      expect.objectContaining({ shape: "triangle", end: 32 })
    );
  });

  it("uses defaultValueRange for initial value inputs", () => {
    render(
      <EnvelopeGenerateControl
        onGenerate={vi.fn()}
        defaultValueRange={[0, 127]}
      />
    );
    const startVal = screen.getByLabelText("Val Start") as HTMLInputElement;
    const endVal = screen.getByLabelText("Val End") as HTMLInputElement;
    expect(Number(startVal.value)).toBe(0);
    expect(Number(endVal.value)).toBe(127);
  });
});
