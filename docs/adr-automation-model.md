# ADR: Automation Model — Track-Based Primary

## Status

Accepted

## Context

HDAW stores automation **per track**, not per clip. Each TRACK node in the
project ValueTree owns an `AUTOMATION_LIST` child, created by
`ProjectModel::createTrackAutomationList` (`src/model/ProjectModel.cpp:48`).
That list is populated with three default lanes — Volume (`paramID` 1),
Pan (`paramID` 2), and Mute (`paramID` 3)
(`src/model/ProjectModel.cpp:51-53`) — and each lane is a child `AUTOMATION`
tree identified by its `paramID` (set in `createAutomationLane` at
`src/model/ProjectModel.cpp:37`). When a track is built, the list is attached
as a child of the TRACK tree (`AudioEngineCommands::createTrackValueTree`
calls `ProjectModel::createTrackAutomationList`), so automation is scoped to
the track, never to an individual clip.

Lane lookup is track-scoped for the same reason:
`AudioEngineCommands::findAutomationLane`
(`src/engine/AudioEngineCommands.cpp:116`) resolves the track by index, reads
that track's `AUTOMATION_LIST` child (`src/engine/AudioEngineCommands.cpp:121`),
and searches its lane children. There is no clip-local automation storage
anywhere in the tree.

The ReadModel projects this shape to the frontend unchanged:
`ReadModel::getAutomationLanes(trackIndex)` (`src/common/ReadModel.h:188`)
returns a `std::vector<AutomationLaneSnapshot>`
(`src/common/ReadModel.h:107`) keyed by track index, with no clip association.
Automation is narrative and absolute on the arranger timeline — a lane's
points are positioned in track/time space, not relative to any clip.

## Decision

**Track-based automation is the primary (and, today, only) automation model.**
Clip-based / relative automation — clip-local lanes that move and loop with
the clip and layer on top of track automation — is **deferred** as a
documented future option, not a current capability.

Rationale:

- **It matches the existing ValueTree shape.** Lanes already live under each
  TRACK's `AUTOMATION_LIST`; there is no second storage location to reconcile.
- **It matches the ReadModel projection.** `getAutomationLanes` is keyed by
  track index, and `AutomationLaneSnapshot` carries no clip association.
- **It matches the delta-vs-fullSync model.** Lane/point edits are sub-clip
  detail and already take the fullSync path; a clip-local lane would add a new
  entity kind straddling the clip/track boundary with no delta representation.
- **Clip-local automation would require a second storage location plus a
  combine/precedence rule** (how a clip lane composes with the track lane for
  the same `paramID`), and it **interacts with the beats-vs-seconds boundary**:
  the frontend speaks beats while the engine stores seconds (see
  `docs/architecture.md` → "Time-unit convention"). Clip-relative points would
  need beats↔seconds conversion at clip start/offset boundaries on top of the
  existing timeline conversion. That coupling is why it is intentionally
  postponed rather than bolted on now.

## pid ranges (lane `paramID` address space)

Lane `paramID` is a track-wide address with three ranges (explicit since the
2026-09-02 pid-routing fix; previously implicit):

- `1` / `2` / `3` — volume / pan / mute.
- `100..999` — audio FX chain compound: `100 + slotIndex * 100 + paramIndex`.
- `1000..1999` — MIDI FX chain compound: `1000 + slotIndex * 100 + paramIndex`.

The engine decodes in `Track::processBlock` (automation record/apply, LFO
targets); `ReadModelImpl::getAutomatableParams` encodes. The 2026-08-06
MIDI-FX-modulation plan's original `200+` MIDI-FX range is **superseded** —
it collided with the audio compound at slot 1 (pid 200 = audio slot 1 param 0),
which made every audio-FX lane on slot >= 1 and every UI-created MIDI-FX lane
inert. `AutomationPanel`/`ModulationPanel` compose audio-FX pids as the
`100+` compound and pass MIDI-FX pids (which the snapshot carries as the full
`1000+` value) through unchanged.

## Consequences

- **Per-clip / per-note animation is NOT delivered by clip automation.** It is
  delivered by the per-event Operators and the five per-note expressions (plan
  Phase C — items 1, 11, 2), which operate on notes/events at trigger time.
  Track automation remains the mechanism for timeline-absolute parameter
  curves.
- **The precedence/combination of the five control sources** — {manual, MIDI,
  automation, remote, modulator} — **is defined in plan item B3** (modulation
  fan-out + 5-source precedence) and must be documented there, not in this ADR.
  This ADR only fixes the storage model.
- **Any future clip-based automation must specify two things before it is
  built:** (1) how it composes with track automation for the same `paramID`
  (the precedence/combine rule, coordinated with B3), and (2) how its
  clip-relative points convert beats↔seconds at clip boundaries (clip
  start/offset), per the time-unit convention in `docs/architecture.md`.

## References

- `ProjectModel::createTrackAutomationList` — `src/model/ProjectModel.cpp:48`
  (default Volume/Pan/Mute lanes at `:51-53`; lane `paramID` set at `:37`).
- `AudioEngineCommands::findAutomationLane` — `src/engine/AudioEngineCommands.cpp:116`
  (track-scoped lookup via the track's `AUTOMATION_LIST` child at `:121`).
- `ReadModel::getAutomationLanes` — `src/common/ReadModel.h:188`.
- `AutomationLaneSnapshot` — `src/common/ReadModel.h:107`.
- Master plan — `.zcode/plans/bitwig-checklist-goal.md`, items 8 (automation
  joker / MIDI lanes), 12 (modulation fan-out + 5-source precedence), and 14
  (this clip-vs-track automation decision); see also section A5.
- Time-unit convention (beats vs seconds) — `docs/architecture.md`.
