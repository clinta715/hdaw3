# Handoff: PluginIsolation.LiveDropDrainsStaleOutput (2026-08-23)

## Context

One pre-existing test failure remains in the suite after fixing all 7 original
failures. This test lives in `tests/integration/proxy/isolation_integration_test.cpp:784`.

## Test purpose

`LiveDropDrainsStaleOutput` verifies the plugin isolation proxy's live drop path:

When the input ring is full (child is slow), `PluginProxySlot::processBlock`
drops the block instead of spinning. The contract is:
1. **Caller's buffer is untouched** — dry audio passes through (no stale output injected)
2. **Output ring is drained** — `outputReadPos` catches up to `outputWritePos` so
   no stale output survives for a future read

## Failure

The assertion at line 842 fires: `drop path must pass dry audio through, not stale output`

The `b3` buffer (filled with `0.3f`) has been modified — some samples are no
longer `0.3f`, meaning the drop path is writing stale output into the caller's
buffer instead of leaving it untouched.

## Key code

**Drop path** — `src/proxy/pluginproxyslot.cpp:433-440`:
```cpp
} else {
    // Live drop path: the ring is full — drop this block (caller
    // passes dry audio through) and drain the output ring so no
    // stale output survives for a future read.
    hdr->outputReadPos.store(hdr->outputWritePos.load(std::memory_order_acquire),
                             std::memory_order_release);
    return;
}
```

This drains the output ring and returns. It does NOT modify the caller's buffer.
So something else is writing stale output into the buffer.

**Output ring read** — `src/plugin/pluginproxyslot.cpp:500+` (the `processBlock`
output-read path after the main write):

The output ring read happens inside `processBlock` when the input write succeeds
(blocks 0, 1, 2). The stale output could be leaking from a **previous block's
output read** into the buffer before block 3's drop path fires.

**Test sequence:**
1. Block 0 (0.05f) — written to ring, child starts processing
2. Block 1 (0.1f) — written to ring (child still processing block 0)
3. Sleep 400ms — child finishes block 0, writes output; child starts block 1
4. Block 2 (0.2f) — written to ring (child processing block 1)
5. Block 3 (0.3f) — **input ring full** → drop path fires

## Hypothesis

The stale output in `b3` comes from the **output ring read of block 2**. When
block 2's `processBlock` call reads from the output ring (after successfully
writing block 2 to the input ring), it may read stale output from block 0 that
was produced while block 2's buffer was still being filled. The output ring read
loop writes directly into the caller's buffer, so if it reads stale data from
the output ring, it contaminates the buffer.

Alternatively, there may be a race: block 2 reads output, block 3 drops, but
the output ring read for block 2 finishes AFTER block 3's drop, meaning the
output ring wasn't actually drained between reads.

## Investigation steps

1. **Read the full `processBlock` output-read path** — `PluginProxySlot.cpp`
   after line 500 — to understand when/how output is read into the caller's
   buffer
2. **Check if the output read is conditional on the input write succeeding**
   — if block 2 reads output AND block 3 drops, there could be a window where
   block 2's output read is still in flight
3. **Check if `outputReadPos` is advanced during the input-write success path**
   — the drop path drains it, but does the successful write path also read and
   advance it? If so, block 2's read may have already advanced `outputReadPos`
   past some stale data before block 3's drop fires
4. **Consider a timing fix**: the drop path's drain may be racing with a
   concurrent output read from a previous block. The drain needs to happen
   BEFORE any output read, not just during the drop.

## Files

- `tests/integration/proxy/isolation_integration_test.cpp:784` — the failing test
- `src/proxy/pluginproxyslot.cpp:420-440` — drop path (drains output ring)
- `src/proxy/pluginproxyslot.cpp:500+` — output ring read into caller's buffer

## Note

This is a **timing/race** issue in the proxy's ring-buffer protocol, not a
logic error in the drain itself. The drain correctly sets `outputReadPos` =
`outputWritePos`, but the caller's buffer may have already been contaminated
by a previous output read before the drop path fires.
