#include "StepEditorWidget.h"
#include "Theme.h"
#include "PreferencesDialog.h"
#include "DebugLog.h"
#include <QPainter>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSettings>

StepEditorWidget::StepEditorWidget(AudioEngine& ae, QWidget* parent)
    : QWidget(parent), engine(ae)
{
    setupUI();
}

StepEditorWidget::~StepEditorWidget() = default;

void StepEditorWidget::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto* headerBar = new QWidget(this);
    headerBar->setFixedHeight(headerH);
    auto* headerLayout = new QHBoxLayout(headerBar);
    headerLayout->setContentsMargins(8, 2, 8, 2);

    titleLabel = new QLabel("Step Editor - No clip selected", headerBar);
    titleLabel->setStyleSheet("color: #ccc; font-weight: bold;");

    switchButton = new QPushButton("Switch to Piano Roll", headerBar);
    switchButton->setFixedHeight(22);
    connect(switchButton, &QPushButton::clicked, this, [this]() {
        QSettings settings(PreferencesDialog::kSettingsOrg, PreferencesDialog::kSettingsApp);
        settings.setValue("midiEditorMode", "piano");
        emit switchToPianoRoll();
    });

    closeButton = new QPushButton("X", headerBar);
    closeButton->setFixedSize(22, 22);
    connect(closeButton, &QPushButton::clicked, this, [this]() {
        clear();
        emit clipClosed();
    });

    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(switchButton);
    headerLayout->addWidget(closeButton);
    mainLayout->addWidget(headerBar);

    setMinimumSize(400, 200);
    setMouseTracking(false);
}

void StepEditorWidget::loadClip(juce::ValueTree clipTree)
{
    currentClip = clipTree;
    isLoaded = clipTree.isValid() && clipTree.hasType(IDs::CLIP);
    if (isLoaded)
    {
        juce::String name = clipTree.getProperty(IDs::name, "MIDI").toString();
        titleLabel->setText(QString("Step Editor - ") + QString::fromUtf8(name.toRawUTF8()));
        syncFromClip();
    }
    else
    {
        clear();
    }
    update();
}

void StepEditorWidget::clear()
{
    currentClip = juce::ValueTree();
    isLoaded = false;
    for (int r = 0; r < numRows; ++r)
        for (int s = 0; s < numSteps; ++s)
            stepGrid[r][s] = false;
    titleLabel->setText("Step Editor - No clip selected");
    update();
}

void StepEditorWidget::syncFromClip()
{
    // Reset grid
    for (int r = 0; r < numRows; ++r)
        for (int s = 0; s < numSteps; ++s)
            stepGrid[r][s] = false;

    auto noteList = currentClip.getChildWithName(IDs::MIDI_NOTE_LIST);
    if (!noteList.isValid()) return;

    double beatStep = 1.0; // one quarter note per step
    for (int i = 0; i < noteList.getNumChildren(); ++i)
    {
        auto note = noteList.getChild(i);
        int nn = note.getProperty(IDs::noteNumber, 60);
        double startBeat = note.getProperty(IDs::startBeat, 0.0);

        int row = nn - noteStart;
        int step = static_cast<int>(std::round(startBeat / beatStep));

        if (row >= 0 && row < numRows && step >= 0 && step < numSteps)
            stepGrid[row][step] = true;
    }
}

void StepEditorWidget::toggleStep(int row, int step)
{
    if (!isLoaded || row < 0 || row >= numRows || step < 0 || step >= numSteps)
        return;

    bool newState = !stepGrid[row][step];
    stepGrid[row][step] = newState;
    commitNote(row, step, newState);
    update();
}

void StepEditorWidget::commitNote(int row, int step, bool add)
{
    auto noteList = currentClip.getChildWithName(IDs::MIDI_NOTE_LIST);
    if (!noteList.isValid())
    {
        if (!add) return;
        noteList = juce::ValueTree(IDs::MIDI_NOTE_LIST);
        currentClip.addChild(noteList, -1, nullptr);
    }

    int noteNumber = noteStart + row;
    double stepBeat = static_cast<double>(step);

    if (add)
    {
        auto& um = engine.getProjectModel().getUndoManager();
        juce::ValueTree note(IDs::MIDI_NOTE);
        note.setProperty(IDs::noteID, ProjectModel::allocateNoteID(), nullptr);
        note.setProperty(IDs::noteNumber, noteNumber, &um);
        note.setProperty(IDs::velocity, 100, &um);
        note.setProperty(IDs::startBeat, stepBeat, &um);
        note.setProperty(IDs::durationBeats, 1.0, &um);
        noteList.addChild(note, -1, &um);
    }
    else
    {
        // Remove matching note
        for (int i = 0; i < noteList.getNumChildren(); ++i)
        {
            auto note = noteList.getChild(i);
            int nn = note.getProperty(IDs::noteNumber, -1);
            double sb = note.getProperty(IDs::startBeat, -1.0);
            if (nn == noteNumber && std::abs(sb - stepBeat) < 0.01)
            {
                noteList.removeChild(i, &engine.getProjectModel().getUndoManager());
                return;
            }
        }
    }
}

void StepEditorWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    int w = width();
    int h = height();

    // Background
    p.fillRect(rect(), QColor(30, 30, 35));

    // Grid area below header
    int gridX = labelW;
    int gridY = headerH;
    int gridW = w - labelW;
    int gridH = h - headerH;

    // Compute cell sizes
    int cw = (gridW - 4) / numSteps;
    int ch = (gridH - 4) / numRows;
    if (cw < 12) cw = 12;
    if (ch < 12) ch = 12;

    // Draw rows
    for (int r = 0; r < numRows; ++r)
    {
        int rowY = gridY + 2 + r * ch;
        int nn = noteStart + r;

        // Label
        static const char* noteNames[12] = {
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
        };
        int octave = nn / 12 - 1;
        QString label = QString("%1%2").arg(noteNames[nn % 12]).arg(octave);

        QRect labelRect(2, rowY, labelW - 4, ch);
        p.setPen(QColor(180, 180, 190));
        QFont lf = p.font();
        lf.setPointSize(8);
        p.setFont(lf);
        p.drawText(labelRect, Qt::AlignVCenter | Qt::AlignRight, label);

        // Step cells
        for (int s = 0; s < numSteps; ++s)
        {
            int cellX = gridX + 2 + s * cw;
            int cellW = cw - 1;
            int cellH = ch - 1;

            QRect cellRect(cellX, rowY, cellW, cellH);

            // Beat-group shading: darker background on beat 1, 3, 5...
            bool isBeatStart = (s % 4 == 0);
            QColor bg = isBeatStart
                ? QColor(40, 40, 48)
                : QColor(35, 35, 40);

            // Active step
            if (stepGrid[r][s])
            {
                int hue = (nn * 37) % 360;
                bg = QColor::fromHsv(hue, 200, 180);
            }

            p.fillRect(cellRect, bg);

            // Cell border
            p.setPen(QPen(QColor(50, 50, 55), 1));
            p.drawRect(cellRect);
        }
    }

    // Step number labels at bottom
    QFont sf = p.font();
    sf.setPointSize(7);
    p.setFont(sf);
    for (int s = 0; s < numSteps; ++s)
    {
        int cellX = gridX + 2 + s * cw;
        int labelY = gridY + 2 + numRows * ch + 2;
        int bar = s / 4 + 1;
        int beat = s % 4 + 1;
        QString stepLabel = QString("%1.%2").arg(bar).arg(beat);
        p.setPen(QColor(120, 120, 130));
        p.drawText(QRect(cellX, labelY, cw, 16), Qt::AlignLeft | Qt::AlignVCenter, stepLabel);
    }
}

void StepEditorWidget::mousePressEvent(QMouseEvent* event)
{
    if (!isLoaded) return;

    int w = width();
    int gridX = labelW;
    int gridW = w - labelW;
    int gridH = height() - headerH;

    int cw = (gridW - 4) / numSteps;
    int ch = (gridH - 4) / numRows;
    if (cw < 12) cw = 12;
    if (ch < 12) ch = 12;

    int mx = static_cast<int>(event->pos().x());
    int my = static_cast<int>(event->pos().y());

    int row = (my - headerH - 2) / ch;
    int step = (mx - gridX - 2) / cw;

    if (row >= 0 && row < numRows && step >= 0 && step < numSteps)
        toggleStep(row, step);
}

void StepEditorWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    // Recalculate layout
}
