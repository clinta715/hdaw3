interface JsonRpcRequest {
  jsonrpc: "2.0";
  id: number;
  method: string;
  params?: unknown;
}

interface JsonRpcResponse {
  jsonrpc: "2.0";
  id: number;
  result?: unknown;
  error?: { code: number; message: string };
}

type NotificationHandler = (method: string, params: unknown) => void;

class RpcError extends Error {
  constructor(public code: number, message: string) {
    super(message);
    this.name = "RpcError";
  }
}

export class RpcClient {
  private ws: WebSocket | null = null;
  private nextId = 1;
  private pending = new Map<number, { resolve: (v: unknown) => void; reject: (e: Error) => void }>();
  private handlers = new Map<string, Set<NotificationHandler>>();
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private reconnectDelay = 1000;
  private destroyed = false;
  private connecting: Promise<void> | null = null;

  constructor(private port: number) {}

  connect(): Promise<void> {
    if (this.connecting) return this.connecting;
    if (this.ws?.readyState === WebSocket.OPEN) return Promise.resolve();
    if (this.destroyed) return Promise.reject(new Error("client destroyed"));

    this.connecting = new Promise((resolve, reject) => {
      const url = `ws://127.0.0.1:${this.port}`;
      this.ws = new WebSocket(url);

      this.ws.onopen = () => {
        this.connecting = null;
        this.reconnectDelay = 1000;
        resolve();
      };

      this.ws.onmessage = (event) => {
        let msg: unknown;
        try {
          msg = JSON.parse(event.data as string);
        } catch {
          console.warn("RPC: malformed JSON", event.data);
          return;
        }
        const obj = msg as Record<string, unknown>;
        if (obj && typeof obj === "object" && "method" in obj && typeof obj.method === "string") {
          this.dispatchNotification(obj.method, obj.params);
        } else if (obj && typeof obj === "object" && "id" in obj) {
          const resp = obj as unknown as JsonRpcResponse;
          const pd = this.pending.get(resp.id);
          if (pd) {
            this.pending.delete(resp.id);
            if (resp.error) {
              pd.reject(new RpcError(resp.error.code, resp.error.message));
            } else {
              pd.resolve(resp.result);
            }
          }
        }
      };

      this.ws.onerror = () => {
        this.connecting = null;
        reject(new Error("WebSocket connection failed"));
      };

      this.ws.onclose = () => {
        this.connecting = null;
        if (!this.destroyed) {
          this.scheduleReconnect();
        }
      };
    });

    return this.connecting;
  }

  private scheduleReconnect() {
    if (this.reconnectTimer || this.destroyed) return;
    const delay = this.reconnectDelay;
    this.reconnectDelay = Math.min(this.reconnectDelay * 2, 30000);
    const jitter = delay * (0.75 + Math.random() * 0.5);
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      if (this.destroyed) return;
      this.connect().catch(() => this.scheduleReconnect());
    }, jitter);
  }

  disconnect() {
    this.destroyed = true;
    this.connecting = null;
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    if (this.ws) {
      this.ws.onclose = null;
      this.ws.close();
      this.ws = null;
    }
    for (const [, pd] of this.pending) {
      pd.reject(new Error("disconnected"));
    }
    this.pending.clear();
    this.handlers.clear();
  }

  async call(method: string, params?: unknown): Promise<unknown> {
    if (this.destroyed) throw new Error("client destroyed");
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
      await this.connect();
    }
    const id = this.nextId++;
    const req: JsonRpcRequest = { jsonrpc: "2.0", id, method };
    if (params !== undefined) req.params = params;

    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error("RPC timeout"));
      }, 30000);
      this.pending.set(id, { resolve: (v) => { clearTimeout(timeout); resolve(v); }, reject: (e) => { clearTimeout(timeout); reject(e); } });
      this.ws!.send(JSON.stringify(req));
    });
  }

  onNotification(method: string, handler: NotificationHandler): () => void {
    if (!this.handlers.has(method)) {
      this.handlers.set(method, new Set());
    }
    this.handlers.get(method)!.add(handler);
    return () => {
      this.handlers.get(method)?.delete(handler);
    };
  }

  private dispatchNotification(method: string, params: unknown) {
    const handlers = this.handlers.get(method);
    if (handlers) {
      for (const h of handlers) {
        try { h(method, params); } catch { /* ignore */ }
      }
    }
  }
}
