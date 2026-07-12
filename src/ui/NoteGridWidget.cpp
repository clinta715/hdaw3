#include "NoteGridWidget.h"
#include "../engine/AudioEngine.h"
#include "Theme.h"
#include "../engine/PhraseGenerator.h"
#include "../model/ProjectModel.h"
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QApplication>
#include <cmath>

NoteGridWidget::NoteGridWidget(PianoRollModel& m, AudioEngine& ae, QWidget* parent)
    : QWidget(parent), model(m), engine(ae)
{
    projectCmds = &engine.getProjectCommands();
    transportCmds = &engine.getTransportCommands();
    audioGraphCmds = &engine.getAudioGraphCommands();
    readModel = &engine.getReadModel();
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

void NoteGridWidget::setPixelsPerBeat(double ppb)
{
    pixelsPerBeat = (std::max)(10.0, (std::min)(200.0, ppb));
    update();
}

void NoteGridWidget::setScrollOffset(int x, int y)
{
    // 128 keys * keyHeight defines the vertical extent. Clamp scrollY to it so
    // the user can't scroll past the end of the piano range.
    int maxScrollY = (std::max)(0, static_cast<int>(128 * keyHeight) - height());
    scrollX = (std::max)(0, x);
    scrollY = std::clamp(y, 0, maxScrollY);
    update();
}

int NoteGridWidget::noteNumberAtPos(int y) const
{
    // Return -1 for clicks that fall outside the MIDI range (above the top
    // of the grid or below the bottom). Callers that create notes on click
    // must check for -1, otherwise stray notes at MIDI 0/127 appear when
    // the user clicks in the ruler spacer or below the grid.
    int idx = (y + scrollY) / static_cast<int>(keyHeight);
    int note = 96 - 1 - idx;
    if (note < 0 || note > 127) return -1;
    return note;
}

int NoteGridWidget::noteIndexAtPos(const QPoint& pos) const
{
    for (int i = model.getNumNotes() - 1; i >= 0; --i)
    {
        auto nr = noteRect(i);
        if (nr.contains(pos))
            return i;
    }
    return -1;
}

QRectF NoteGridWidget::noteRect(int noteIndex) const
{
    auto note = model.getNote(noteIndex);
    if (!note.isValid()) return {};

    int noteNum = note.getProperty(IDs::noteNumber);
    double startBeat = note.getProperty(IDs::startBeat);
    double durBeats = note.getProperty(IDs::durationBeats);

    double x = startBeat * pixelsPerBeat - scrollX;
    double w = (std::max)(durBeats * pixelsPerBeat, 3.0);
    int idx = 96 - 1 - noteNum;
    double y = idx * keyHeight - scrollY;

    return QRectF(x, y, w, keyHeight);
}

void NoteGridWidget::setSnapEnabled(bool enabled)
{
    snapEnabled = enabled;
    update();
}

void NoteGridWidget::setSnapDivision(double division)
{
    snapDivision = division;
    update();
}

double NoteGridWidget::snapToGrid(double beat) const
{
    if (!snapEnabled || snapDivision <= 0.0)
        return beat;
    return std::max(0.0, std::round(beat / snapDivision) * snapDivision);
}

void NoteGridWidget::createNoteAtPos(const QPoint& pos, float velocity, double durationBeats)
{
    int noteNum = noteNumberAtPos(pos.y());
    if (noteNum < 0) return; // click outside the MIDI range — no note
    double beat = (pos.x() + scrollX) / pixelsPerBeat;
    beat = (std::max)(0.0, beat);
    if (snapEnabled)
        beat = snapToGrid(beat);
    model.addNote(noteNum, velocity, beat, durationBeats);
    lastNoteDuration = durationBeats;
    emit notesChanged();
    update();
}

void NoteGridWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int w = width();
    int h = height();

    // Background
    painter.fillRect(rect(), ThemeColors::bgWindow());

    // Scale highlighting
    {
        int scaleRoot = readModel->getScaleRoot();
        int scaleMode = readModel->getScaleMode();
        if (scaleRoot != cachedScaleRoot || scaleMode != cachedScaleMode)
        {
            cachedScaleRoot = scaleRoot;
            cachedScaleMode = scaleMode;
            inScale.reset();
            auto scalePitches = PhraseGenerator::buildScalePitches(scaleRoot, scaleMode, 0, 127);
            for (int p : scalePitches)
                if (p >= 0 && p < 128)
                    inScale.set(static_cast<size_t>(p));
        }

        QColor scaleColor(ThemeColors::accent().red(), ThemeColors::accent().green(),
                          ThemeColors::accent().blue(), 12);
        for (int n = 0; n < 128; ++n)
        {
            if (!inScale.test(n)) continue;
            int y = static_cast<int>(n * keyHeight - scrollY);
            if (y > h + 10 || y + keyHeight < -10) continue;
            painter.fillRect(0, static_cast<int>(y), w, static_cast<int>(keyHeight + 1), scaleColor);
        }
    }

    // Grid lines (octave colors)
    painter.setPen(QPen(ThemeColors::gridLineSub(), 1));
    for (int n = 0; n < 128; ++n)
    {
        int y = static_cast<int>(n * keyHeight - scrollY);
        if (y < -10 || y > h + 10) continue;
        bool isCOctave = (n % 12 == 0);
        if (isCOctave)
        {
            painter.setPen(QPen(ThemeColors::gridLineBeat(), 1));
            painter.drawLine(0, y, w, y);
        }
        else if (n % 12 == 4 || n % 12 == 7)
        {
            painter.setPen(QPen(ThemeColors::gridLineSub(), 1));
            painter.drawLine(0, y, w, y);
        }
    }

    // Beat lines
    double totalBeats = static_cast<double>(w + scrollX) / pixelsPerBeat + 1;
    for (int b = 0; b <= static_cast<int>(totalBeats); ++b)
    {
        int x = static_cast<int>(b * pixelsPerBeat - scrollX);
        if (x < -5 || x > w + 5) continue;
        bool isBar = (b % 4 == 0);
        painter.setPen(QPen(isBar ? ThemeColors::gridLineBar() : ThemeColors::gridLineBeat(), isBar ? 2 : 1));
        painter.drawLine(x, 0, x, h);
    }

    // Snap grid lines (faint vertical lines at snap division boundaries)
    if (snapEnabled && snapDivision > 0.0)
    {
        painter.setPen(QPen(ThemeColors::gridLineSub(), 1));
        double snappedBeats = static_cast<double>(w + scrollX) / pixelsPerBeat + 1;
        for (int si = 0; si <= static_cast<int>(snappedBeats / snapDivision); ++si)
        {
            int x = static_cast<int>(si * snapDivision * pixelsPerBeat - scrollX);
            if (x < 0 || x > w) continue;
            painter.drawLine(x, 0, x, h);
        }
    }

    // Draw notes
    for (int i = 0; i < model.getNumNotes(); ++i)
    {
        auto note = model.getNote(i);
        auto r = noteRect(i);
        if (r.right() < 0 || r.left() > w || r.bottom() < 0 || r.top() > h) continue;

        float vel = note.getProperty(IDs::velocity);
        int alpha = static_cast<int>(vel / 127.0f * 200.0f + 55.0f);
        alpha = (std::min)(255, alpha);

        QColor noteColor(ThemeColors::accent().red(), ThemeColors::accent().green(), ThemeColors::accent().blue(), alpha);

        // Note shadow
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 40));
        painter.drawRoundedRect(r.adjusted(0, 1, -1, 0), 2, 2);

        // Note body (gradient)
        QLinearGradient noteGrad(r.topLeft(), r.bottomLeft());
        noteGrad.setColorAt(0, noteColor.lighter(120));
        noteGrad.setColorAt(1, noteColor);
        painter.setPen(QPen(noteColor.darker(130), 1));
        painter.setBrush(noteGrad);
        painter.drawRoundedRect(r.adjusted(0, 0, -1, -1), 2, 2);

        // Selection highlight
        if (model.isSelected(i))
        {
            painter.setPen(QPen(QColor(0xe8, 0xe8, 0xec), 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(r.adjusted(1, 1, -2, -2), 2, 2);
        }
    }

    // Rubber-band selection rectangle
    if (dragMode == Select)
    {
        QRect selRect = QRect(dragStart, rubberBandEnd).normalized();
        if (selRect.width() > 3 || selRect.height() > 3)
        {
            painter.setPen(QPen(ThemeColors::accent(), 1, Qt::DashLine));
            painter.setBrush(QColor(ThemeColors::accent().red(), ThemeColors::accent().green(),
                                    ThemeColors::accent().blue(), 20));
            painter.drawRect(selRect);
        }
    }

    // Playhead line
    {
        auto transport = readModel->getTransport();
        if (transport.isPlaying)
        {
            double beat = transport.currentTimeSeconds * transport.bpm / 60.0;
            int phx = static_cast<int>(beat * pixelsPerBeat - scrollX);
            if (phx >= 0 && phx <= w)
            {
                painter.setPen(QPen(ThemeColors::accent(), 1));
                painter.drawLine(phx, 0, phx, h);
            }
        }
    }
}

void NoteGridWidget::mousePressEvent(QMouseEvent* event)
{
    setFocus();
    QPoint pos = event->pos();

    if (event->button() == Qt::LeftButton)
    {
        int idx = noteIndexAtPos(pos);
        if (idx >= 0)
        {
            engine.getProjectModel().getUndoManager().beginNewTransaction("Edit note");

            auto note = model.getNote(idx);
            double localX = pos.x() - (noteRect(idx).x());
            double noteW = noteRect(idx).width();
            double edgeThreshold = 5.0;

            if (localX < edgeThreshold)
            {
                dragMode = ResizeLeft;
            }
            else if (localX > noteW - edgeThreshold)
            {
                dragMode = ResizeRight;
            }
            else
            {
                dragMode = Move;
            }

            dragNoteIndex = idx;
            dragStart = pos;
            dragStartBeat = note.getProperty(IDs::startBeat);
            dragStartDuration = note.getProperty(IDs::durationBeats);
            dragStartNoteNumber = note.getProperty(IDs::noteNumber);

            if (!(event->modifiers() & Qt::ShiftModifier))
                model.deselectAll();
            model.selectNote(note, event->modifiers() & Qt::ShiftModifier);

            emit noteSelected(idx);
            update();
        }
        else
        {
            // Start rubber-band selection (or click-to-create on release)
            dragMode = Select;
            dragStart = pos;
            rubberBandEnd = pos;
        }
    }
    else if (event->button() == Qt::RightButton)
    {
        int idx = noteIndexAtPos(pos);
        if (idx >= 0)
        {
            model.removeSelectedNotes();
            update();
        }
    }
}

void NoteGridWidget::mouseMoveEvent(QMouseEvent* event)
{
    QPoint pos = event->pos();

    if (dragMode == Move && dragNoteIndex >= 0)
    {
        QPoint delta = pos - dragStart;
        double beatDelta = delta.x() / pixelsPerBeat;
        int noteDelta = -(delta.y() / static_cast<int>(keyHeight));

        auto note = model.getNote(dragNoteIndex);
        double newBeat = (std::max)(0.0, dragStartBeat + beatDelta);
        if (snapEnabled)
            newBeat = snapToGrid(newBeat);
        int newNoteNum = (std::max)(0, (std::min)(127, dragStartNoteNumber + noteDelta));

        note.setProperty(IDs::startBeat, newBeat, &engine.getProjectModel().getUndoManager());
        note.setProperty(IDs::noteNumber, newNoteNum, &engine.getProjectModel().getUndoManager());
        emit notesChanged();
        update();
    }
    else if (dragMode == ResizeRight && dragNoteIndex >= 0)
    {
        QPoint delta = pos - dragStart;
        double newDur = (std::max)(0.25, dragStartDuration + delta.x() / pixelsPerBeat);
        if (snapEnabled)
        {
            double endBeat = snapToGrid(dragStartBeat + newDur);
            if (endBeat <= dragStartBeat)
                endBeat = dragStartBeat + snapDivision;
            newDur = endBeat - dragStartBeat;
        }
        auto note = model.getNote(dragNoteIndex);
        note.setProperty(IDs::durationBeats, newDur, &engine.getProjectModel().getUndoManager());
        emit notesChanged();
        update();
    }
    else if (dragMode == Select)
    {
        rubberBandEnd = pos;
        update();
    }
    else if (dragMode == ResizeLeft && dragNoteIndex >= 0)
    {
        QPoint delta = pos - dragStart;
        double newStart = dragStartBeat + delta.x() / pixelsPerBeat;
        double newDur = dragStartDuration - delta.x() / pixelsPerBeat;
        if (snapEnabled)
        {
            double endBeat = dragStartBeat + dragStartDuration;
            newStart = snapToGrid(newStart);
            if (newStart >= endBeat)
                newStart = endBeat - snapDivision;
            newDur = endBeat - newStart;
        }
        if (newDur < 0.25)
        {
            newStart = dragStartBeat + dragStartDuration - 0.25;
            newDur = 0.25;
        }
        newStart = (std::max)(0.0, newStart);
        auto note = model.getNote(dragNoteIndex);
        note.setProperty(IDs::startBeat, newStart, &engine.getProjectModel().getUndoManager());
        note.setProperty(IDs::durationBeats, newDur, &engine.getProjectModel().getUndoManager());
        emit notesChanged();
        update();
    }
    else
    {
        // Hover — update cursor
        int idx = noteIndexAtPos(pos);
        if (idx >= 0)
        {
            double localX = pos.x() - noteRect(idx).x();
            double noteW = noteRect(idx).width();
            if (localX < 5 || localX > noteW - 5)
                setCursor(Qt::SizeHorCursor);
            else
                setCursor(Qt::ArrowCursor);
        }
        else
        {
            setCursor(Qt::ArrowCursor);
        }
    }
}

void NoteGridWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (dragMode == Select)
    {
        QPoint releasePos = event->pos();
        int dragDist = (releasePos - dragStart).manhattanLength();

        if (dragDist > 5)
        {
            // Rubber-band select
            QRect selRect = QRect(dragStart, rubberBandEnd).normalized();
            model.deselectAll();
            for (int i = 0; i < model.getNumNotes(); ++i)
            {
                auto r = noteRect(i).toRect();
                if (selRect.intersects(r))
                    model.selectNote(model.getNote(i), true);
            }
            if (!model.getSelectedNotes().isEmpty())
                emit noteSelected(0);
            emit notesChanged();
            update();
        }
        else
        {
            // Click — create note (or chord stamp)
            int noteNum = noteNumberAtPos(releasePos.y());
            if (noteNum < 0)
            {
                // Click fell outside the MIDI range (ruler spacer or below
                // the grid) — don't create a stray note. Treat as a no-op.
                dragMode = None;
                return;
            }
            double beat = (releasePos.x() + scrollX) / pixelsPerBeat;
            beat = (std::max)(0.0, beat);
            if (snapEnabled)
                beat = snapToGrid(beat);

            if (chordStampEnabled)
            {
                PhraseGenerator::ChordParams cp;
                cp.scaleRoot = readModel->getScaleRoot();
                cp.scaleMode = readModel->getScaleMode();
                cp.lowNote = 0;
                cp.highNote = 127;
                cp.minVelocity = 80;
                cp.maxVelocity = 110;
                cp.chordType = chordStampType;
                cp.voicing = chordStampVoicing;
                cp.inversion = chordStampInversion;
                cp.arpeggiate = false;
                cp.durationBeats = chordStampDuration;

                auto chordNotes = PhraseGenerator::generateChord(noteNum, cp);
                int lastIdx = -1;
                for (const auto& cn : chordNotes)
                {
                    auto newNote = model.addNote(cn.noteNumber, static_cast<float>(cn.velocity),
                                                  beat, cn.durationBeats);
                    lastIdx = model.getNumNotes() - 1;
                }
                model.deselectAll();
                if (lastIdx >= 0)
                    model.selectNote(model.getNote(lastIdx));

                emit notesChanged();
                update();
                return;
            }

            auto newNote = model.addNote(noteNum, 100.0f, beat, lastNoteDuration);
            model.deselectAll();
            model.selectNote(newNote);

            dragMode = ResizeRight;
            dragNoteIndex = model.getNumNotes() - 1;
            dragStart = releasePos;
            dragStartBeat = beat;
            dragStartDuration = lastNoteDuration;
            dragStartNoteNumber = noteNum;

            emit notesChanged();
            update();
            return;
        }
    }

    if ((dragMode == ResizeRight || dragMode == ResizeLeft) && dragNoteIndex >= 0)
    {
        auto note = model.getNote(dragNoteIndex);
        if (note.isValid())
            lastNoteDuration = static_cast<double>(note.getProperty(IDs::durationBeats));
    }

    dragMode = None;
    dragNoteIndex = -1;
}

void NoteGridWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
    {
        model.removeSelectedNotes();
        emit notesChanged();
        update();
    }
    else if (event->key() == Qt::Key_A && (event->modifiers() & Qt::ControlModifier))
    {
        model.deselectAll();
        for (int i = 0; i < model.getNumNotes(); ++i)
            model.selectNote(model.getNote(i), true);
        update();
    }
    else if (event->key() == Qt::Key_C && (event->modifiers() & Qt::ControlModifier))
    {
        model.copySelectedNotes();
    }
    else if (event->key() == Qt::Key_V && (event->modifiers() & Qt::ControlModifier))
    {
        double targetBeat = static_cast<double>(scrollX) / pixelsPerBeat;
        model.pasteNotes(targetBeat);
        emit notesChanged();
        update();
    }
    else if (event->key() == Qt::Key_X && (event->modifiers() & Qt::ControlModifier))
    {
        model.copySelectedNotes();
        model.removeSelectedNotes();
        emit notesChanged();
        update();
    }
    else if (event->key() == Qt::Key_Up)
    {
        int semitones = (event->modifiers() & Qt::ControlModifier) ? 12 : 1;
        model.transposeSelected(semitones);
        emit notesChanged();
        update();
    }
    else if (event->key() == Qt::Key_Down)
    {
        int semitones = (event->modifiers() & Qt::ControlModifier) ? -12 : -1;
        model.transposeSelected(semitones);
        emit notesChanged();
        update();
    }
    else if (event->key() == Qt::Key_Q)
    {
        model.quantizeSelected(snapDivision, 1.0);
        emit notesChanged();
        update();
    }
    else if (event->key() == Qt::Key_H)
    {
        model.humanizeSelected(0.05, 5.0);
        emit notesChanged();
        update();
    }
}

void NoteGridWidget::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier)
    {
        // Horizontal zoom
        double factor = (event->angleDelta().y() > 0) ? 1.2 : 1.0 / 1.2;
        setPixelsPerBeat(pixelsPerBeat * factor);
        event->accept();
    }
    else if (event->modifiers() & Qt::ShiftModifier)
    {
        // Horizontal scroll
        scrollX = (std::max)(0, scrollX + event->angleDelta().y());
        update();
        event->accept();
    }
    else
    {
        // Vertical scroll (route through setScrollOffset to clamp to piano range)
        setScrollOffset(scrollX, scrollY - event->angleDelta().y());
        event->accept();
    }
}

void NoteGridWidget::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);

    QMenu* editMenu = menu.addMenu("Edit");
    editMenu->addAction("Quantize (Q)", this, [this]() {
        model.quantizeSelected(snapDivision, 1.0);
        emit notesChanged();
        update();
    });
    editMenu->addAction("Humanize (H)", this, [this]() {
        model.humanizeSelected(0.05, 5.0);
        emit notesChanged();
        update();
    });
    editMenu->addSeparator();
    editMenu->addAction("Transpose Up +1", this, [this]() {
        model.transposeSelected(1);
        emit notesChanged();
        update();
    });
    editMenu->addAction("Transpose Down -1", this, [this]() {
        model.transposeSelected(-1);
        emit notesChanged();
        update();
    });
    editMenu->addAction("Transpose Up Octave", this, [this]() {
        model.transposeSelected(12);
        emit notesChanged();
        update();
    });
    editMenu->addAction("Transpose Down Octave", this, [this]() {
        model.transposeSelected(-12);
        emit notesChanged();
        update();
    });
    editMenu->addSeparator();
    editMenu->addAction("Delete Selected", this, [this]() {
        model.removeSelectedNotes();
        emit notesChanged();
        update();
    });

    menu.addSeparator();
    menu.addAction("Select All", this, [this]() {
        model.deselectAll();
        for (int i = 0; i < model.getNumNotes(); ++i)
            model.selectNote(model.getNote(i), true);
        update();
    });

    menu.exec(event->globalPos());
}

void NoteGridWidget::focusOutEvent(QFocusEvent* event)
{
    QWidget::focusOutEvent(event);
    if (dragMode != None)
    {
        dragMode = None;
        dragNoteIndex = -1;
        update();
    }
}

void NoteGridWidget::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
    if (dragMode != None)
    {
        dragMode = None;
        dragNoteIndex = -1;
        update();
    }
}
