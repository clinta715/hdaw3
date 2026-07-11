#pragma once
#include <QWidget>
#include <QTreeView>
#include <QListWidget>
#include <QFileSystemModel>
#include <QSplitter>
#include <QPushButton>
#include "../common/AudioGraphCommands.h"
#include "../common/ReadModel.h"
#include "../model/ProjectModel.h"

class AudioEngine;

class ProjectPoolBrowser : public QWidget
{
    Q_OBJECT
public:
    ProjectPoolBrowser(AudioEngine& engine, QWidget* parent = nullptr);
    ~ProjectPoolBrowser() override;

    void refreshPool();

signals:
    void fileImported(const QString& path);

private:
    void setupUI();
    void importFile(const QString& path);
    void onFileActivated(const QModelIndex& index);
    void onPoolItemDoubleClicked(QListWidgetItem* item);

    AudioEngine& engine;
    AudioGraphCommands* audioGraphCmds = nullptr;
    ReadModel* readModel = nullptr;

    QFileSystemModel* fsModel;
    QTreeView* fileTree;
    QListWidget* poolList;
    QSplitter* splitter;
    QPushButton* addBtn;
};
