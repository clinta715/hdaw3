import { app, BrowserWindow, Menu, dialog, ipcMain } from "electron";
import { ChildProcess, spawn } from "child_process";
import * as path from "path";
import * as net from "net";
import * as fs from "fs";

const DEFAULT_PORT = 8766;
const MAX_CRASHES = 3;

let childProcess: ChildProcess | null = null;
let mainWindow: BrowserWindow | null = null;
let crashCount = 0;
let showingCrashDialog = false;

function getPort(): number {
  const idx = process.argv.indexOf("--port");
  if (idx >= 0 && idx + 1 < process.argv.length) {
    const p = parseInt(process.argv[idx + 1], 10);
    if (!isNaN(p)) return p;
  }
  return DEFAULT_PORT;
}

function waitForPort(port: number, timeoutMs = 8000): Promise<void> {
  const start = Date.now();
  return new Promise((resolve, reject) => {
    const tryConnect = () => {
      const sock = new net.Socket();
      sock.once("connect", () => {
        sock.destroy();
        resolve();
      });
      sock.once("error", () => {
        sock.destroy();
        if (Date.now() - start > timeoutMs) {
          reject(new Error(`Timed out waiting for port ${port}`));
        } else {
          setTimeout(tryConnect, 200);
        }
      });
      sock.connect(port, "127.0.0.1");
    };
    tryConnect();
  });
}

function enginePath(): string {
  if (app.isPackaged) {
    return path.join(process.resourcesPath, "engine", "HDAW_headless.exe");
  }
  return path.resolve(__dirname, "..", "..", "build", "Debug", "HDAW_headless.exe");
}

function spawnEngine(port: number): ChildProcess {
  const ep = enginePath();
  const proc = spawn(ep, [`--port=${port}`], {
    stdio: ["ignore", "pipe", "pipe"],
    windowsHide: true,
  });
  proc.stdout?.on("data", (data) => console.log(`[engine] ${data}`));
  proc.stderr?.on("data", (data) => console.error(`[engine] ${data}`));

  proc.on("error", (err) => {
    console.error("[engine] spawn error:", err.message);
    if (mainWindow && !showingCrashDialog) {
      showingCrashDialog = true;
      dialog.showErrorBox("Engine Failed to Start", err.message);
      showingCrashDialog = false;
      app.quit();
    }
  });

  proc.on("exit", (code, signal) => {
    console.log(`[engine] exited code=${code} signal=${signal}`);
    if (mainWindow && !showingCrashDialog) {
      showingCrashDialog = true;
      crashCount++;
      if (crashCount >= MAX_CRASHES) {
        dialog.showErrorBox(
          "Engine Crashed Too Many Times",
          `The engine has crashed ${crashCount} times. Please check your setup and try again.`
        );
        showingCrashDialog = false;
        app.quit();
        return;
      }
      dialog.showMessageBox(mainWindow, {
        type: "error",
        title: "Engine Crashed",
        message: "The audio engine has stopped unexpectedly.",
        detail: `Exit code: ${code}${signal ? ` Signal: ${signal}` : ""} (attempt ${crashCount}/${MAX_CRASHES})`,
        buttons: ["Restart", "Quit"],
        defaultId: 0,
      }).then(({ response }) => {
        showingCrashDialog = false;
        if (response === 0) {
          childProcess = spawnEngine(port);
          waitForPort(port).then(() => {
            mainWindow?.webContents.reload();
          }).catch((err) => {
            console.error("Failed to restart engine:", err);
            app.quit();
          });
        } else {
          app.quit();
        }
      });
    }
  });
  return proc;
}

async function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1400,
    height: 900,
    backgroundColor: "#141416",
    autoHideMenuBar: true,
    icon: app.isPackaged
      ? path.join(process.resourcesPath, "..", "build-resources", "icon.ico")
      : path.resolve(__dirname, "..", "build-resources", "icon.ico"),
    webPreferences: {
      preload: path.join(__dirname, "preload.js"),
      contextIsolation: true,
      nodeIntegration: false,
    },
  });

  // Intercept close to check for unsaved changes
  mainWindow.on("close", (e) => {
    if (mainWindow && !mainWindow.isDestroyed()) {
      // Ask the renderer if it's dirty — it will handle the dialog itself
      // and call request-close when ready. Block the close for now.
      e.preventDefault();
      mainWindow.webContents.send("app-close-requested");
    }
  });

  if (process.env.NODE_ENV === "development" || !app.isPackaged) {
    await mainWindow.loadURL("http://localhost:5173");
  } else {
    await mainWindow.loadFile(path.join(__dirname, "..", "dist", "index.html"));
  }
}

function setupIpc() {
  ipcMain.handle("show-open-dialog", async (_event, options: Electron.OpenDialogOptions) => {
    if (!mainWindow) return { canceled: true, filePaths: [] };
    return dialog.showOpenDialog(mainWindow, options);
  });

  ipcMain.handle("show-save-dialog", async (_event, options: Electron.SaveDialogOptions) => {
    if (!mainWindow) return { canceled: true, filePath: "" };
    return dialog.showSaveDialog(mainWindow, options);
  });

  ipcMain.handle("fs-readdir", async (_event, dirPath: string) => {
    try {
      const entries = fs.readdirSync(dirPath, { withFileTypes: true });
      return entries.map((e) => ({
        name: e.name,
        isDir: e.isDirectory(),
        path: path.join(dirPath, e.name),
      }));
    } catch {
      return [];
    }
  });

  // Renderer reports its dirty state when asked
  ipcMain.handle("is-dirty", async () => {
    return false; // actual value comes from renderer via request-close flow
  });

  // Renderer handles save via its own RPC
  ipcMain.handle("save-project", async () => {
    if (mainWindow) {
      mainWindow.webContents.send("do-save");
    }
  });

  // 3-button confirm dialog: returns "save", "dont-save", or "cancel"
  ipcMain.handle("show-close-confirm", async () => {
    if (!mainWindow) return "cancel";
    const { response } = await dialog.showMessageBox(mainWindow, {
      type: "question",
      title: "Unsaved Changes",
      message: "Do you want to save changes before closing?",
      buttons: ["Save", "Don't Save", "Cancel"],
      defaultId: 0,
      cancelId: 2,
    });
    if (response === 0) return "save";
    if (response === 1) return "dont-save";
    return "cancel";
  });

  // Renderer requests to close — used after it handles the dirty check itself
  ipcMain.handle("request-close", async () => {
    if (mainWindow) {
      mainWindow.destroy();
    }
  });
}

app.whenReady().then(async () => {
  setupIpc();

  // Remove the default application menu entirely. On Windows/Linux the menu
  // bar's accelerator keys steal modifier presses that the timeline uses for
  // clip interactions — most notably Alt (paint/repeat), which activates the
  // menu bar and disrupts the in-flight drag. HDAW defines its own keyboard
  // shortcuts in the renderer, so the menu provides no value here.
  Menu.setApplicationMenu(null);
  // Keep a convenient way to open DevTools now that the default menu is gone.
  const { globalShortcut } = await import("electron");
  if (!app.isPackaged) {
    globalShortcut.register("CommandOrControl+Shift+I", () => {
      mainWindow?.webContents.toggleDevTools();
    });
  }

  const port = getPort();
  childProcess = spawnEngine(port);
  try {
    await waitForPort(port);
    console.log("[main] Engine ready on port", port);
  } catch (err) {
    dialog.showErrorBox("Engine Startup Failed", String(err));
    app.quit();
    return;
  }
  await createWindow();
});

app.on("window-all-closed", () => {
  if (childProcess) {
    childProcess.kill();
    childProcess = null;
  }
  app.quit();
});

app.on("before-quit", () => {
  if (childProcess) {
    childProcess.kill();
    childProcess = null;
  }
});

// Release the DevTools shortcut registered in whenReady().
app.on("will-quit", async () => {
  const { globalShortcut } = await import("electron");
  globalShortcut.unregisterAll();
});
