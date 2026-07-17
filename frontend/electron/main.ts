import { app, BrowserWindow, dialog } from "electron";
import { ChildProcess, spawn } from "child_process";
import * as path from "path";
import * as net from "net";

const DEFAULT_PORT = 8766;

let childProcess: ChildProcess | null = null;
let mainWindow: BrowserWindow | null = null;

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

function spawnEngine(port: number): ChildProcess {
  const enginePath = path.resolve(__dirname, "..", "..", "..", "build", "Debug", "HDAW.exe");
  const proc = spawn(enginePath, ["--headless", `--port=${port}`], {
    stdio: ["ignore", "pipe", "pipe"],
  });
  proc.stdout?.on("data", (data) => console.log(`[engine] ${data}`));
  proc.stderr?.on("data", (data) => console.error(`[engine] ${data}`));
  proc.on("exit", (code, signal) => {
    console.log(`[engine] exited code=${code} signal=${signal}`);
    if (mainWindow) {
      dialog.showMessageBox(mainWindow, {
        type: "error",
        title: "Engine Crashed",
        message: "The audio engine has stopped unexpectedly.",
        detail: `Exit code: ${code}${signal ? ` Signal: ${signal}` : ""}`,
        buttons: ["Restart", "Quit"],
        defaultId: 0,
      }).then(({ response }) => {
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
    webPreferences: {
      preload: path.join(__dirname, "preload.js"),
      contextIsolation: true,
      nodeIntegration: false,
    },
  });
  if (process.env.NODE_ENV === "development" || !app.isPackaged) {
    await mainWindow.loadURL("http://localhost:5173");
    mainWindow.webContents.openDevTools();
  } else {
    await mainWindow.loadFile(path.join(__dirname, "..", "dist", "index.html"));
  }
}

app.whenReady().then(async () => {
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
