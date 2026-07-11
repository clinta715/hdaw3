#include "StatusBar.h"
#include "Theme.h"
#include <QHBoxLayout>
#include <QFrame>

namespace
{
QLabel* makeField(QWidget* parent)
{
    auto* l = new QLabel("--", parent);
    l->setStyleSheet(
        "QLabel { color: #c8c8cc; font-size: 8pt; padding: 1px 6px; "
        "background: transparent; }");
    l->setMinimumWidth(40);
    return l;
}

QFrame* makeSeparator(QWidget* parent)
{
    auto* sep = new QFrame(parent);
    sep->setFrameShape(QFrame::VLine);
    sep->setStyleSheet("color: #2a2a2e;");
    sep->setFixedHeight(14);
    return sep;
}
}

StatusBar::StatusBar(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 2, 8, 2);
    layout->setSpacing(0);

    selLabel = makeField(this);
    selLabel->setText("No selection");
    layout->addWidget(selLabel);

    layout->addStretch(1);

    recLabel = makeField(this);
    recLabel->setText("");
    recLabel->setStyleSheet(
        "QLabel { color: #ef4444; font-weight: bold; font-size: 8pt; padding: 1px 6px; }");
    layout->addWidget(recLabel);

    layout->addWidget(makeSeparator(this));

    trackLabel = makeField(this);
    trackLabel->setText("No track");
    layout->addWidget(trackLabel);

    layout->addWidget(makeSeparator(this));

    bpmLabel = makeField(this);
    bpmLabel->setText("-- BPM");
    layout->addWidget(bpmLabel);

    timeSigLabel = makeField(this);
    timeSigLabel->setText("--/--");
    layout->addWidget(timeSigLabel);

    layout->addWidget(makeSeparator(this));

    sampleRateLabel = makeField(this);
    sampleRateLabel->setText("-- Hz");
    layout->addWidget(sampleRateLabel);

    layout->addWidget(makeSeparator(this));

    midiLabel = makeField(this);
    midiLabel->setText("MIDI: --");
    layout->addWidget(midiLabel);

    setStyleSheet(
        "QWidget { background: #0e0e10; border-top: 1px solid #2a2a2e; }");
    setFixedHeight(22);
}

void StatusBar::setBPM(double bpm)
{
    bpmLabel->setText(QString::number(bpm, 'f', 1) + " BPM");
}

void StatusBar::setTimeSignature(int numerator, int denominator)
{
    timeSigLabel->setText(QString("%1/%2").arg(numerator).arg(denominator));
}

void StatusBar::setSampleRate(double sampleRate)
{
    if (sampleRate > 0)
        sampleRateLabel->setText(QString::number(sampleRate / 1000.0, 'f', 1) + " kHz");
    else
        sampleRateLabel->setText("-- Hz");
}

void StatusBar::setSelectedTrack(int trackIndex, const QString& trackName)
{
    if (trackIndex < 0 || trackName.isEmpty())
        trackLabel->setText("No track");
    else
        trackLabel->setText(QString("Track %1: %2").arg(trackIndex + 1).arg(trackName));
}

void StatusBar::setMidiDevice(const QString& deviceName)
{
    if (deviceName.isEmpty() || deviceName == "None")
        midiLabel->setText("MIDI: --");
    else
        midiLabel->setText("MIDI: " + deviceName);
}

void StatusBar::setRecording(bool recording)
{
    recLabel->setText(recording ? "\xE2\x97\x8F REC" : "");
}

void StatusBar::setSelectionCount(int count)
{
    if (count == 0)
        selLabel->setText("No selection");
    else if (count == 1)
        selLabel->setText("1 clip selected");
    else
        selLabel->setText(QString("%1 clips selected").arg(count));
}
