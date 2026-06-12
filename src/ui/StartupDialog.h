#pragma once
#include <QDialog>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QStringList>

class StartupDialog : public QDialog
{
    Q_OBJECT
public:
    enum Action { NewProject, OpenRecent, OpenOther, Cancel };

    explicit StartupDialog(QWidget* parent = nullptr);
    ~StartupDialog() override;

    Action getAction() const { return chosenAction; }
    QString getSelectedPath() const { return selectedPath; }

private:
    void onNewClicked();
    void onRecentClicked(QListWidgetItem* item);
    void onOpenOtherClicked();

    Action chosenAction = Cancel;
    QString selectedPath;
    QListWidget* recentList;
    QStringList recentPaths;
};
