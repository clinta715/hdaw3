#include "StartupDialog.h"
#include "Theme.h"
#include "PreferencesDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QApplication>
#include <QScreen>
#include <QSettings>
#include <QFileInfo>
#include <QStyle>
#include <QFont>

StartupDialog::StartupDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("HDAW - New Session");
    setFixedSize(420, 480);
    setModal(true);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 32, 24, 24);
    mainLayout->setSpacing(16);

    // Title
    auto* titleLabel = new QLabel("HDAW");
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(28);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("color: #88bbff;");
    mainLayout->addWidget(titleLabel);

    auto* subtitleLabel = new QLabel("Holofonic Digital Audio Workstation");
    QFont subFont = subtitleLabel->font();
    subFont.setPointSize(10);
    subtitleLabel->setFont(subFont);
    subtitleLabel->setAlignment(Qt::AlignCenter);
    subtitleLabel->setStyleSheet("color: #888;");
    mainLayout->addWidget(subtitleLabel);
    mainLayout->addSpacing(12);

    // New Project button
    auto* newBtn = new QPushButton("+ New Project");
    newBtn->setFixedHeight(40);
    newBtn->setCursor(Qt::PointingHandCursor);
    newBtn->setStyleSheet(
        "QPushButton { background-color: #2a6fdb; color: white; border: none; border-radius: 4px; font-size: 14px; font-weight: bold; }"
        "QPushButton:hover { background-color: #3a7feb; }"
        "QPushButton:pressed { background-color: #1a5fcb; }"
    );
    connect(newBtn, &QPushButton::clicked, this, &StartupDialog::onNewClicked);
    mainLayout->addWidget(newBtn);

    mainLayout->addSpacing(8);

    // Recent projects label
    auto* recentLabel = new QLabel("Recent Projects");
    QFont recentFont = recentLabel->font();
    recentFont.setPointSize(11);
    recentFont.setBold(true);
    recentLabel->setFont(recentFont);
    recentLabel->setStyleSheet("color: #bbb;");
    mainLayout->addWidget(recentLabel);

    // Recent projects list
    recentList = new QListWidget(this);
    recentList->setStyleSheet(
        "QListWidget { background-color: #252530; border: 1px solid #3a3a45; border-radius: 4px; }"
        "QListWidget::item { padding: 8px; color: #ccc; }"
        "QListWidget::item:hover { background-color: #333040; }"
        "QListWidget::item:selected { background-color: #2a6fdb; color: white; }"
    );
    recentList->setCursor(Qt::PointingHandCursor);

    // Populate from QSettings
    QSettings settings(PreferencesDialog::kSettingsOrg, PreferencesDialog::kSettingsApp);
    recentPaths = settings.value(PreferencesDialog::kKeyRecentProjects).toStringList();
    for (const auto& path : recentPaths)
    {
        QFileInfo fi(path);
        auto* item = new QListWidgetItem(fi.fileName() + "\n" + fi.absolutePath());
        item->setToolTip(path);
        recentList->addItem(item);
    }

    if (recentPaths.isEmpty())
    {
        recentList->addItem("(No recent projects)");
        recentList->item(0)->setFlags(Qt::NoItemFlags);
    }

    connect(recentList, &QListWidget::itemClicked, this, &StartupDialog::onRecentClicked);
    mainLayout->addWidget(recentList);

    // Open Other button
    auto* openBtn = new QPushButton("Open Other...");
    openBtn->setFixedHeight(36);
    openBtn->setCursor(Qt::PointingHandCursor);
    openBtn->setStyleSheet(
        "QPushButton { background-color: #353540; color: #ccc; border: 1px solid #4a4a55; border-radius: 4px; font-size: 13px; }"
        "QPushButton:hover { background-color: #454550; }"
        "QPushButton:pressed { background-color: #2a2a35; }"
    );
    connect(openBtn, &QPushButton::clicked, this, &StartupDialog::onOpenOtherClicked);
    mainLayout->addWidget(openBtn);

    // Center on parent or screen
    if (parent)
    {
        QRect parentGeo = parent->geometry();
        move(parentGeo.center() - rect().center());
    }
    else if (auto* screen = QApplication::primaryScreen())
    {
        QRect screenGeo = screen->availableGeometry();
        move(screenGeo.center() - rect().center());
    }
}

StartupDialog::~StartupDialog() = default;

void StartupDialog::onNewClicked()
{
    chosenAction = NewProject;
    accept();
}

void StartupDialog::onRecentClicked(QListWidgetItem* item)
{
    int row = recentList->row(item);
    if (row >= 0 && row < recentPaths.size())
    {
        chosenAction = OpenRecent;
        selectedPath = recentPaths[row];
        accept();
    }
}

void StartupDialog::onOpenOtherClicked()
{
    chosenAction = OpenOther;
    accept();
}
