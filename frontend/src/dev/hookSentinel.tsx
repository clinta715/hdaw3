import React, { Component, ErrorInfo, ReactNode, ComponentType } from "react";

interface SentinelProps {
  name: string;
  children: ReactNode;
}

interface SentinelState {
  error: Error | null;
}

/**
 * Dev-only ErrorBoundary that catches React error #300 ("Rendered fewer hooks
 * than expected") and logs the component name + stack to the console.
 *
 * In production builds the HOC is a no-op passthrough (zero overhead).
 */
class HookSentinelBoundary extends Component<SentinelProps, SentinelState> {
  state: SentinelState = { error: null };

  static getDerivedStateFromError(error: Error): SentinelState {
    return { error };
  }

  componentDidCatch(error: Error, errorInfo: ErrorInfo) {
    const msg = error.message ?? "";
    if (msg.includes("#300") || msg.includes("fewer hooks")) {
      console.error(
        `\n[HookSentinel] React error #300 detected in component "${this.props.name}"\n`,
        "Component stack:",
        errorInfo.componentStack,
        "\n",
      );
    }
  }

  render() {
    if (this.state.error) {
      const msg = this.state.error.message ?? "";
      if (msg.includes("#300") || msg.includes("fewer hooks")) {
        throw new Error(
          `[HookSentinel] React error #300 in "${this.props.name}": ${this.state.error.message}`,
        );
      }
      throw this.state.error;
    }
    return this.props.children;
  }
}

/**
 * Wrap a component with the hook-count sentinel.  In development the wrapper
 * catches React error #300, logs the component name + stack, then re-throws
 * so the outer ErrorBoundary still handles it.  In production the original
 * component is returned unchanged.
 *
 * Usage:
 *   const SentineledClipEditor = withHookSentinel(ClipEditor, "ClipEditor");
 *   // …use SentineledClipEditor in JSX exactly like ClipEditor
 */
export function withHookSentinel<P extends object>(
  WrappedComponent: ComponentType<P>,
  componentName: string,
): ComponentType<P> {
  if (process.env.NODE_ENV !== "development") {
    return WrappedComponent;
  }

  const SentinelWrapper: ComponentType<P> = (props) => (
    <HookSentinelBoundary name={componentName}>
      <WrappedComponent {...props} />
    </HookSentinelBoundary>
  );

  SentinelWrapper.displayName = `withHookSentinel(${componentName})`;
  return SentinelWrapper;
}

// Module-level render counter map. Keys are component names, values are render counts.
// Exposed on window so the dev console can inspect counts:
//   window.__HDAW_GET_RENDER_COUNTS()       → Record<name, count>
//   window.__HDAW_RESET_RENDER_COUNTS()     → clears the map
const renderCounts = new Map<string, number>();

function getRenderCounts(): Record<string, number> {
  const obj: Record<string, number> = {};
  renderCounts.forEach((v, k) => { obj[k] = v; });
  return obj;
}

function resetRenderCounts(): void {
  renderCounts.clear();
}

if (typeof window !== "undefined") {
  const w = window as any;
  w.__HDAW_RENDER_COUNTS = renderCounts;
  w.__HDAW_GET_RENDER_COUNTS = getRenderCounts;
  w.__HDAW_RESET_RENDER_COUNTS = resetRenderCounts;
}

/**
 * Wrap a component to count its renders. Counter is always maintained; the
 * `console.warn` every 10 renders is gated on `window.HDAW_DEBUG_RENDERS`.
 *
 * Use directly when you only want a counter:
 *
 *   const Countered = withRenderCount(MyComponent, "MyComponent");
 *
 * Compose with `withHookSentinel` to get error #300 detection AND counting.
 * Sentinel is OUTER so it catches errors thrown from inside the counter:
 *
 *   const Instrumented = withHookSentinel(
 *     withRenderCount(MyComponent, "MyComponent"),
 *     "MyComponent"
 *   );
 *
 * IMPORTANT: this HOC must NOT call any React hooks. It is designed to be
 * composable with `withHookSentinel` without breaking the hook-count invariant
 * that React error #300 guards against.
 */
export function withRenderCount<P extends object>(
  WrappedComponent: ComponentType<P>,
  componentName: string,
): ComponentType<P> {
  const RenderCounter: ComponentType<P> = (props) => {
    const count = (renderCounts.get(componentName) ?? 0) + 1;
    renderCounts.set(componentName, count);

    if ((window as any).HDAW_DEBUG_RENDERS) {
      if (count > 1 && count % 10 === 0) {
        console.warn(
          `[RenderCount] "${componentName}" has rendered ${count} times. ` +
          `Call window.__HDAW_GET_RENDER_COUNTS() to see all counts.`
        );
      }
    }

    return <WrappedComponent {...props} />;
  };

  RenderCounter.displayName = `withRenderCount(${componentName})`;
  return RenderCounter;
}
