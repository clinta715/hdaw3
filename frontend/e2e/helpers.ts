import { Page, expect } from "@playwright/test";
import * as fs from "fs";
import * as os from "os";
import * as path from "path";

// ── App startup ──────────────────────────────────────────────────────────────
// The StartupDialog gates the App. Click "New Project" to create a fresh default
// project (deterministic starting state) and reach the main UI. Every test calls
// this so tests don't see each other's clips (the engine is a shared singleton).
export async function startApp(page: Page) {
  await page.goto("/");
  const startupBtn = page.locator(".startup-btn.primary");
  try {
    await startupBtn.waitFor({ state: "visible", timeout: 10000 });
  } catch {
    // The app root only mounts after the initial WS connect + read.snapshot
    // succeed (main.tsx init() renders nothing until then). A busy engine or a
    // failed first sync leaves a blank page — recover with one reload instead
    // of failing the test outright.
    await page.reload();
    await startupBtn.waitFor({ state: "visible", timeout: 12000 });
  }
  await startupBtn.click();
  await expect(page.locator(".tl-track-row").first()).toBeVisible({ timeout: 20000 });
}

// ── RPC (through the app's own WebSocket connection — see window.rpc seam) ───
export async function rpcCall<T = unknown>(
  page: Page,
  method: string,
  params?: Record<string, unknown>,
): Promise<T> {
  return (await page.evaluate(
    ([m, p]: [string, Record<string, unknown> | null]) =>
      p == null ? (window as any).rpc.call(m) : (window as any).rpc.call(m, p),
    [method, params ?? null] as [string, Record<string, unknown> | null],
  )) as T;
}

export function clipLocator(page: Page, clipId: number) {
  return page.locator(`.tl-clip[data-clip-id="${clipId}"]`);
}

export async function clipLeft(page: Page, clipId: number): Promise<number> {
  const box = await clipLocator(page, clipId).boundingBox();
  if (!box) throw new Error(`clip ${clipId} has no bounding box`);
  return box.x;
}

// Create a MIDI clip and wait for it to render. Track 0 ("Track 1") is empty in
// the default project, so clips placed at start 0 are visible and non-overlapping.
export async function addMidiClip(
  page: Page,
  opts: { trackIndex?: number; start?: number; duration?: number; name?: string } = {},
): Promise<number> {
  const clipId = await rpcCall<number>(page, "project.addMidiClip", {
    trackIndex: opts.trackIndex ?? 0,
    start: opts.start ?? 0,
    duration: opts.duration ?? 2,
    name: opts.name ?? "E2E MIDI",
  });
  await expect(clipLocator(page, clipId)).toBeVisible({ timeout: 10000 });
  return clipId;
}

export async function addAudioClip(
  page: Page,
  opts: { trackIndex?: number; start?: number; duration?: number; sourceFile: string; name?: string },
): Promise<number> {
  const clipId = await rpcCall<number>(page, "project.addAudioClip", {
    trackIndex: opts.trackIndex ?? 0,
    start: opts.start ?? 0,
    duration: opts.duration ?? 4,
    sourceFile: opts.sourceFile,
    name: opts.name ?? "E2E Audio",
  });
  await expect(clipLocator(page, clipId)).toBeVisible({ timeout: 10000 });
  return clipId;
}

// ── Drag a clip by its center ────────────────────────────────────────────────
// Moves in steps so the pointer crosses the 4px engagement threshold (a real
// drag, not a click). Hold modifiers (e.g. Control) for ctrl-drag duplicate.
export async function dragClip(
  page: Page,
  clipId: number,
  dx: number,
  dy: number,
  opts: { modifiers?: ("Control" | "Shift" | "Alt")[] } = {},
) {
  const box = await clipLocator(page, clipId).boundingBox();
  if (!box) throw new Error(`clip ${clipId} has no bounding box`);
  const startX = box.x + box.width / 2;
  const startY = box.y + box.height / 2;
  for (const m of opts.modifiers ?? []) await page.keyboard.down(m);
  try {
    await page.mouse.move(startX, startY);
    await page.mouse.down();
    await page.mouse.move(startX + dx, startY + dy, { steps: 10 });
    await page.mouse.up();
  } finally {
    for (const m of opts.modifiers ?? []) await page.keyboard.up(m);
  }
}

// ── Generate a valid mono 16-bit PCM WAV (a sine wave) ───────────────────────
// Used to create a real audio clip whose waveform peaks are non-zero, so the
// audio-editor waveform render can be asserted on.
export function writeSineWav(
  filePath: string,
  opts: { sampleRate?: number; seconds?: number; freq?: number; amplitude?: number } = {},
): void {
  const sampleRate = opts.sampleRate ?? 44100;
  const seconds = opts.seconds ?? 1;
  const freq = opts.freq ?? 220;
  const amplitude = opts.amplitude ?? 0.8;
  const numSamples = Math.floor(sampleRate * seconds);
  const numChannels = 1;
  const bitsPerSample = 16;
  const blockAlign = numChannels * (bitsPerSample / 8);
  const byteRate = sampleRate * blockAlign;
  const dataSize = numSamples * blockAlign;

  const buffer = Buffer.alloc(44 + dataSize);
  buffer.write("RIFF", 0, "ascii");
  buffer.writeUInt32LE(36 + dataSize, 4);
  buffer.write("WAVE", 8, "ascii");
  buffer.write("fmt ", 12, "ascii");
  buffer.writeUInt32LE(16, 16);
  buffer.writeUInt16LE(1, 20); // PCM
  buffer.writeUInt16LE(numChannels, 22);
  buffer.writeUInt32LE(sampleRate, 24);
  buffer.writeUInt32LE(byteRate, 28);
  buffer.writeUInt16LE(blockAlign, 32);
  buffer.writeUInt16LE(bitsPerSample, 34);
  buffer.write("data", 36, "ascii");
  buffer.writeUInt32LE(dataSize, 40);

  for (let i = 0; i < numSamples; i++) {
    const t = i / sampleRate;
    const sample = Math.round(amplitude * 32767 * Math.sin(2 * Math.PI * freq * t));
    buffer.writeInt16LE(sample, 44 + i * 2);
  }
  fs.writeFileSync(filePath, buffer);
}

export function tempWavPath(name: string): string {
  return path.join(os.tmpdir(), `hdaw-e2e-${Date.now()}-${name}.wav`);
}

// ── Generate a minimal valid MIDI file (format 0, one track, one note) ──────
// Used by the import-dialog journey: project.importMidiFile needs a real file
// on disk (the engine resolves the path from its own cwd, so tests must pass
// an absolute path). Returns the path; caller is responsible for cleanup.
export function writeTestMidi(name = "import"): string {
  const trackData = Buffer.from([
    0x00, 0x90, 60, 100,        // delta 0: note on, channel 0, C4, velocity 100
    0x83, 0x60, 0x80, 60, 0x00, // delta 480 (VLQ 0x83 0x60): note off
    0x00, 0xff, 0x2f, 0x00,     // end of track
  ]);
  const header = Buffer.alloc(14);
  header.write("MThd", 0, "ascii");
  header.writeUInt32BE(6, 4);
  header.writeUInt16BE(0, 8);    // format 0
  header.writeUInt16BE(1, 10);   // 1 track
  header.writeUInt16BE(480, 12); // 480 ticks per quarter note
  const trackHeader = Buffer.alloc(8);
  trackHeader.write("MTrk", 0, "ascii");
  trackHeader.writeUInt32BE(trackData.length, 4);
  const midiPath = path.join(os.tmpdir(), `hdaw-e2e-${Date.now()}-${name}.mid`);
  fs.writeFileSync(midiPath, Buffer.concat([header, trackHeader, trackData]));
  return midiPath;
}

// ── Wait for snapshot sync after an RPC that mutates the project ─────────────
// The backend pushes snapshot updates via a debounced tree-change notification
// (~16 ms). After an RPC that adds/removes/modifies tracks or clips, the DOM
// won't reflect the change until the push arrives. This helper waits for a
// DOM condition to become true, bridging the async gap.
export async function waitForTrackCount(page: Page, count: number, timeout = 10000) {
  await expect(page.locator(".th-row")).toHaveCount(count, { timeout });
}

export async function waitForClipCount(page: Page, count: number, timeout = 10000) {
  await expect(page.locator(".tl-clip")).toHaveCount(count, { timeout });
}
