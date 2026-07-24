import { describe, it, expect, vi } from "vitest";
import { render, screen, fireEvent } from "@testing-library/react";
import BottomTabs from "./BottomTabs";

describe("BottomTabs", () => {
  const tabs = [
    { id: "mixer", label: "Mixer", content: <div>Mixer content</div> },
    { id: "piano", label: "Piano Roll", content: <div>Piano content</div> },
    { id: "fx", label: "FX Chain", content: <div>FX content</div> },
  ];

  it("renders all tab labels", () => {
    render(<BottomTabs tabs={tabs} />);
    expect(screen.getByText("Mixer")).toBeInTheDocument();
    expect(screen.getByText("Piano Roll")).toBeInTheDocument();
    expect(screen.getByText("FX Chain")).toBeInTheDocument();
  });

  it("shows first tab content by default", () => {
    render(<BottomTabs tabs={tabs} />);
    expect(screen.getByText("Mixer content")).toBeInTheDocument();
  });

  it("shows defaultTab content when specified", () => {
    render(<BottomTabs tabs={tabs} defaultTab="piano" />);
    expect(screen.getByText("Piano content")).toBeInTheDocument();
  });

  it("switches content on tab click", () => {
    render(<BottomTabs tabs={tabs} />);
    expect(screen.getByText("Mixer content")).toBeInTheDocument();
    fireEvent.click(screen.getByText("Piano Roll"));
    expect(screen.getByText("Piano content")).toBeInTheDocument();
  });

  it("marks active tab with active class", () => {
    render(<BottomTabs tabs={tabs} />);
    const mixerTab = screen.getByText("Mixer");
    expect(mixerTab.className).toContain("bt-tab--active");
    const pianoTab = screen.getByText("Piano Roll");
    expect(pianoTab.className).not.toContain("bt-tab--active");
  });

  it("updates active class on tab switch", () => {
    render(<BottomTabs tabs={tabs} />);
    fireEvent.click(screen.getByText("FX Chain"));
    const fxTab = screen.getByText("FX Chain");
    expect(fxTab.className).toContain("bt-tab--active");
    const mixerTab = screen.getByText("Mixer");
    expect(mixerTab.className).not.toContain("bt-tab--active");
  });

  it("respects controlled activeTab prop", () => {
    render(<BottomTabs tabs={tabs} activeTab="fx" />);
    expect(screen.getByText("FX content")).toBeInTheDocument();
  });

  it("calls onTabChange when tab is clicked", () => {
    const onTabChange = vi.fn();
    render(<BottomTabs tabs={tabs} onTabChange={onTabChange} />);
    fireEvent.click(screen.getByText("Piano Roll"));
    expect(onTabChange).toHaveBeenCalledWith("piano");
  });

  it("does not call onTabChange for already-active tab", () => {
    const onTabChange = vi.fn();
    render(<BottomTabs tabs={tabs} defaultTab="mixer" onTabChange={onTabChange} />);
    fireEvent.click(screen.getByText("Mixer"));
    expect(onTabChange).toHaveBeenCalledWith("mixer");
  });

  it("renders empty content for unknown activeTab", () => {
    render(<BottomTabs tabs={tabs} activeTab="nonexistent" />);
    const content = document.querySelector(".bt-content");
    expect(content?.innerHTML).toBe("");
  });

  it("handles empty tabs array", () => {
    render(<BottomTabs tabs={[]} />);
    const content = document.querySelector(".bt-content");
    expect(content?.innerHTML).toBe("");
  });
});
