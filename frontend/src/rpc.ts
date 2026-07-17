import { RpcClient } from "./rpc/client";

const api = (window as any).electronAPI as { rpcPort: number } | undefined;
const port = api?.rpcPort ?? 8766;
export const rpc = new RpcClient(port);
