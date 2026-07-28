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
