#include "PianoRollWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>

PianoRollWidget::PianoRollWidget(AudioEngine& ae, QWidget* parent)
    : QWidget(parent), engine(ae)
{
    setupUI();
    connectSignals();
    clear();
}

PianoRollWidget::~PianoRollWidget() = default;

void PianoRollWidget::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Header bar with clip name and close button
    auto* header = new QWidget(this);
    header->setFixedHeight(28);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(8, 2, 8, 2);

    auto* closeBtn = new QPushButton("X", header);
    closeBtn->setFixedSize(20, 20);
    connect(closeBtn, &QPushButton::clicked, this, &PianoRollWidget::clipClosed);
    headerLayout->addWidget(closeBtn);

    auto* titleLabel = new QLabel("Piano Roll", header);
    titleLabel->setObjectName("pianoRollTitle");
    headerLayout->addWidget(titleLabel);

    headerLayout->addStretch();

    auto* zoomInBtn = new QPushButton("+", header);
    zoomInBtn->setFixedSize(20, 20);
    connect(zoomInBtn, &QPushButton::clicked, this, [this]() { updateZoom(1.3); });
    headerLayout->addWidget(zoomInBtn);

    auto* zoomOutBtn = new QPushButton("-", header);
    zoomOutBtn->setFixedSize(20, 20);
    connect(zoomOutBtn, &QPushButton::clicked, this, [this]() { updateZoom(1.0 / 1.3); });
    headerLayout->addWidget(zoomOutBtn);

    mainLayout->addWidget(header);

    // Ruler row
    auto* rulerRow = new QWidget(this);
    auto* rulerLayout = new QHBoxLayout(rulerRow);
    rulerLayout->setContentsMargins(0, 0, 0, 0);
    rulerLayout->setSpacing(0);

    // Spacer to align ruler with note grid (offset by piano key width)
    auto* rulerSpacer = new QWidget(rulerRow);
    rulerSpacer->setFixedWidth(PianoKeysWidget::keyWidth);
    rulerLayout->addWidget(rulerSpacer);

    ruler = new PianoRollRuler(rulerRow);
    rulerLayout->addWidget(ruler, 1);

    mainLayout->addWidget(rulerRow);

    // Main content: keys + note grid (+ scrollbar in its own column so it never overlays the grid)
    auto* contentRow = new QWidget(this);
    auto* contentLayout = new QGridLayout(contentRow);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    keys = new PianoKeysWidget(contentRow);
    contentLayout->addWidget(keys, 0, 0);

    noteGrid = new NoteGridWidget(model, engine, contentRow);
    contentLayout->addWidget(noteGrid, 0, 1);

    mainLayout->addWidget(contentRow, 1);

    // Velocity lane
    velocityLane = new VelocityLaneWidget(model, this);
    mainLayout->addWidget(velocityLane);

    // Horizontal scroll bar
    hScrollBar = new QScrollBar(Qt::Horizontal, this);
    hScrollBar->setRange(0, 2000);
    mainLayout->addWidget(hScrollBar);

    // Vertical scroll bar in its own column to the right of the note grid.
    vScrollBar = new QScrollBar(Qt::Vertical, contentRow);
    vScrollBar->setRange(0, 1280);
    contentLayout->addWidget(vScrollBar, 0, 2);
}

void PianoRollWidget::connectSignals()
{
    connect(hScrollBar, &QScrollBar::valueChanged, this, [this](int val) {
        ruler->setScrollOffset(val);
        noteGrid->setScrollOffset(val, noteGrid->getScrollY());
        velocityLane->setScrollOffset(val);
    });

    connect(vScrollBar, &QScrollBar::valueChanged, this, [this](int val) {
        keys->setScrollOffset(val);
        noteGrid->setScrollOffset(noteGrid->getScrollX(), val);
    });

    connect(noteGrid, &NoteGridWidget::notesChanged, this, [this]() {
        velocityLane->update();
    });

    connect(velocityLane, &VelocityLaneWidget::velocityChanged, this, [this]() {
        noteGrid->update();
    });

    connect(noteGrid, &NoteGridWidget::noteSelected, this, [this](int) {
        velocityLane->update();
    });
}

void PianoRollWidget::loadClip(juce::ValueTree clipTree)
{
    model.setClipTree(clipTree);
    model.setUndoManager(&engine.getProjectModel().getUndoManager());
    isLoaded = model.isValid();

    if (isLoaded)
    {
        QString name = QString::fromUtf8(clipTree.getProperty(IDs::name).toString().toRawUTF8());
        auto* title = findChild<QLabel*>("pianoRollTitle");
        if (title) title->setText("Piano Roll - " + name);
    }

    // Land on middle C (note 60) by default so default clips are visible immediately.
    int midCScrollY = noteGrid->defaultScrollYForMiddleC();
    noteGrid->setScrollOffset(0, midCScrollY);
    velocityLane->setScrollOffset(0);
    if (vScrollBar != nullptr)
        vScrollBar->setValue(midCScrollY);

    noteGrid->update();
    velocityLane->update();
    ruler->update();
    show();
}

void PianoRollWidget::clear()
{
    model.clear();
    isLoaded = false;
    auto* title = findChild<QLabel*>("pianoRollTitle");
    if (title) title->setText("Piano Roll - No clip selected");
    noteGrid->update();
    velocityLane->update();
}

void PianoRollWidget::updateZoom(double factor)
{
    double ppb = noteGrid->getPixelsPerBeat() * factor;
    ppb = std::max(10.0, std::min(200.0, ppb));
    noteGrid->setPixelsPerBeat(ppb);
    velocityLane->setPixelsPerBeat(ppb);
    ruler->setPixelsPerBeat(ppb);
}
