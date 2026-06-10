#pragma once
#include <QWidget>
#include <QTreeView>
#include <QListWidget>
#include <QFileSystemModel>
#include <QSplitter>
#include <QPushButton>
#include "../engine/AudioEngine.h"
#include "../model/ProjectModel.h"

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

    QFileSystemModel* fsModel;
    QTreeView* fileTree;
    QListWidget* poolList;
    QSplitter* splitter;
    QPushButton* addBtn;
};
