import { describe, it, expect } from "vitest";
import { render } from "@testing-library/react";
import { FolderIcon, KnobIcon, NoteIcon, SlidersIcon } from "./Icons";

describe("Icons", () => {
  const icons: Array<[string, React.ReactElement]> = [
    ["FolderIcon", <FolderIcon />],
    ["KnobIcon", <KnobIcon />],
    ["NoteIcon", <NoteIcon />],
    ["SlidersIcon", <SlidersIcon />],
  ];

  it.each(icons)("%s renders an svg that inherits currentColor", (_name, el) => {
    const { container } = render(el);
    const svg = container.querySelector("svg");
    expect(svg).not.toBeNull();
    // Monochrome by construction: stroke inherits the button's text color.
    expect(svg?.getAttribute("stroke")).toBe("currentColor");
  });

  it("honours a custom size", () => {
    const { container } = render(<FolderIcon size={20} />);
    const svg = container.querySelector("svg");
    expect(svg?.getAttribute("width")).toBe("20");
    expect(svg?.getAttribute("height")).toBe("20");
  });
});
