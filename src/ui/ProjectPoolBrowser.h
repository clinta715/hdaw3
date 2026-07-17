#pragma once
#include <QWidget>
#include <QTreeView>
#include <QListWidget>
#include <QFileSystemModel>
#include <QSplitter>
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QCheckBox>
#include "../common/ProjectCommands.h"
#include "../common/TransportCommands.h"
#include "../common/AudioGraphCommands.h"
#include "../common/ReadModel.h"

class AudioEngine;
#include "../model/ProjectModel.h"
#include "../engine/AudioPreviewPlayer.h"

class ProjectPoolBrowser : public QWidget
{
    Q_OBJECT
public:
    ProjectPoolBrowser(AudioEngine& engine, QWidget* parent = nullptr);
    ~ProjectPoolBrowser() override;

    void refreshPool();

signals:
    void fileImported(const QString& path);

public slots:
    void importFile(const QString& path);
    void addToPool(const QString& path);

private:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void setupUI();
    void onFileActivated(const QModelIndex& index);
    void onFileClicked(const QModelIndex& index);
    void onPoolItemDoubleClicked(QListWidgetItem* item);
    void navigateUp();
    void navigateToDir(const QString& dir);
    void updateCurrentDir(const QString& dir);
    void goToDefaultDir(int index);

    void startPreview(const QString& path);
    void stopPreview();

    void saveBrowsedDir() const;
    QString currentRootDir;

    AudioEngine& engine;
    ProjectCommands* projectCmds = nullptr;
    TransportCommands* transportCmds = nullptr;
    AudioGraphCommands* audioGraphCmds = nullptr;
    ReadModel* readModel = nullptr;

    QFileSystemModel* fsModel;
    QTreeView* fileTree;
    QListWidget* poolList;
    QSplitter* splitter;
    QPushButton* addBtn;
    QLabel* pathLabel;

    // Preview controls
    std::unique_ptr<HDAW::AudioPreviewPlayer> previewPlayer;
    QPushButton* previewPlayBtn = nullptr;
    QPushButton* previewStopBtn = nullptr;
    QCheckBox* tempoMatchCheck = nullptr;
    QSlider* previewVolumeSlider = nullptr;
    QLabel* previewLabel = nullptr;
    QModelIndex lastClickedIndex;
};
