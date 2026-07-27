import React from "react";

interface State {
  error: Error | null;
}

// Catches render-time exceptions anywhere in the subtree and shows the error
// + stack in-window instead of letting React unmount the whole tree (which,
// with no boundary present, produces a blank/black window — the symptom we
// were chasing). This is diagnostic: it surfaces the root cause of a crash
// rather than masking it. Clicking Reload re-mounts the app.
export class ErrorBoundary extends React.Component<{ children: React.ReactNode }, State> {
  state: State = { error: null };

  static getDerivedStateFromError(error: Error): State {
    return { error };
  }

  componentDidCatch(error: Error, info: React.ErrorInfo) {
    // Also log to the console so DevTools / engine logs capture it.
    console.error("[ErrorBoundary] render crash:", error, info);
  }

  reload = () => {
    this.setState({ error: null });
    // Hard reload guarantees a clean store/subscription state.
    window.location.reload();
  };

  render() {
    if (this.state.error) {
      const { error } = this.state;
      return (
        <div style={{
          position: "fixed", inset: 0, background: "#1a0d0d", color: "#ffb3b3",
          fontFamily: "monospace", fontSize: 12, padding: 20, overflow: "auto",
          display: "flex", flexDirection: "column", gap: 12,
        }}>
          <div style={{ fontSize: 16, color: "#ff6b6b", fontWeight: 700 }}>
            HDAW hit a render error
          </div>
          <div style={{ color: "#e8a040" }}>{error.message}</div>
          <pre style={{ whiteSpace: "pre-wrap", color: "#c99", margin: 0 }}>
            {error.stack ?? "(no stack)"}
          </pre>
          <div style={{ display: "flex", gap: 8 }}>
            <button
              onClick={this.reload}
              style={{
                padding: "6px 14px", background: "#3a1a1a", color: "#ffb3b3",
                border: "1px solid #6a3a3a", borderRadius: 4, cursor: "pointer",
              }}
            >
              Reload
            </button>
            <button
              onClick={() => { if (this.state.error) navigator.clipboard?.writeText(this.state.error.stack ?? this.state.error.message); }}
              style={{
                padding: "6px 14px", background: "#2a2a2a", color: "#ccc",
                border: "1px solid #444", borderRadius: 4, cursor: "pointer",
              }}
            >
              Copy stack
            </button>
          </div>
          <div style={{ color: "#888", fontSize: 11 }}>
            Please share the message + stack above (press Ctrl+Shift+A to open DevTools for the full console log).
          </div>
        </div>
      );
    }
    return this.props.children;
  }
}
