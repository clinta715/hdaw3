# File Browser Panel — Design Spec

**Date**: 2026-07-19
**Status**: Approved

## Overview

A collapsible right-side file browser panel for exploring user-specified filesystem folders and importing audio/MIDI files into the project.

## Layout

- 240px wide collapsible panel on the right side of the app shell
- Toggle button in the transport bar area + keyboard shortcut (Ctrl+B)
- Three sections: search box, folder tree, action bar

## Folder Management

- "Add Folder" button opens Electron's `dialog.showOpenDialog` with `properties: ['openDirectory']`
- Folders stored in `localStorage` as `hdaw_browser_folders: string[]`
- Right-click context menu on folder → "Remove" to unpin
- Folders persist across sessions

## File Display

- Tree view: expandable folders show child directories and files
- Filters to audio/MIDI extensions: `.wav`, `.aiff`, `.aif`, `.mp3`, `.flac`, `.ogg`, `.mid`, `.midi`
- File icons: speaker icon for audio, note icon for MIDI
- Search box at top filters visible filenames (case-insensitive substring match)
- Expanded/collapsed state tracked per path in store

## Interaction

- **Drag file to timeline**: creates clip at drop position (reuses existing `handleDrop` logic)
- **Double-click file**: imports at playhead position on selected track (or track 0)
- Both use existing `rpc.call("project.addAudioClip", ...)` / `rpc.call("project.addMidiClip", ...)`

## State — `browserStore.ts`

```ts
interface BrowserState {
  folders: string[];              // pinned folder paths
  expandedPaths: Set<string>;     // expanded tree nodes
  selectedFile: string | null;    // highlighted file
  searchQuery: string;            // filter text
  addFolder: (path: string) => void;
  removeFolder: (path: string) => void;
  toggleExpanded: (path: string) => void;
  setSelectedFile: (path: string | null) => void;
  setSearchQuery: (q: string) => void;
}
```

## Filesystem Access

- Electron IPC: `fs-readdir` handler in main.ts reads directory contents
- Returns `{ name: string, isDir: boolean, path: string }[]`
- Falls back gracefully in browser dev mode (no-op or mock)

## New Files

- `frontend/src/store/browserStore.ts` — Zustand store
- `frontend/src/components/FileBrowser.tsx` — main component
- `frontend/src/components/FileBrowser.css` — styles
- `frontend/electron/main.ts` — add `fs-readdir` IPC handler
- `frontend/electron/preload.ts` — expose `readDirectory`
- `frontend/src/electron.d.ts` — add type for `readDirectory`
- `frontend/src/App.tsx` — add panel to layout

## No Backend Changes

All RPC calls already exist. No C++ changes needed.
