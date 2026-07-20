import { contextBridge, ipcRenderer } from "electron";

contextBridge.exposeInMainWorld("hdaw", {
  invoke: (channel: string, ...args: unknown[]) =>
    ipcRenderer.invoke(channel, ...args),
  showOpenDialog: (options: Electron.OpenDialogOptions) =>
    ipcRenderer.invoke("show-open-dialog", options),
  showSaveDialog: (options: Electron.SaveDialogOptions) =>
    ipcRenderer.invoke("show-save-dialog", options),
  readDirectory: (dirPath: string) =>
    ipcRenderer.invoke("fs-readdir", dirPath),
  isDirty: () =>
    ipcRenderer.invoke("is-dirty"),
  saveProject: () =>
    ipcRenderer.invoke("save-project"),
  requestClose: () =>
    ipcRenderer.invoke("request-close"),
  on: (channel: string, callback: (...args: unknown[]) => void) => {
    ipcRenderer.on(channel, (_event, ...args) => callback(...args));
  },
});
