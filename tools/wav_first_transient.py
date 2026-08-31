#!/usr/bin/env python3
"""WAV forensics: manual RIFF chunk walk + first-nonzero-sample detection.

Keeps the channel-count term (handoff 2026-09-01: two session conclusions were
WRONG before the math was fixed):

    frame_index = byte_index / (channels * bytes_per_sample)
    time        = frame_index / sample_rate

JUCE-written 24-bit files confuse Python's `wave` module, so RIFF is walked
manually. numpy is used for the fast scan when available.

Usage:
    python wav_first_transient.py <file.wav> [--threshold 0.0001]

Output: one JSON line with header info, data size, duration, first-sound
frame index + time, and peak level.
"""
import argparse
import json
import struct
import sys


def riff_walk(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 12 or data[0:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise SystemExit("not a RIFF/WAVE file")
    pos = 12
    fmt = None
    data_chunk = None
    while pos + 8 <= len(data):
        cid = data[pos:pos + 4]
        csize = struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + csize]
        if cid == b"fmt ":
            fmt = body
        elif cid == b"data":
            data_chunk = body
        pos += 8 + csize + (csize & 1)  # chunks are word-aligned
    if fmt is None or data_chunk is None:
        raise SystemExit("missing fmt or data chunk")
    audio_fmt, channels, sample_rate, byte_rate, block_align, bits = struct.unpack(
        "<HHIIHH", fmt[:16])
    return audio_fmt, channels, sample_rate, bits, data_chunk


def frames_to_array(pcm, channels, bits):
    import numpy as np
    if bits == 16:
        arr = np.frombuffer(pcm, dtype="<i2")
    elif bits == 32:
        arr = np.frombuffer(pcm, dtype="<f4")
    elif bits == 24:
        raw = np.frombuffer(pcm, dtype=np.uint8)
        raw = raw.reshape(-1, 3)
        vals = (raw[:, 0].astype(np.int32)
                | (raw[:, 1].astype(np.int32) << 8)
                | (raw[:, 2].astype(np.int32) << 16))
        vals = np.where(vals & 0x800000, vals - 0x1000000, vals)
        return vals.reshape(-1, channels), np.float64
    else:
        raise SystemExit(f"unsupported bit depth {bits}")
    n = (arr.size // channels) * channels
    return arr[:n].reshape(-1, channels), arr.dtype


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav")
    ap.add_argument("--threshold", type=float, default=1e-4)
    args = ap.parse_args()

    audio_fmt, channels, sample_rate, bits, pcm = riff_walk(args.wav)
    import numpy as np
    frames, _ = frames_to_array(pcm, channels, bits)
    duration = frames.shape[0] / sample_rate

    peak = float(np.abs(frames.astype(np.float64)).max()) if frames.size else 0.0
    mag = np.abs(frames.astype(np.float64)).max(axis=1) if frames.size else np.zeros(1)
    nz = np.flatnonzero(mag > args.threshold)
    if nz.size:
        frame_idx = int(nz[0])
        first_time = frame_idx / sample_rate
        first_val = float(frames[frame_idx].astype(np.float64).max())
    else:
        frame_idx, first_time, first_val = None, None, 0.0

    print(json.dumps({
        "file": args.wav,
        "formatTag": audio_fmt,
        "channels": channels,
        "sampleRate": sample_rate,
        "bitsPerSample": bits,
        "dataBytes": len(pcm),
        "frames": int(frames.shape[0]),
        "durationSec": round(duration, 6),
        "peak": round(peak, 6),
        "firstSoundFrame": frame_idx,
        "firstSoundSec": round(first_time, 6) if first_time is not None else None,
        "firstSoundLevel": round(first_val, 6),
        "threshold": args.threshold,
    }))


if __name__ == "__main__":
    main()
