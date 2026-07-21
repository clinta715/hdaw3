import { contextBridge, ipcRenderer } from "electron";

contextBridge.exposeInMainWorld("hdaw", {
  invoke: (channel: string, ...args: unknown[]) =>
    ipcRenderer.invoke(channel, ...args),
  showOpenDialog: (options: Electron.OpenDialogOptions) =>
    ipcRenderer.invoke("show-open-dialog", options),
  showSaveDialog: (options: Electron.SaveDialogOptions) =>
    ipcRenderer.invoke("show-save-dialog", options),
  showCloseConfirm: () =>
    ipcRenderer.invoke("show-close-confirm"),
  readDirectory: (dirPath: string) =>
    ipcRenderer.invoke("fs-readdir", dirPath),
  isDirty: () =>
    ipcRenderer.invoke("is-dirty"),
  saveProject: () =>
    ipcRenderer.invoke("save-project"),
  requestClose: () =>
    ipcRenderer.invoke("request-close"),
  on: (channel: string, callback: (...args: unknown[]) => void) => {
    const wrapped = (_event: unknown, ...args: unknown[]) => callback(...args);
    ipcRenderer.on(channel, wrapped);
    // Return an unsubscribe function so callers (e.g. React useEffect) can
    // clean up. Without this, every remount in React StrictMode registers a
    // fresh listener and they accumulate — close would fire N confirm()
    // dialogs and N requestClose() calls.
    return () => {
      ipcRenderer.removeListener(channel, wrapped);
    };
  },
});
