#include "ProjectPoolBrowser.h"
#include "../engine/AudioEngine.h"
#include "PreferencesDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QHeaderView>
#include <QFileDialog>
#include <QSettings>
#include <QMessageBox>
#include <QDir>
#include <QKeyEvent>
#include <QStyle>
#include <QFrame>

ProjectPoolBrowser::ProjectPoolBrowser(AudioEngine& ae, QWidget* parent)
    : QWidget(parent), engine(ae)
{
    projectCmds = &engine.getProjectCommands();
    transportCmds = &engine.getTransportCommands();
    audioGraphCmds = &engine.getAudioGraphCommands();
    readModel = &engine.getReadModel();

    // Create the preview player using the engine's device/format managers
    previewPlayer = std::make_unique<HDAW::AudioPreviewPlayer>(
        engine.getDeviceManager(), engine.getProjectPool().getFormatManager());

    setupUI();

    // Restore last browsed directory; fallback chain: saved → default → home
    auto& settings = PreferencesDialog::settings();
    currentRootDir = settings.value(PreferencesDialog::kKeyLastBrowserDir).toString();
    if (currentRootDir.isEmpty())
        currentRootDir = PreferencesDialog::getDefaultAudioDir();
    if (currentRootDir.isEmpty())
        currentRootDir = QDir::homePath();

    if (!currentRootDir.isEmpty())
        updateCurrentDir(currentRootDir);
}

ProjectPoolBrowser::~ProjectPoolBrowser()
{
    stopPreview();
    saveBrowsedDir();
}

void ProjectPoolBrowser::setupUI()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    splitter = new QSplitter(Qt::Vertical, this);
    splitter->setHandleWidth(2);

    // File Browser Section
    auto* browserContainer = new QWidget(this);
    auto* browserLayout = new QVBoxLayout(browserContainer);
    browserLayout->setContentsMargins(4, 4, 4, 4);
    browserLayout->setSpacing(2);

    // Title row with path label
    auto* titleRow = new QHBoxLayout;
    auto* browserTitle = new QLabel("File Browser", browserContainer);
    titleRow->addWidget(browserTitle);
    titleRow->addStretch();
    browserLayout->addLayout(titleRow);

    // Navigation toolbar: Up, current path, quick dirs
    auto* navRow = new QHBoxLayout;
    navRow->setSpacing(4);

    auto* upBtn = new QPushButton("\xe2\x86\x91 Up", browserContainer);
    upBtn->setFixedWidth(60);
    upBtn->setToolTip("Go to parent directory (Backspace)");
    connect(upBtn, &QPushButton::clicked, this, &ProjectPoolBrowser::navigateUp);
    navRow->addWidget(upBtn);

    pathLabel = new QLabel(browserContainer);
    pathLabel->setStyleSheet("color: #888; font-size: 11px;");
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    navRow->addWidget(pathLabel, 1);

    // Quick-access dirs
    auto* dirCombo = new QComboBox(browserContainer);
    dirCombo->setToolTip("Quick navigate to default directories");
    dirCombo->addItem("Audio samples dir", PreferencesDialog::getDefaultAudioDir());
    dirCombo->addItem("MIDI files dir", PreferencesDialog::getDefaultMidiDir());
    dirCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    dirCombo->setCurrentIndex(-1);
    connect(dirCombo, QOverload<int>::of(&QComboBox::activated), this, &ProjectPoolBrowser::goToDefaultDir);
    navRow->addWidget(dirCombo);

    browserLayout->addLayout(navRow);

    fsModel = new QFileSystemModel(this);
    fsModel->setRootPath("");
    fsModel->setNameFilters({"*.wav", "*.aiff", "*.aif", "*.mp3", "*.flac", "*.ogg"});
    fsModel->setNameFilterDisables(false);

    fileTree = new QTreeView(browserContainer);
    fileTree->setModel(fsModel);
    fileTree->setDragEnabled(true);
    fileTree->setAnimated(true);
    fileTree->setIndentation(16);
    fileTree->setSortingEnabled(false);
    fileTree->setColumnHidden(1, true);
    fileTree->setColumnHidden(2, true);
    fileTree->setColumnHidden(3, true);
    fileTree->header()->setStretchLastSection(false);
    fileTree->header()->setSectionResizeMode(0, QHeaderView::Fixed);
    fileTree->header()->resizeSection(0, 250);
    fileTree->setFocusPolicy(Qt::StrongFocus);

    connect(fileTree, &QTreeView::activated, this, &ProjectPoolBrowser::onFileActivated);
    connect(fileTree, &QTreeView::doubleClicked, this, &ProjectPoolBrowser::onFileActivated);
    connect(fileTree, &QTreeView::clicked, this, &ProjectPoolBrowser::onFileClicked);

    browserLayout->addWidget(fileTree, 1);

    // Preview toolbar
    auto* previewBar = new QWidget(browserContainer);
    previewBar->setFixedHeight(28);
    auto* previewLayout = new QHBoxLayout(previewBar);
    previewLayout->setContentsMargins(4, 2, 4, 2);
    previewLayout->setSpacing(4);

    previewPlayBtn = new QPushButton("\u25B6", previewBar);
    previewPlayBtn->setFixedSize(24, 20);
    previewPlayBtn->setToolTip("Preview selected file");
    previewPlayBtn->setStyleSheet("QPushButton { font-size: 10pt; }");
    previewLayout->addWidget(previewPlayBtn);

    previewStopBtn = new QPushButton("\u25A0", previewBar);
    previewStopBtn->setFixedSize(24, 20);
    previewStopBtn->setToolTip("Stop preview");
    previewStopBtn->setStyleSheet("QPushButton { font-size: 10pt; }");
    previewStopBtn->setEnabled(false);
    previewLayout->addWidget(previewStopBtn);

    auto* sep = new QFrame(previewBar);
    sep->setFrameShape(QFrame::VLine);
    sep->setFixedHeight(18);
    previewLayout->addWidget(sep);

    tempoMatchCheck = new QCheckBox("At Tempo", previewBar);
    tempoMatchCheck->setToolTip("Preview at project tempo (time-stretched)");
    tempoMatchCheck->setStyleSheet("color: #a8a8b0; font-size: 8pt;");
    previewLayout->addWidget(tempoMatchCheck);

    auto* volLbl = new QLabel("Vol:", previewBar);
    volLbl->setStyleSheet("color: #a8a8b0; font-size: 8pt;");
    previewLayout->addWidget(volLbl);

    previewVolumeSlider = new QSlider(Qt::Horizontal, previewBar);
    previewVolumeSlider->setRange(0, 100);
    previewVolumeSlider->setValue(80);
    previewVolumeSlider->setFixedWidth(80);
    previewVolumeSlider->setFixedHeight(16);
    previewLayout->addWidget(previewVolumeSlider);

    previewLabel = new QLabel(previewBar);
    previewLabel->setStyleSheet("color: #787880; font-size: 8pt;");
    previewLabel->setText("No preview");
    previewLayout->addWidget(previewLabel, 1);

    connect(previewPlayBtn, &QPushButton::clicked, this, [this]() {
        if (lastClickedIndex.isValid() && !fsModel->isDir(lastClickedIndex))
            startPreview(fsModel->filePath(lastClickedIndex));
    });
    connect(previewStopBtn, &QPushButton::clicked, this, &ProjectPoolBrowser::stopPreview);

    connect(tempoMatchCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (previewPlayer)
            previewPlayer->setTempoMatch(checked,
                readModel->getTransport().bpm > 0 ? readModel->getTransport().bpm : 120.0);
    });

    connect(previewVolumeSlider, &QSlider::valueChanged, this, [this](int val) {
        if (previewPlayer)
            previewPlayer->setVolume(val / 100.0f);
    });

    browserLayout->addWidget(previewBar);

    splitter->addWidget(browserContainer);

    // Project Pool Section
    auto* poolContainer = new QWidget(this);
    auto* poolLayout = new QVBoxLayout(poolContainer);
    poolLayout->setContentsMargins(4, 4, 4, 4);
    poolLayout->setSpacing(2);

    auto* poolTitle = new QLabel("Project Pool", poolContainer);
    poolLayout->addWidget(poolTitle);

    poolList = new QListWidget(poolContainer);
    poolList->setDragEnabled(true);
    poolList->setAlternatingRowColors(true);

    connect(poolList, &QListWidget::itemDoubleClicked,
            this, &ProjectPoolBrowser::onPoolItemDoubleClicked);

    poolLayout->addWidget(poolList, 1);

    addBtn = new QPushButton("Add File to Pool", poolContainer);
    connect(addBtn, &QPushButton::clicked, this, [this]() {
        auto& settings = PreferencesDialog::settings();
        auto fileDir = settings.value(PreferencesDialog::kKeyLastProjectDir).toString();
        if (fileDir.isEmpty())
            fileDir = PreferencesDialog::getDefaultAudioDir();
        if (fileDir.isEmpty())
            fileDir = QDir::homePath();
        QString file = QFileDialog::getOpenFileName(this, "Import Audio",
            fileDir,
            "Audio Files (*.wav *.aiff *.aif *.mp3 *.flac *.ogg)");
        if (!file.isEmpty())
            importFile(file);
    });
    poolLayout->addWidget(addBtn);

    splitter->addWidget(poolContainer);

    layout->addWidget(splitter, 1);
}

void ProjectPoolBrowser::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Backspace)
    {
        navigateUp();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void ProjectPoolBrowser::onFileActivated(const QModelIndex& index)
{
    if (fsModel->isDir(index))
    {
        // Enter the directory
        QString dir = fsModel->filePath(index);
        navigateToDir(dir);
    }
    else
    {
        QString path = fsModel->filePath(index);
        importFile(path);
    }
}

void ProjectPoolBrowser::onFileClicked(const QModelIndex& index)
{
    lastClickedIndex = index;
    if (!fsModel->isDir(index))
    {
        QString path = fsModel->filePath(index);
        startPreview(path);
    }
}

void ProjectPoolBrowser::startPreview(const QString& path)
{
    if (path.isEmpty() || !previewPlayer) return;

    QFileInfo fi(path);
    if (!fi.exists()) return;

    // Stop any current preview
    stopPreview();

    previewPlayer->loadFile(juce::File(path.toUtf8().constData()));

    // Set tempo match with project BPM
    if (tempoMatchCheck->isChecked())
    {
        double bpm = readModel->getTransport().bpm;
        if (bpm <= 0) bpm = 120.0;
        previewPlayer->setTempoMatch(true, bpm);
    }

    previewPlayer->setVolume(previewVolumeSlider->value() / 100.0f);
    previewPlayer->play();

    previewPlayBtn->setEnabled(false);
    previewStopBtn->setEnabled(true);
    previewLabel->setText(fi.fileName());
}

void ProjectPoolBrowser::stopPreview()
{
    if (previewPlayer)
        previewPlayer->stop();

    previewPlayBtn->setEnabled(true);
    previewStopBtn->setEnabled(false);
    previewLabel->setText("No preview");
}

void ProjectPoolBrowser::navigateUp()
{
    QDir dir(currentRootDir);
    if (dir.isRoot())
        return;
    QString parent = dir.absolutePath();
    QDir parentDir(parent);
    if (parentDir.cdUp())
    {
        QString newDir = parentDir.absolutePath();
        if (!newDir.isEmpty() && newDir != currentRootDir)
            navigateToDir(newDir);
    }
}

void ProjectPoolBrowser::navigateToDir(const QString& dir)
{
    if (dir.isEmpty() || dir == currentRootDir)
        return;
    updateCurrentDir(dir);
}

void ProjectPoolBrowser::updateCurrentDir(const QString& dir)
{
    currentRootDir = dir;
    fsModel->setRootPath(dir);
    fileTree->setRootIndex(fsModel->index(dir));
    pathLabel->setText(dir);
    saveBrowsedDir();
}

void ProjectPoolBrowser::goToDefaultDir(int index)
{
    if (index < 0) return;
    QComboBox* combo = qobject_cast<QComboBox*>(sender());
    if (!combo) return;
    QString dir = combo->itemData(index).toString();
    if (!dir.isEmpty())
        navigateToDir(dir);
}

void ProjectPoolBrowser::onPoolItemDoubleClicked(QListWidgetItem* item)
{
    if (item)
        importFile(item->data(Qt::UserRole).toString());
}

void ProjectPoolBrowser::importFile(const QString& path)
{
    if (path.isEmpty()) return;

    QFileInfo fi(path);
    if (!fi.exists()) return;

    addToPool(path);

    // Create clip on the first track
    if (readModel->getTrackCount() > 0)
    {
        // Read actual audio file duration
        double duration = 4.0;
        auto& pool = engine.getProjectPool();
        auto reader = std::unique_ptr<juce::AudioFormatReader>(
            pool.getFormatManager().createReaderFor(juce::File(path.toUtf8().constData())));
        if (reader != nullptr)
            duration = reader->lengthInSamples / reader->sampleRate;

        // Find the next position (after the last clip)
        double startTime = 0.0;
        auto snap = readModel->snapshot();
        for (const auto& clip : snap.clips)
        {
            if (clip.trackIndex == 0)
            {
                double end = clip.startBeat + clip.durationBeats;
                startTime = (std::max)(startTime, end);
            }
        }

        projectCmds->addAudioClip(0, startTime, duration,
            path.toUtf8().constData(),
            fi.baseName().toUtf8().constData());

        audioGraphCmds->rebuildRoutingGraph();
    }

    emit fileImported(path);
}

void ProjectPoolBrowser::addToPool(const QString& path)
{
    if (path.isEmpty()) return;

    QFileInfo fi(path);
    if (!fi.exists()) return;

    // Add to pool list if not already present
    bool exists = false;
    for (int i = 0; i < poolList->count(); ++i)
    {
        if (poolList->item(i)->data(Qt::UserRole).toString() == path)
        {
            exists = true;
            break;
        }
    }

    if (!exists)
    {
        auto* item = new QListWidgetItem(fi.fileName());
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
        poolList->addItem(item);
    }
}

void ProjectPoolBrowser::refreshPool()
{
    // Clear and rebuild from ValueTree if needed
    poolList->clear();
    auto trackList = engine.getProjectModel().getTrackListTree();
    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
        if (clipList.isValid())
        {
            for (int c = 0; c < clipList.getNumChildren(); ++c)
            {
                auto clip = clipList.getChild(c);
                juce::String src = clip.getProperty(IDs::sourceFile).toString();
                if (src.isNotEmpty())
                {
                    QString path = QString::fromUtf8(src.toRawUTF8());
                    QFileInfo fi(path);
                    if (fi.exists())
                    {
                        auto* item = new QListWidgetItem(fi.fileName());
                        item->setData(Qt::UserRole, path);
                        item->setToolTip(path);
                        poolList->addItem(item);
                    }
                }
            }
        }
    }
}

void ProjectPoolBrowser::saveBrowsedDir() const
{
    if (currentRootDir.isEmpty())
        return;
    auto& settings = PreferencesDialog::settings();
    settings.setValue(PreferencesDialog::kKeyLastBrowserDir, currentRootDir);
}
