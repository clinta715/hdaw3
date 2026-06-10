# AGENTS.md

Project-specific lessons learned. Read this before working on the timeline,
the project model, or the main window — these are the pitfalls that cost
real debugging time.

**Current scope**: HDAW is a Qt 6 + JUCE 8 desktop DAW at version
**0.2.1**. The core engine (project model, transport, routing,
JUCE plugin hosting, internal FX) and the basic UI shell
(track headers, timeline, mixer, piano roll, FX chain,
automation) work end-to-end. The project is pre-1.0, pre-test-suite,
and pre-per-clip-audio-editor. For the full list of working
features and the priority-ordered roadmap, see `README.md`.

## Build

- Configuration: `cmake --build build --config Debug`
- Output: `build/Debug/HDAW.exe`
- Do NOT run `build/Release/HDAW.exe` — it is a stale binary from before
  the bug-fix series began and contains none of the fixes.
- Logging: `HDAW_LOG` (or the older `DBG` macro is **not** available —
  JUCE defines its own `DBG` and shadows it). All paths to `HDAW_LOG`
  must `#include "DebugLog.h"`. Output is appended to `%TEMP%/hdaw_debug.log`.

## QGraphicsView initial vertical scroll position — the silent show-stealer

**Symptom**: At startup, the main window appears with the three default
tracks laid out, but **none of the track rows are visible** in the
track-header area on the left. The headers are present (the right size,
the right width) but the actual painted track rectangles are scrolled
above the visible clip-rect, so the user sees a blank band.

After clicking "Add Track", the layout reflows and the tracks suddenly
become visible. The user perceives this as "tracks are missing at
startup" and assumes a data path bug. **It is not a data path bug.
It is a scroll-position bug.**

**Root cause**: `QGraphicsView` computes its vertical scroll-bar value
during the first layout pass, *after* `setupUI` returns. Setting
`verticalScrollBar()->setValue(0)` in `setupUI` is a no-op: the
viewport has not been sized yet, the scroll-bar range is `0..0`, and
whatever value `setupUI` sets is overwritten when the layout pass
finishes. With a 4000×2000 sceneRect and a viewport that lands at
~535 px tall after layout, the value lands at roughly
`(2000 - 535) / 2 ≈ 732`, not at 0.

That scroll value propagates through `TimelineView::syncRulerWithScene`
into `TrackHeaderWidget::setScrollOffset`. The track header widget's
`paintEvent` then computes `trackY = rulerHeight - scrollOffset` and
paints every track at negative y. The widget's clip-rect starts at
y = 30, so all painted track rows are above the clip and nothing is
visible.

**The fix** lives in `src/ui/TimelineView.cpp` — override
`QWidget::showEvent` and reset both scroll bars there, after the
layout has fully resolved. Also keep `syncRulerWithScene()` clamped
to a valid range as a defensive measure:

```cpp
void TimelineView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (graphicsView != nullptr)
    {
        graphicsView->verticalScrollBar()->setValue(0);
        graphicsView->horizontalScrollBar()->setValue(0);
        syncRulerWithScene();
    }
}

void TimelineView::syncRulerWithScene()
{
    double sceneHeight = timelineScene->sceneRect().height();
    double viewportHeight = graphicsView->viewport()->height();
    double maxScrollY = std::max(0.0, sceneHeight - viewportHeight);
    double scrollY = std::clamp(
        static_cast<double>(graphicsView->verticalScrollBar()->value()),
        0.0, maxScrollY);
    // ... rest of sync, including trackHeaders->setScrollOffset(scrollY)
}
```

**Diagnostic signature** (if the symptom recurs): the
`TrackHeaderWidget::paintEvent` will log `scrollOffset` values like
`~737` (not 0), and the per-track y ranges will be negative
(`y=[-707,-627) h=80`). That confirms this pitfall, not a different
one.

**Why this is easy to re-introduce**: the symptom is identical to a
"data is missing" bug. Future contributors will see "no tracks
visible at startup" and start hunting through `createDefaultProject`,
clip creation, and the scene rebuild. None of that is the cause. The
cause is the *timing* of when the QGraphicsView commits its scroll
position, which is the last thing any reasonable code review would
check.

## Track headers need a `sizeHint` override — the layout will not infer it

`QWidget::sizeHint` returns a useless default (~100 px tall) for
custom widgets. If a custom widget lives in a `QHBoxLayout` next to
a `QGraphicsView` and relies on the row height to fit its content, it
**must** override `sizeHint()`. Otherwise the row collapses to 100 px
and most of the widget's content is clipped.

For `TrackHeaderWidget`, the override sums the ruler height, all
track heights, and a small margin:

```cpp
QSize TrackHeaderWidget::sizeHint() const
{
    auto trackList = engine.getProjectModel().getTrackListTree();
    int count = trackList.getNumChildren();
    double totalH = rulerHeight;
    for (int i = 0; i < count; ++i)
        totalH += getTrackHeight(i);
    int hintH = static_cast<int>(totalH) + 20;
    int hintW = static_cast<int>(headerWidth);
    return QSize(hintW, std::max(hintH, minimumHeight()));
}
```

`minimumSizeHint()` should be overridden too with the fixed header
width and the existing `minimumHeight()` floor.

## `setAlignment(Qt::AlignTop | Qt::AlignLeft)` is required on the timeline QGraphicsView

`QGraphicsView`'s default alignment is `Qt::AlignCenter`. When the
scene is smaller than the viewport (which is the common case for a
DAW timeline with a 290-tall content area in a 535-tall row), the
scene is centered. The headers share the same row at y=0; the
centered scene is offset by half the empty space. The user sees
"track info on the left" at the top of the row and "track data on
the right" shifted down — the misalignment is the alignment default,
not a layout bug.

Set it in `setupUI`, right after constructing the QGraphicsView:

```cpp
graphicsView->setAlignment(Qt::AlignTop | Qt::AlignLeft);
```

## Default project should not reference non-existent sample files

`ProjectModel::createDefaultProject` historically created audio clips
on Track 1 and Vocals with `sourceFile` set to `samples/bass.wav`,
`samples/drums.wav`, and `samples/vocals.wav`. None of these files
ship with the project. The clips would silently render a 10% white
tint (`AudioClipItem::paintContent` fallback) and the user would see
"empty audio clips" with no indication that the data was missing.

Audio tracks should be created with an **empty `CLIP_LIST`**. Users
populate them by drag-dropping real audio files. Do not add
fake/sample audio clips back to the default project without
also shipping the actual sample files.

## Scene-mouse event routing — the installEventFilter trap

`QGraphicsScene::installEventFilter` does **not** receive scene mouse
events. Mouse events from the `QGraphicsView` are dispatched
directly to the hit-tested `QGraphicsItem`, never to the scene
QObject's event filter. If you want to intercept scene mouse events,
override `QGraphicsScene::mousePressEvent` / `mouseMoveEvent` /
`mouseReleaseEvent` / `mouseDoubleClickEvent` directly.

This was the root cause of "can't create or edit clips in the
timeline" in earlier sessions. The interaction code in
`TimelineInteraction` was correctly written but was being installed
on the wrong object.

## `ClipItem` must not have `ItemIsSelectable`

`QGraphicsItem::mousePressEvent`'s default implementation calls
`event->accept()` for any item with `ItemIsSelectable`. That
terminates the scene-mouse-event dispatch before the interaction can
process it, so trim/move/fade drags never start.

Set only `ItemSendsGeometryChanges`. Drive selection explicitly from
the interaction via `clip->setSelected(true)`. The `ClipItem::paint`
method already paints a custom selection outline, so visuals are
preserved.

## `DBG` macro collides with JUCE — use `HDAW_LOG`, do not redefine

JUCE defines `DBG(textToWrite)` as a single-argument macro in
`juce_PlatformDefs.h` (used in 100+ places across the project).
Trying to `#define DBG(tag, msg)` to add a two-argument debug
log is wrong on two counts:

1. **Redefinition warning** — the compiler emits `C4005: 'DBG':
   macro redefinition` because JUCE's version is already in scope
   from any TU that includes a JUCE header.
2. **Signature mismatch** — the 8 existing `DBG("TSCtor", ...)`
   call sites in `TimelineScene.cpp` and `MainWindow.cpp` pass
   two arguments (tag + message). JUCE's `DBG` takes one
   argument. Either the build fails outright or the calls bind
   to the wrong macro and silently produce garbage.

The project's own logging facility is `HDAW_LOG(tag, msg)`, defined
in `src/ui/DebugLog.h`. It writes NDJSON to
`%TEMP%/hdaw_debug.log`. All TUs that call it must
`#include "DebugLog.h"`.

**Rule**: never use the bare `DBG` identifier in this project. If
you see `DBG(...)` in source, rename it to `HDAW_LOG(...)`. If you
add a new logging macro, pick a name that does not collide with
JUCE — `HDAW_LOG`, `LOG_INFO`, `AppLog`, anything but `DBG`.

## `paintEvent` clip-rect must include the ruler offset — `TrackHeaderWidget`

`TrackHeaderWidget::paintEvent` clips its drawing to a region that
*excludes* the ruler area:

```cpp
painter.setClipRect(0, static_cast<int>(rulerHeight),
                    w, height() - static_cast<int>(rulerHeight));
```

If the widget's actual height is smaller than
`rulerHeight + oneTrackHeight` (i.e. less than 110 px), the clip
rect's height becomes negative or zero, and the visible track rows
collapse to nothing. The early-history symptom was that the widget
was sized to 100 px (its `minimumHeight()` floor) and the user saw
"track header is empty."

The fix is twofold:

1. **Override `sizeHint()`** so the layout allocates enough vertical
   space for the ruler plus all track rows. The "Track headers
   need a `sizeHint` override" section above documents the
   override.
2. **Keep the clip-rect at the top of `paintEvent`** — the ruler
   area is painted elsewhere (or not at all, in current code) but
   the clip is what hides the per-track backgrounds below the
   ruler.

If you change the `rulerHeight` constant in `TrackHeaderWidget.h`,
make sure both `sizeHint` and the clip-rect use the same value.
The two are intentionally coupled and live in the same header
file as `static constexpr double rulerHeight = 30.0;`.

## `TimelineView` setupUI / connectSignals ordering — null-deref trap

`MainWindow::setupLayout` calls
`connect(bottomStack, &QStackedWidget::currentChanged, ...)`
to keep the tab-button checked-state in sync with the stack.
If you write that `connect` *before* `bottomStack = new
QStackedWidget(...)`, you dereference a null pointer and the
`MainWindow` ctor crashes during startup with no useful error
message. The crash happens *after* the scene ctor logs
`[TSCtor] sceneRect=...` and *before* any UI event, which looks
like a Qt init failure if you don't read the log carefully.

**Rule for `MainWindow::setupLayout`**: create the `QStackedWidget`
first, populate it with all four child widgets (Mixer, Piano Roll,
FX Chain, Automation), *then* connect `currentChanged` to the
button-group sync lambda, *then* call `setCurrentIndex(0)`. The
order is:

```cpp
bottomStack = new QStackedWidget(bottomContainer);
bottomStack->addWidget(mixerWidget);
bottomStack->addWidget(pianoRollWidget);
bottomStack->addWidget(fxChainWidget);
bottomStack->addWidget(automationWidget);

// connect currentChanged HERE, after bottomStack exists
connect(bottomStack, &QStackedWidget::currentChanged, ...);
// initial setChecked, then setCurrentIndex(0)
bottomStack->setCurrentIndex(0);
```

If you see a startup crash that produces no error dialog and no
console output, check that any new `connect(...)` call does not
reference a member variable that hasn't been `new`'d yet.

## Piano-roll pitfalls — `MIDI_NOTE_LIST`, vertical scroll, note culling

`PianoRollWidget` and `NoteGridWidget` have three traps that all
look like "I can't edit MIDI" from the user's side:

1. **MIDI clips must carry a `MIDI_NOTE_LIST` child.** If a clip
   is missing the note container, `PianoRollModel::addNote`
   returns an empty `ValueTree` and clicks silently no-op. The
   defensive fix in `PianoRollModel::setClipTree` is to create
   the container if it's missing:

   ```cpp
   noteList = clip.getChildWithName(IDs::MIDI_NOTE_LIST);
   if (!noteList.isValid() && clip.isValid()) {
       noteList = juce::ValueTree(IDs::MIDI_NOTE_LIST);
       clip.addChild(noteList, -1, nullptr);  // no undo manager
   }
   ```

2. **Default `scrollY` lands above middle C.** With
   `keyHeight = 10` and the default 6 notes in the project's
   `createMidiClip` (notes 60, 64, 67, 72, 71, 69), a scrollY of
   0 places note 95 at the top of the grid — middle C (note 60)
   is at y=350 and completely off-screen. Use
   `NoteGridWidget::defaultScrollYForMiddleC()` (= 350) as the
   initial `scrollY` in `PianoRollWidget::loadClip`. Also clamp
   `setScrollOffset` to `[0, 128 * keyHeight - height()]` so the
   user cannot scroll past the end.

3. **The vertical scrollbar overlays the note grid.** Adding a
   `QScrollBar` to the same `QHBoxLayout` as the note grid
   consumes the rightmost 17 px of the grid. Use a `QGridLayout`
   with three columns (keys | noteGrid | vScrollBar) so the
   scrollbar has its own column and never overlaps the grid.

4. **The `paintEvent` cull is horizontal only.** The current
   cull in `NoteGridWidget::paintEvent` skips notes whose rect
   lies entirely outside the viewport horizontally but does
   NOT cull vertically. If a future feature moves notes far
   from y=0, add a vertical cull (`r.bottom() < 0 || r.top() >
   h`) to avoid the painter being asked to draw off-screen
   rects.

## Tab buttons in `QStackedWidget` need a `QButtonGroup` + `currentChanged` bridge

`QStackedWidget::setCurrentIndex(N)` does not emit a `clicked`
signal on any button. If your tab buttons are `checkable: true`
and you only connect their `clicked` to `setCurrentIndex(N)`,
then programmatic `setCurrentIndex(M)` (e.g. from a
`clipSelected` signal) leaves the button checked-state
unchanged. The user sees the bottom panel change but the tab
button stays on the old tab — it looks like nothing happened.

The fix:

- Put all tab buttons in a `QButtonGroup` with
  `setExclusive(true)`.
- Connect `bottomStack->currentChanged` to a lambda that
  iterates the buttons and sets `setChecked(i == index)`.
- Also do an explicit `setChecked` on the initial
  `currentIndex` after connecting — `setCurrentIndex(0)` won't
  fire `currentChanged` if the stack's current index is already
  0.

The `MainWindow::tabGroup`, `MainWindow::tabButtons` members and
the `makeTab` lambda are the working pattern. Don't restructure
them; copy them when adding new tabs.

## Forward-declare to break circular includes between `TimelineScene` and `TimelineInteraction`

`TimelineScene.h` and `TimelineInteraction.h` had a circular
include. `TimelineScene` needs to call into
`TimelineInteraction`'s handlers (or hold a pointer to it for
delegation); `TimelineInteraction` needs to know about
`TimelineScene` (its `scene` member, plus a forward `TimelineScene*`
in member functions). Including both headers from each other
produces incomplete-type errors when the compiler reaches the
`TimelineScene::mousePressEvent(QGraphicsSceneMouseEvent*)`
override in the `.cpp` and the `ClipItem` member
`dragItem` is referenced from `TimelineInteraction.h`.

**The fix** in `TimelineInteraction.h`:

```cpp
#pragma once
#include <QObject>
#include <QGraphicsSceneMouseEvent>
#include <juce_data_structures/juce_data_structures.h>

class TimelineScene;
class ClipItem;
class AudioEngine;
```

Three forward declarations, no header includes for those types.
The `TimelineInteraction.cpp` then `#include "TimelineScene.h"`
and `#include "ClipItem.h"` to get the full definitions before
calling methods on them. This pattern is needed any time two
headers mutually reference each other — the right move is to
forward-declare in the header and include in the cpp.

Also: never make the four scene-mouse-handler methods
(`handleMousePress`, `handleMouseMove`, `handleMouseRelease`,
`handleMouseDoubleClick`) `private` on `TimelineInteraction` if
`TimelineScene` needs to call them. They must be `public` (or
`TimelineScene` must be a `friend class`). The access check
fails at compile time with `C2248: cannot access private
member` and the error message doesn't make the cause obvious
because the method *names* are public-looking.

## Build pipeline: MOC, autogen, stale PDB, parallel-link

The project uses Qt 6 with `qt_standard_project_setup()` which
enables `CMAKE_AUTOMOC` automatically. MOC processes any header
that contains `Q_OBJECT`. A few things to know:

- **Stale PDB on parallel builds**. The first time
  `cmake --build build --config Debug` is invoked after a large
  edit, MSBuild's parallel-link may fail with
  `C1041: cannot open program database 'vc145.pdb'; if multiple
  CL.EXE write to the same .PDB file, please use /FS`. The fix
  is to kill any orphaned `cl.exe` and `Tracker.exe` processes
  left over from a previous aborted build, then re-run. The
  command:

  ```powershell
  Get-Process cl, Tracker, MSBuild -ErrorAction SilentlyContinue |
      Stop-Process -Force
  cmake --build build --config Debug
  ```

- **Header-only edits are not always detected.** The build
  system uses header mtime to decide what to recompile. If you
  change a `.h` and the build does not pick it up (you see
  unchanged behaviour despite a clear source diff), force a
  recompile by touching the corresponding `.cpp` or by deleting
  the relevant `.obj` files in `build/HDAW.dir/Debug/`.

- **The Release binary is stale.** If the user reports
  "nothing changed visually," check whether they are running
  `build\Debug\HDAW.exe` (29 MB) or `build\Release\HDAW.exe`
  (5 MB). The Release one was built before the bug-fix series
  and is intentionally not maintained. Always run the Debug
  binary.

- **Sources must be added to `add_executable` in `CMakeLists.txt`.**
  Adding a new `.cpp` file without listing it in the CMake
  source list will not produce a build error — the file just
  will not be compiled. Always check the source list when
  adding a new translation unit.

## Diagnostic pattern when something is mysteriously wrong

When a fix doesn't take effect despite clear evidence the binary
contains it, add `HDAW_LOG` calls at the *exact* points where the
problem-state is decided. The pattern is:

1. One log at construction / data-setup time, showing the input state.
2. One log in the rendering or event-handling path, showing what the
   widget actually sees at runtime.
3. Cross-reference the two. If the data is correct but the
   rendering is wrong, the bug is in the rendering path. If the
   data is wrong, trace upstream.

This is what surfaced the QGraphicsView scroll-position bug. A
similar `paintEvent` log on `TrackHeaderWidget` revealed that
`scrollOffset=737` at first paint, immediately pointing at the
QGraphicsView's internal layout-time scroll commit.

**The log file is the first place to look, not the source code.**
When a user reports "nothing changed visually" or "X is broken,"
read `%TEMP%/hdaw_debug.log` directly. The `TSCtor`,
`TSRebuild START/DONE`, `MWAddTrk`, `TIEvt`, `TIPress`,
`TIDblClk` lines (pre-existing) give a complete picture of what
the app did during startup and on each user interaction. If a
fix is supposed to take effect and the log doesn't show the
expected state, the fix isn't running — probably a stale
binary (see "Build pipeline" above) or a missed compile of the
edited file.

## Out-of-scope: known gaps deferred to future work

- **No per-clip audio editor**. Double-clicking an audio clip
  currently routes the user to the global Mixer panel, not to a
  per-clip waveform/properties editor. Adding one is a
  ~200-400-line feature, not a bug fix.
