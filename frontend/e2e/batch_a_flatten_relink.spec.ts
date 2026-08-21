import { test, expect } from "@playwright/test";
import { startApp, rpcCall } from "./helpers";
import * as fs from "fs";
import * as path from "path";
import * as os from "os";

test.describe("Batch A: Flatten arranger & missing-file relink", () => {
  test.beforeEach(async ({ page }) => {
    await startApp(page);
  });

  test("relink: relinkAllMissingFiles reports counts", async ({ page }) => {
    const tmpDir = os.tmpdir();
    const wavName = `hdaw-e2e-relink-${Date.now()}.wav`;
    const wavPath = path.join(tmpDir, wavName);
    const sampleCount = 4410;
    const dataSize = sampleCount * 2;
    const wavBuf = Buffer.alloc(44 + dataSize);
    wavBuf.write("RIFF", 0);
    wavBuf.writeUInt32LE(44 + dataSize - 8, 4);
    wavBuf.write("WAVE", 8);
    wavBuf.write("fmt ", 12);
    wavBuf.writeUInt32LE(16, 16);
    wavBuf.writeUInt16LE(1, 20);
    wavBuf.writeUInt16LE(1, 22);
    wavBuf.writeUInt32LE(44100, 24);
    wavBuf.writeUInt32LE(88200, 28);
    wavBuf.writeUInt16LE(2, 32);
    wavBuf.writeUInt16LE(16, 34);
    wavBuf.write("data", 36);
    wavBuf.writeUInt32LE(dataSize, 40);
    fs.writeFileSync(wavPath, wavBuf);

    const fakePath = path.join(tmpDir, `hdaw-e2e-missing-${Date.now()}.wav`);

    await rpcCall(page, "project.addAudioClip", {
      trackIndex: 0,
      start: 0,
      duration: 0.1,
      sourceFile: fakePath,
      name: "Relink Test",
    });

    const result = await rpcCall<{ found: number; totalMissing: number }>(
      page,
      "project.relinkAllMissingFiles",
      { searchDir: tmpDir },
    );
    expect(result.totalMissing).toBeGreaterThanOrEqual(1);

    fs.unlinkSync(wavPath);
  });

  test("flatten: expands chain entries into timeline clips", async ({ page }) => {
    await rpcCall(page, "project.addMidiClip", {
      trackIndex: 0,
      start: 0,
      duration: 4,
      name: "Content",
    });

    const regionId = await rpcCall<string>(page, "project.addArrangerRegion", {
      name: "Verse",
      startTime: 0,
      duration: 4,
    });
    const chainId = await rpcCall<string>(page, "project.addArrangerChain", {
      name: "Test Chain",
    });
    await rpcCall(page, "project.addChainEntry", {
      chainID: chainId,
      regionID: regionId,
      repeatCount: 2,
    });
    await rpcCall(page, "project.setArrangerChainActive", { chainID: chainId });

    await rpcCall(page, "project.flattenArranger", {});

    await expect(async () => {
      const snap = await rpcCall<{ clips: unknown[] }>(page, "read.snapshot");
      expect(snap.clips.length).toBeGreaterThan(1);
    }).toPass({ timeout: 10000 });
  });
});
