import { RpcClient } from "./rpc/client";

const api = (window as any).__HDAW_ELECTRON_API__ as { rpcPort?: number } | undefined;
const injected = (window as any).__HDAW_WS_PORT__ as number | undefined;
const port = api?.rpcPort ?? injected ?? 8766;
export const rpc = new RpcClient(port);
