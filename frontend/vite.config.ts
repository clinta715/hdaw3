import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

export default defineConfig({
  plugins: [react()],
  base: "./",
  server: {
    // Bind all interfaces so the dev server is reachable via 127.0.0.1 (IPv4),
    // matching the engine and the Playwright E2E config. Vite 6 otherwise binds
    // only the IPv6 loopback (::1), which 127.0.0.1 probes can't reach.
    host: true,
    port: 5173,
    strictPort: true,
  },
  build: {
    outDir: "dist",
    // Match tsconfig target — skips polyfills for features all modern
    // browsers support, producing smaller/faster output.
    target: "es2020",
    // No source maps in production (embedded SPA via qrc, no CDN).
    sourcemap: false,
    rollupOptions: {
      output: {
        entryFileNames: "assets/index.js",
        chunkFileNames: "assets/[name].js",
        assetFileNames: "assets/index.[ext]",
      },
    },
  },
});
