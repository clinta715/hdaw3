import { NoteSnapshot } from "../../rpc/types";
import { RpcClient } from "../../rpc/client";

export interface Props {
  notes: NoteSnapshot[];
  rpc: RpcClient;
  clipId: number | null;
  pixelsPerBeat: number;
  onVerticalScroll?: (scrollTop: number) => void;
  onHorizontalScroll?: (scrollLeft: number) => void;
  onZoom?: (newPixelsPerBeat: number, anchorClientX: number) => void;
  selectedNoteIds?: Set<number>;
  onSelectionChange?: (ids: Set<number>) => void;
  chordShape?: number[];
  quantizeStrength?: number;
  swing?: number;
}

export interface NoteDragState {
  members: Array<{ noteId: number; startPitch: number; startBeat: number }>;
  anchorIndex: number;
  offsetX: number;
  offsetY: number;
  minPitch: number;
  maxPitch: number;
  minBeat: number;
  pitchDelta: number;
  beatDelta: number;
}

export interface NoteResizeState {
  noteId: number;
  startX: number;
  initialDuration: number;
  currentDuration: number;
}

export interface ContextMenuState {
  x: number;
  y: number;
  noteId: number | null;
}

export interface MarqueeState {
  x1: number;
  y1: number;
  x2: number;
  y2: number;
  additive: boolean;
  baseSelection: Set<number>;
}
