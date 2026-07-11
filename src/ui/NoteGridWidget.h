#pragma once
#include <QWidget>
#include <QTimer>
#include <QMenu>
#include <QContextMenuEvent>
#include <bitset>
#include "PianoRollModel.h"
#include "../common/ProjectCommands.h"
#include "../common/TransportCommands.h"
#include "../common/AudioGraphCommands.h"
#include "../common/ReadModel.h"

class AudioEngine;

class NoteGridWidget : public QWidget
{
    Q_OBJECT
public:
    NoteGridWidget(PianoRollModel& model, AudioEngine& engine, QWidget* parent = nullptr);

    void setPixelsPerBeat(double ppb);
    double getPixelsPerBeat() const { return pixelsPerBeat; }
    void setKeyHeight(double kh) { keyHeight = kh; update(); }
    double getKeyHeight() const { return keyHeight; }
    void setScrollOffset(int x, int y);
    int getScrollX() const { return scrollX; }
    int getScrollY() const { return scrollY; }

    void setSnapEnabled(bool enabled);
    void setSnapDivision(double division);

    // Chord stamp mode
    void setChordStampEnabled(bool enabled) { chordStampEnabled = enabled; }
    bool isChordStampEnabled() const { return chordStampEnabled; }
    void setChordStampType(int chordTypeIndex) { chordStampType = chordTypeIndex; }
    int getChordStampType() const { return chordStampType; }
    void setChordStampVoicing(int v) { chordStampVoicing = v; }
    void setChordStampInversion(int inv) { chordStampInversion = inv; }
    void setChordStampDuration(double dur) { chordStampDuration = dur; }

    int noteNumberAtPos(int y) const;
    int noteIndexAtPos(const QPoint& pos) const;
    int defaultScrollYForMiddleC() const
    {
        return static_cast<int>(96 - 1 - 60) * static_cast<int>(keyHeight);
    }

signals:
    void noteSelected(int noteIndex);
    void notesChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    QRectF noteRect(int noteIndex) const;
    void createNoteAtPos(const QPoint& pos, float velocity = 100.0f, double durationBeats = 1.0);
    double snapToGrid(double beat) const;

    PianoRollModel& model;
    AudioEngine& engine;
    ProjectCommands* projectCmds = nullptr;
    TransportCommands* transportCmds = nullptr;
    AudioGraphCommands* audioGraphCmds = nullptr;
    ReadModel* readModel = nullptr;

    double pixelsPerBeat = 40.0;
    double keyHeight = 10.0;
    int scrollX = 0;
    int scrollY = 0;

    // Cached scale-pitch lookup. Rebuilt only when (scaleRoot, scaleMode) change,
    // not on every paint. Avoids the per-paint cost of PhraseGenerator::buildScalePitches
    // and a 128-entry array fill in NoteGridWidget::paintEvent.
    int cachedScaleRoot = -1;
    int cachedScaleMode = -1;
    std::bitset<128> inScale;

    bool snapEnabled = true;
    double snapDivision = 0.0625; // 1/16

    enum DragMode { None, Create, Move, ResizeLeft, ResizeRight, Select };
    DragMode dragMode = None;
    int dragNoteIndex = -1;
    QPoint dragStart;
    double dragStartBeat = 0.0;
    double dragStartDuration = 0.0;
    int dragStartNoteNumber = 0;
    QPoint rubberBandEnd{0, 0};
    double lastNoteDuration = 1.0;

    // Chord stamp state
    bool chordStampEnabled = false;
    int chordStampType = 0;
    int chordStampVoicing = 0;
    int chordStampInversion = 0;
    double chordStampDuration = 2.0;
};
