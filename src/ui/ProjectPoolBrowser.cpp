#include "ProjectPoolBrowser.h"
#include "../engine/AudioEngine.h"
#include "PreferencesDialog.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QHeaderView>
#include <QFileDialog>
#include <QSettings>
#include <QMessageBox>

ProjectPoolBrowser::ProjectPoolBrowser(AudioEngine& ae, QWidget* parent)
    : QWidget(parent), engine(ae)
{
    projectCmds = &engine.getProjectCommands();
    transportCmds = &engine.getTransportCommands();
    audioGraphCmds = &engine.getAudioGraphCommands();
    readModel = &engine.getReadModel();
    setupUI();

    // Restore last browsed directory
    auto& settings = PreferencesDialog::settings();
    currentRootDir = settings.value(PreferencesDialog::kKeyLastBrowserDir).toString();
    if (currentRootDir.isEmpty())
        currentRootDir = PreferencesDialog::getDefaultAudioDir();
    if (!currentRootDir.isEmpty())
    {
        fsModel->setRootPath(currentRootDir);
        fileTree->setRootIndex(fsModel->index(currentRootDir));
    }

    // Save current directory on navigation
    connect(fileTree, &QTreeView::clicked, this, [this](const QModelIndex& idx) {
        if (fsModel->isDir(idx))
        {
            currentRootDir = fsModel->filePath(idx);
            saveBrowsedDir();
        }
    });
}

ProjectPoolBrowser::~ProjectPoolBrowser()
{
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

    auto* browserTitle = new QLabel("File Browser", browserContainer);
    browserLayout->addWidget(browserTitle);

    fsModel = new QFileSystemModel(this);
    fsModel->setRootPath("");
    fsModel->setNameFilters({"*.wav", "*.aiff", "*.aif", "*.mp3", "*.flac", "*.ogg"});
    fsModel->setNameFilterDisables(false);

    fileTree = new QTreeView(browserContainer);
    fileTree->setModel(fsModel);
    fileTree->setDragEnabled(true);
    fileTree->setAnimated(true);
    fileTree->setIndentation(16);
    fileTree->setSortingEnabled(true);
    fileTree->setColumnHidden(1, true);
    fileTree->setColumnHidden(2, true);
    fileTree->setColumnHidden(3, true);
    fileTree->header()->setStretchLastSection(false);
    fileTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);

    connect(fileTree, &QTreeView::activated, this, &ProjectPoolBrowser::onFileActivated);
    connect(fileTree, &QTreeView::doubleClicked, this, &ProjectPoolBrowser::onFileActivated);

    browserLayout->addWidget(fileTree, 1);

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

void ProjectPoolBrowser::onFileActivated(const QModelIndex& index)
{
    if (!fsModel->isDir(index))
    {
        QString path = fsModel->filePath(index);
        importFile(path);
    }
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

    // Create clip on the first track
    if (readModel->getTrackCount() > 0)
    {
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

        projectCmds->addAudioClip(0, startTime, 4.0,
            path.toUtf8().constData(),
            fi.baseName().toUtf8().constData());

        audioGraphCmds->rebuildRoutingGraph();
    }

    emit fileImported(path);
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
