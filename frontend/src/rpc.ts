import { RpcClient } from "./rpc/client";

const api = (window as any).__HDAW_ELECTRON_API__ as { rpcPort?: number } | undefined;
const injected = (window as any).__HDAW_WS_PORT__ as number | undefined;
const port = api?.rpcPort ?? injected ?? 8766;
export const rpc = new RpcClient(port);

// Test/debug seam: expose the RPC client so Playwright E2E tests (and devtools)
// can set up deterministic state through the app's own WebSocket connection.
(window as any).rpc = rpc;
