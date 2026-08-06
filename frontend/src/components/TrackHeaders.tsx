import { useEffect, useMemo, useRef, useState } from "react";
import { useProjectStore } from "../store/projectStore";
import { useMeterStore } from "../store/meterStore";
import { useUiStore } from "../store/uiStore";
import { useAutomationStore } from "../store/automationStore";
import { rpc } from "../rpc";
import { colorStr } from "../theme";
import { RULER_HEIGHT, TOOLBAR_HEIGHT } from "../utils/timelineConstants";
import { buildRowLayout } from "../utils/rowLayout";
import { getVisibleTracks } from "../utils/timelineUtils";
import {
  TRACK_TYPE_ICONS,
  TRACK_TYPE_LABELS,
  TRACK_TYPE_CLASSES,
  TRACK_TYPE_COLORS,
} from "../utils/trackTypes";
import { AddTrackMenu } from "./AddTrackMenu";
import "./TrackHeaders.css";

export default function TrackHeaders() {
  const snapshot = useProjectStore((s) => s.snapshot);
  const tracks = snapshot?.tracks ?? [];

  // Must match the timeline's visible-track set and row geometry exactly, so
  // header rows and lanes stay in register (see rowLayout.ts).
  const visibleTracks = useMemo(() => getVisibleTracks(tracks), [tracks]);
  const layout = useMemo(
    () => buildRowLayout(visibleTracks.map((t) => t.height)),
    [visibleTracks]
  );

  const trackMeters = useMeterStore((s) => s.tracks);
  const selectClip = useUiStore((s) => s.selectClip);

  const [expandedTracks, setExpandedTracks] = useState<Set<number>>(new Set());
  const [editingMidiCh, setEditingMidiCh] = useState<number | null>(null);

  const scrollRef = useRef<HTMLDivElement>(null);
  const syncingRef = useRef(false);

  // Keep the header column vertically locked to the timeline lanes. Scrolling
  // either side mirrors to the other; the guard stops the programmatic scroll
  // from re-triggering its counterpart (feedback loop).
  useEffect(() => {
    const header = scrollRef.current;
    const lanes = document.querySelector<HTMLElement>(".tl-tracks");
    if (!header || !lanes) return;
    const mirror = (src: HTMLElement, dst: HTMLElement) => {
      if (syncingRef.current) return;
      if (dst.scrollTop === src.scrollTop) return;
      syncingRef.current = true;
      dst.scrollTop = src.scrollTop;
      requestAnimationFrame(() => {
        syncingRef.current = false;
      });
    };
    const onLanes = () => mirror(lanes, header);
    const onHeader = () => mirror(header, lanes);
    lanes.addEventListener("scroll", onLanes);
    header.addEventListener("scroll", onHeader);
    return () => {
      lanes.removeEventListener("scroll", onLanes);
      header.removeEventListener("scroll", onHeader);
    };
  }, []);

  // Right-click a track header for track operations (add / duplicate / delete).
  const [headerMenu, setHeaderMenu] = useState<{ x: number; y: number; trackIndex: number } | null>(null);

  useEffect(() => {
    const close = (e: MouseEvent) => {
      if ((e.target as HTMLElement).closest(".clip-context-menu")) return;
      setHeaderMenu(null);
    };
    window.addEventListener("mousedown", close);
    return () => window.removeEventListener("mousedown", close);
  }, []);

  const handleMute = (idx: number, muted: boolean, e: React.MouseEvent) => {
    e.stopPropagation();
    useAutomationStore.getState().setLastClickedParamID(3);
    rpc.call("project.setTrackMuted", { trackIndex: idx, muted: !muted }).catch(console.error);
  };

  const handleSolo = (idx: number, soloed: boolean, e: React.MouseEvent) => {
    e.stopPropagation();
    rpc.call("project.setTrackSoloed", { trackIndex: idx, soloed: !soloed }).catch(console.error);
  };

  const handleArm = (idx: number, armed: boolean, e: React.MouseEvent) => {
    e.stopPropagation();
    rpc.call("project.setTrackArmed", { trackIndex: idx, armed: !armed }).catch(console.error);
  };

  const handleMonitor = (idx: number, monitor: boolean, e: React.MouseEvent) => {
    e.stopPropagation();
    rpc.call("project.setTrackInputMonitor", { trackIndex: idx, monitor: !monitor }).catch(console.error);
  };

  const handleColorChange = (idx: number, e: React.MouseEvent) => {
    e.stopPropagation();
    const input = document.createElement("input");
    input.type = "color";
    input.value = colorStr(tracks[idx].color);
    input.addEventListener("input", () => {
      const hex = input.value.replace("#", "");
      const color = parseInt(hex, 16);
      rpc.call("project.setTrackColor", { trackIndex: idx, color }).catch(console.error);
    });
    input.click();
  };

  const handleHideToggle = (idx: number, e: React.MouseEvent) => {
    e.stopPropagation();
    const currentlyHidden = tracks[idx].isHidden ?? false;
    rpc.call("project.setTrackHidden", { trackIndex: idx, hidden: !currentlyHidden }).catch(console.error);
  };

  const handleHeightDrag = (idx: number, startY: number, startH: number) => {
    const onMove = (me: MouseEvent) => {
      const delta = me.clientY - startY;
      const newH = Math.max(40, Math.min(200, startH + delta));
      rpc.call("project.setTrackHeight", { trackIndex: idx, height: newH });
    };
    const onUp = () => {
      window.removeEventListener("mousemove", onMove);
      window.removeEventListener("mouseup", onUp);
    };
    window.addEventListener("mousemove", onMove);
    window.addEventListener("mouseup", onUp);
  };

  const formatPan = (pan: number): string => {
    if (pan === 0) return "C";
    const pct = Math.round(Math.abs(pan) * 100);
    return pan < 0 ? `L${pct}` : `R${pct}`;
  };

  return (
    <div className="th-root">
      {/* Corner + ruler band reproduce the timeline's toolbar + ruler so the
          rows below start at the exact same Y as the timeline lanes. */}
      <div className="th-corner" style={{ height: TOOLBAR_HEIGHT }} />
      <div className="th-ruler-row" style={{ height: RULER_HEIGHT }}>
        <span className="th-title">Tracks</span>
      </div>
      <div className="th-scroll" ref={scrollRef}
        onContextMenu={(e) => {
          if ((e.target as HTMLElement).closest(".th-row")) return;
          e.preventDefault();
          e.stopPropagation();
          setHeaderMenu({ x: e.clientX, y: e.clientY, trackIndex: -1 });
        }}
      >
        {tracks.length === 0 && (
          <div className="th-empty">
            <span>No tracks loaded</span>
            <AddTrackMenu label="+ Add Track" triggerClassName="th-empty-btn" />
          </div>
        )}
        {visibleTracks.map((track, idx) => {
          const meter = trackMeters[track.index] ?? { l: 0, r: 0 };
          return (
          <div
            key={track.index}
            className={`th-row th-row--${TRACK_TYPE_CLASSES[track.trackType] ?? "audio"}${track.isHidden ? " th-row--hidden" : ""}`}
            style={{ height: layout.heights[idx], paddingLeft: track.parentId != null && track.parentId >= 0 ? 20 : 0 }}
            onClick={() => selectClip(null, track.index)}
            onContextMenu={(e) => {
              e.preventDefault();
              e.stopPropagation();
              setHeaderMenu({ x: e.clientX, y: e.clientY, trackIndex: track.index });
            }}
          >
          {track.trackType === 2 && (
            <span
              className="th-chevron"
              onClick={(e) => {
                e.stopPropagation();
                rpc.call("project.setTrackCollapsed", { trackIndex: track.index, collapsed: !track.isCollapsed }).catch(console.error);
              }}
              title={track.isCollapsed ? "Expand folder" : "Collapse folder"}
            >
              {track.isCollapsed ? "\u25B6" : "\u25BC"}
            </span>
          )}
          <div
            className="th-type-badge"
            style={{ background: TRACK_TYPE_COLORS[track.trackType] ?? TRACK_TYPE_COLORS[0] }}
            title={TRACK_TYPE_LABELS[track.trackType] ?? "AUDIO"}
          >
            {TRACK_TYPE_ICONS[track.trackType] ?? TRACK_TYPE_ICONS[0]}
          </div>
          <div
            className="th-color"
            style={{ background: colorStr(track.color), cursor: "pointer" }}
            onClick={(e) => handleColorChange(track.index, e)}
            title="Click to change track color"
          />
          <button
            className={`th-btn th-hide${track.isHidden ? " active" : ""}`}
            onClick={(e) => handleHideToggle(track.index, e)}
            title={track.isHidden ? "Show track" : "Hide track"}
          >
            {track.isHidden ? "\u25CB" : "\u25CF"}
          </button>
          <div className="th-info">
            <div className="th-name">{track.name}</div>
            <div className="th-type" style={{ color: TRACK_TYPE_COLORS[track.trackType] ?? TRACK_TYPE_COLORS[0] }}>
              {TRACK_TYPE_LABELS[track.trackType] ?? "AUDIO"}
            </div>
          </div>
          <div className="th-controls">
            <button
              className={`th-btn th-mute${track.muted || track.effectiveMuted ? " active" : ""}`}
              onClick={(e) => handleMute(track.index, track.muted, e)}
              title="Mute"
            >
              M
            </button>
            <button
              className={`th-btn th-solo${track.soloed || track.effectiveSoloed ? " active" : ""}`}
              onClick={(e) => handleSolo(track.index, track.soloed, e)}
              title="Solo"
            >
              S
            </button>
            <button
              className={`th-btn th-expand${expandedTracks.has(track.index) ? " th-expand--active" : ""}`}
              onClick={(e) => {
                e.stopPropagation();
                setExpandedTracks((prev) => {
                  const next = new Set(prev);
                  if (next.has(track.index)) next.delete(track.index);
                  else next.add(track.index);
                  return next;
                });
              }}
              title={expandedTracks.has(track.index) ? "Hide advanced" : "Show advanced"}
            >
              ⋯
            </button>
            {expandedTracks.has(track.index) && (
              <>
                <button
                  className={`th-btn th-arm${track.armed ? " active" : ""}`}
                  onClick={(e) => handleArm(track.index, track.armed, e)}
                  title="Arm"
                >
                  R
                </button>
                <button
                  className={`th-btn th-monitor${track.inputMonitor ? " active" : ""}`}
                  onClick={(e) => handleMonitor(track.index, track.inputMonitor, e)}
                  title="Input Monitor"
                >
                  In
                </button>
              </>
            )}
          </div>
          {expandedTracks.has(track.index) && (
            <div className="th-values">
              <span className="th-vol">V:{Math.round(track.volume * 100)}%</span>
              <span className="th-pan">{formatPan(track.pan)}</span>
              {editingMidiCh === track.index ? (
                <input
                  type="number"
                  min={1}
                  max={16}
                  defaultValue={track.midiChannel + 1}
                  className="th-midi-ch-input"
                  autoFocus
                  onBlur={(e) => {
                    const num = parseInt(e.target.value, 10);
                    if (num >= 1 && num <= 16) {
                      rpc.call("project.setTrackMidiChannel", { trackIndex: track.index, channel: num - 1 });
                    }
                    setEditingMidiCh(null);
                  }}
                  onKeyDown={(e) => {
                    if (e.key === "Enter") (e.target as HTMLInputElement).blur();
                    if (e.key === "Escape") setEditingMidiCh(null);
                  }}
                  onClick={(e) => e.stopPropagation()}
                />
              ) : (
                <span
                  className="th-midi-ch"
                  title="MIDI Channel (click to edit)"
                  onClick={(e) => {
                    e.stopPropagation();
                    setEditingMidiCh(track.index);
                  }}
                >
                  Ch{track.midiChannel + 1}
                </span>
              )}
            </div>
          )}
          <div className="th-meters">
            <MeterBar value={meter.l} />
            <MeterBar value={meter.r} />
          </div>
          <div
            className="th-resize-handle"
            onMouseDown={(e) => {
              e.preventDefault();
              e.stopPropagation();
              handleHeightDrag(track.index, e.clientY, track.height);
            }}
          />
        </div>
        );
      })}
      </div>
      {headerMenu && (() => {
        const isEmptyArea = headerMenu.trackIndex === -1;
        const menuTrack = isEmptyArea ? null : tracks.find((t) => t.index === headerMenu.trackIndex);
        const menuType = menuTrack?.trackType ?? 0;
        return (
        <div
          className="clip-context-menu"
          style={{ left: headerMenu.x, top: headerMenu.y }}
          onMouseDown={(e) => e.stopPropagation()}
        >
          <button onMouseDown={(e) => {
            e.stopPropagation();
            rpc.call("project.addTrack", { trackType: 0 }).catch(() => {});
            setHeaderMenu(null);
          }}>
            Add Audio Track
          </button>
          <button onMouseDown={(e) => {
            e.stopPropagation();
            rpc.call("project.addTrack", { trackType: 1 }).catch(() => {});
            setHeaderMenu(null);
          }}>
            Add MIDI Track
          </button>
          {!isEmptyArea && (
            <>
              <button onMouseDown={(e) => {
                e.stopPropagation();
                rpc.call("project.duplicateTrack", { trackIndex: headerMenu.trackIndex }).catch(() => {});
                setHeaderMenu(null);
              }}>
                Duplicate Track
              </button>
              <div className="ctx-separator" />
              <button
                className={menuType === 0 ? "ctx-checked" : ""}
                onMouseDown={(e) => {
                  e.stopPropagation();
                  rpc.call("project.setTrackType", { trackIndex: headerMenu.trackIndex, trackType: 0 }).catch(() => {});
                  setHeaderMenu(null);
                }}
              >
                {menuType === 0 ? "✓ " : ""}Set Type: Audio
              </button>
              <button
                className={menuType === 1 ? "ctx-checked" : ""}
                onMouseDown={(e) => {
                  e.stopPropagation();
                  rpc.call("project.setTrackType", { trackIndex: headerMenu.trackIndex, trackType: 1 }).catch(() => {});
                  setHeaderMenu(null);
                }}
              >
                {menuType === 1 ? "✓ " : ""}Set Type: MIDI
              </button>
              <div className="ctx-separator" />
              <button className="ctx-danger" onMouseDown={(e) => {
                e.stopPropagation();
                rpc.call("project.removeTrack", { trackIndex: headerMenu.trackIndex }).catch(() => {});
                setHeaderMenu(null);
              }}>
                Delete Track
              </button>
            </>
          )}
        </div>
        );
      })()}
    </div>
  );
}

function MeterBar({ value }: { value: number }) {
  const pct = Math.min(100, Math.max(0, value * 100));
  let cls = "meter-fill";
  if (pct > 90) cls += " meter-clip";
  else if (pct > 75) cls += " meter-hot";
  return (
    <div className="meter-track">
      <div className={cls} style={{ height: `${pct}%` }} />
    </div>
  );
}
