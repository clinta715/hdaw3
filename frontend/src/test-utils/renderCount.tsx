import { useRef } from "react";
import { render, act } from "@testing-library/react";
import type { ReactElement } from "react";

/**
 * Wraps a component to count how many times it renders.
 * Returns the wrapper component and a getter for the current count.
 *
 * Usage:
 *   const { Wrapper, getRenderCount } = makeRenderCounter();
 *   render(<Wrapper><MyComponent /></Wrapper>);
 *   // ... trigger a store change ...
 *   expect(getRenderCount()).toBeLessThan(10);
 */
export function makeRenderCounter() {
  let renderCount = 0;

  function Counter({ children }: { children: React.ReactNode }) {
    renderCount++;
    return <>{children}</>;
  }

  return {
    Wrapper: Counter,
    getRenderCount: () => renderCount,
    resetCount: () => { renderCount = 0; },
  };
}

/**
 * Renders a component inside a render counter and asserts it doesn't
 * re-render more than `maxRenders` times after calling `action()`.
 *
 * Fails with a clear message if the component enters a re-render loop.
 */
export async function assertNoRerenderLoop(
  ui: ReactElement,
  action: () => void | Promise<void>,
  maxRenders = 5,
) {
  const { Wrapper, getRenderCount, resetCount } = makeRenderCounter();
  const { unmount } = render(<Wrapper>{ui}</Wrapper>);
  const initialRenders = getRenderCount();
  resetCount();

  await act(async () => {
    await action();
    // Let React flush any pending effects
    await new Promise((r) => setTimeout(r, 50));
  });

  const additionalRenders = getRenderCount();
  unmount();

  if (additionalRenders > maxRenders) {
    throw new Error(
      `Component re-rendered ${additionalRenders} times after action (max ${maxRenders}). ` +
      `Likely a re-render cascade — check useEffect dependencies for store-derived collections.`
    );
  }

  return { initialRenders, additionalRenders };
}
