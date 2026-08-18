import { app, BrowserWindow, Menu, dialog, ipcMain } from "electron";
import { autoUpdater } from "electron-updater";
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
let intentionalQuit = false;

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
  // Dev mode: prefer the optimized build, fall back to Debug.
  const rwdi = path.resolve(__dirname, "..", "..", "build", "RelWithDebInfo", "HDAW_headless.exe");
  if (fs.existsSync(rwdi)) return rwdi;
  return path.resolve(__dirname, "..", "..", "build", "Debug", "HDAW_headless.exe");
}

function spawnEngine(port: number): ChildProcess {
  intentionalQuit = false;
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
    if (intentionalQuit) return; // normal quit — we killed it deliberately, not a crash
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
      ? path.join(process.resourcesPath, "..", "build-resources", "icon.png")
      : path.resolve(__dirname, "..", "build-resources", "icon.png"),
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

  // Diagnostics for the "black screen" class of bug: if the renderer or GPU
  // process dies (crash / OOM), the window goes black with no JS error and no
  // error boundary to catch it. Surface the reason instead of failing silent.
  mainWindow.webContents.on("render-process-gone", (_e, details) => {
    console.error("[main] render-process-gone:", details);
    if (mainWindow && !mainWindow.isDestroyed() && !showingCrashDialog) {
      showingCrashDialog = true;
      dialog.showErrorBox(
        "Renderer Crashed",
        `The UI renderer stopped unexpectedly.\nReason: ${details.reason}\nExit code: ${details.exitCode}\n\n` +
        "Press Ctrl+Shift+A after restarting to open DevTools if it recurs."
      );
      showingCrashDialog = false;
    }
  });

  mainWindow.webContents.on("unresponsive", () => {
    console.error("[main] renderer unresponsive (likely infinite loop)");
    if (mainWindow && !mainWindow.isDestroyed() && !showingCrashDialog) {
      showingCrashDialog = true;
      dialog.showErrorBox(
        "UI Unresponsive",
        "The UI thread stopped responding (possible infinite loop).\n" +
        "Press Ctrl+Shift+A to open DevTools and inspect, or close the app."
      );
      showingCrashDialog = false;
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
  // Registered regardless of packaging: the packaged app has no menu and no
  // other way to open DevTools, which made renderer crashes (black screen)
  // impossible to diagnose. Ctrl+Shift+A toggles DevTools.
  const { globalShortcut } = await import("electron");
  globalShortcut.register("CommandOrControl+Shift+A", () => {
    mainWindow?.webContents.toggleDevTools();
  });

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

  // Auto-update: check for new versions on GitHub Releases.
  if (app.isPackaged) {
    autoUpdater.checkForUpdatesAndNotify().catch((err) => {
      console.warn("[updater] check failed:", err.message);
    });
  }

  // Handle .hdaw file open from command line (double-click file association).
  const filePath = process.argv.find((arg) => arg.endsWith(".hdaw") || arg.endsWith(".hdaw3"));
  if (filePath && mainWindow) {
    mainWindow.webContents.once("did-finish-load", () => {
      mainWindow?.webContents.send("open-project-file", filePath);
    });
  }
});

app.on("window-all-closed", () => {
  intentionalQuit = true;
  if (childProcess) {
    childProcess.kill();
    childProcess = null;
  }
  app.quit();
});

app.on("before-quit", () => {
  intentionalQuit = true;
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
