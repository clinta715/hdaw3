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
    titleLabel->setStyleSheet(QString("color: %1;").arg(ThemeColors::accent().name()));
    mainLayout->addWidget(titleLabel);

    auto* subtitleLabel = new QLabel("Holofonic Digital Audio Workstation");
    QFont subFont = subtitleLabel->font();
    subFont.setPointSize(10);
    subtitleLabel->setFont(subFont);
    subtitleLabel->setAlignment(Qt::AlignCenter);
    subtitleLabel->setStyleSheet(QString("color: %1;").arg(ThemeColors::textMuted().name()));
    mainLayout->addWidget(subtitleLabel);
    mainLayout->addSpacing(12);

    // New Project button
    auto* newBtn = new QPushButton("+ New Project");
    newBtn->setFixedHeight(40);
    newBtn->setCursor(Qt::PointingHandCursor);
    newBtn->setStyleSheet(QString(
        "QPushButton { background-color: %1; color: white; border: none; border-radius: 4px; font-size: 14px; font-weight: bold; }"
        "QPushButton:hover { background-color: %2; }"
        "QPushButton:pressed { background-color: %3; }")
        .arg(ThemeColors::accent().name(), ThemeColors::accentBright().name(), ThemeColors::accentDim().name()));
    connect(newBtn, &QPushButton::clicked, this, &StartupDialog::onNewClicked);
    mainLayout->addWidget(newBtn);

    mainLayout->addSpacing(8);

    // Recent projects label
    auto* recentLabel = new QLabel("Recent Projects");
    QFont recentFont = recentLabel->font();
    recentFont.setPointSize(11);
    recentFont.setBold(true);
    recentLabel->setFont(recentFont);
    recentLabel->setStyleSheet(QString("color: %1;").arg(ThemeColors::textPrimary().name()));
    mainLayout->addWidget(recentLabel);

    // Recent projects list
    recentList = new QListWidget(this);
    recentList->setStyleSheet(QString(
        "QListWidget { background-color: %1; border: 1px solid %2; border-radius: 4px; }"
        "QListWidget::item { padding: 8px; color: %3; }"
        "QListWidget::item:hover { background-color: %4; }"
        "QListWidget::item:selected { background-color: %5; color: white; }")
        .arg(ThemeColors::bgWidget().name(), ThemeColors::border().name(),
             ThemeColors::textSecondary().name(), ThemeColors::bgElevated().name(),
             ThemeColors::accent().name()));
    recentList->setCursor(Qt::PointingHandCursor);

    // Populate from QSettings
    auto& settings = PreferencesDialog::settings();
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
    openBtn->setStyleSheet(QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; font-size: 13px; }"
        "QPushButton:hover { background-color: %4; }"
        "QPushButton:pressed { background-color: %5; }")
        .arg(ThemeColors::bgWidget().name(), ThemeColors::textSecondary().name(),
             ThemeColors::borderLight().name(), ThemeColors::bgElevated().name(),
             ThemeColors::bgPanel().name()));
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
