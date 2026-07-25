import { defineConfig, devices } from "@playwright/test";

export default defineConfig({
  testDir: "./e2e",
  fullyParallel: true,
  forbidOnly: !!process.env.CI,
  retries: process.env.CI ? 2 : 0,
  // The engine is a single process serving one project, so tests share it and
  // must run serially. Each test resets state via "New Project" in startApp().
  workers: 1,
  reporter: "html",
  use: {
    // Vite dev server: serves the live frontend from source, so E2E picks up
    // frontend changes without rebuilding/embedding the SPA into HDAW.exe.
    baseURL: "http://127.0.0.1:5173",
    trace: "on-first-retry",
  },
  projects: [
    {
      name: "chromium",
      use: { ...devices["Desktop Chrome"] },
    },
  ],
  webServer: [
    {
      // Engine: WebSocket RPC (8766) + HTTP (8765, used as the readiness probe).
      // HDAW_NO_BROWSER stops it spawning the system browser during tests.
      command: "cd .. && build\\Debug\\HDAW.exe",
      url: "http://127.0.0.1:8765",
      env: { HDAW_NO_BROWSER: "1" },
      reuseExistingServer: !process.env.CI,
      timeout: 120_000,
    },
    {
      command: "npm run dev",
      url: "http://127.0.0.1:5173",
      reuseExistingServer: !process.env.CI,
      timeout: 120_000,
    },
  ],
});
