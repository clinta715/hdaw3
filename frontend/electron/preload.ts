import { contextBridge, ipcRenderer } from "electron";

contextBridge.exposeInMainWorld("hdaw", {
  invoke: (channel: string, ...args: unknown[]) =>
    ipcRenderer.invoke(channel, ...args),
});
