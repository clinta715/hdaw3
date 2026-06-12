#pragma once
#include <QDialog>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QSettings>

class PreferencesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PreferencesDialog(QWidget* parent = nullptr);

    static double getDefaultClipDuration();
    static void setDefaultClipDuration(double dur);

    static bool getSnapEnabled();
    static void setSnapEnabled(bool en);

    static int getSnapDivision();
    static void setSnapDivision(int idx);

    static inline constexpr auto kKeyLastProjectDir = "lastProjectDirectory";
    static inline constexpr auto kKeyLastExportDir = "lastExportDirectory";
    static inline constexpr auto kKeyRecentProjects = "recentProjects";
    static inline constexpr auto kKeyWindowGeometry = "windowGeometry";
    static inline constexpr auto kKeyWindowState = "windowState";
    static inline constexpr auto kKeyHorizontalSplitter = "horizontalSplitter";
    static inline constexpr auto kKeyVerticalSplitter = "verticalSplitter";
    static inline constexpr auto kKeyBottomPanelIndex = "bottomPanelIndex";
    static inline constexpr auto kSettingsOrg = "HDAW";
    static inline constexpr auto kSettingsApp = "HDAW";

signals:
    void preferencesApplied();

private slots:
    void onSave();
    void onApply();

private:
    void loadSettings();

    QDoubleSpinBox* clipDurSpinBox;
    QCheckBox* snapCheckBox;
    QComboBox* snapDivisionCombo;
};
