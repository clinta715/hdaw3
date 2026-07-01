#include "AboutDialog.h"
#include "Theme.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QApplication>
#include <QScreen>
#include <QFont>
#include <QStyle>

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("About HDAW");
    setFixedSize(380, 280);
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 20);
    layout->setSpacing(8);

    auto* title = new QLabel("HDAW");
    QFont tf = title->font();
    tf.setPointSize(22);
    tf.setBold(true);
    title->setFont(tf);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QString("color: %1;").arg(ThemeColors::accent().name()));
    layout->addWidget(title);

    auto* subtitle = new QLabel("Holofonic Digital Audio Workstation");
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setStyleSheet(QString("color: %1; font-size: 11px;").arg(ThemeColors::textMuted().name()));
    layout->addWidget(subtitle);

    layout->addSpacing(12);

    auto* creditLabel = new QLabel(
        "HDAW was created by <b>Clint Anderson</b><br>"
        "(<a href='mailto:clinta@gmail.com' style='color:#f59e0b;'>clinta@gmail.com</a>)<br><br>"
        "Built with the assistance of<br>"
        "<b>DeepSeek</b>, <b>MiniMax</b>, and <b>GLM</b>.");
    creditLabel->setWordWrap(true);
    creditLabel->setAlignment(Qt::AlignCenter);
    creditLabel->setOpenExternalLinks(true);
    creditLabel->setTextFormat(Qt::RichText);
    creditLabel->setStyleSheet(QString("color: %1; font-size: 12px; line-height: 1.5;").arg(ThemeColors::textSecondary().name()));
    layout->addWidget(creditLabel);

    layout->addStretch();

    auto* closeBtn = new QPushButton("OK");
    closeBtn->setFixedHeight(30);
    closeBtn->setFixedWidth(80);
    closeBtn->setStyleSheet(QString(
        "QPushButton { background-color: %1; color: white; border: none; "
        "border-radius: 4px; }"
        "QPushButton:hover { background-color: %2; }")
        .arg(ThemeColors::accent().name(), ThemeColors::accentBright().name()));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    if (parent)
        move(parent->geometry().center() - rect().center());
    else if (auto* screen = QApplication::primaryScreen())
        move(screen->availableGeometry().center() - rect().center());
}

AboutDialog::~AboutDialog() = default;
