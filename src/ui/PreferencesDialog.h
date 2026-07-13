#pragma once
#include <QDialog>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QLineEdit>
#include <QSpinBox>
#include <QLabel>
#include <QSettings>

class AudioEngine;

class PreferencesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PreferencesDialog(AudioEngine* engine = nullptr, QWidget* parent = nullptr);

    static QSettings& settings();

    static double getDefaultClipDuration();
    static void setDefaultClipDuration(double dur);

    static bool getSnapEnabled();
    static void setSnapEnabled(bool en);

    static int getSnapDivision();
    static void setSnapDivision(int idx);
    static bool getPianoRollSnapEnabled();
    static void setPianoRollSnapEnabled(bool en);
    static int getPianoRollSnapDivision();
    static void setPianoRollSnapDivision(int idx);

    static QString getDefaultProjectDir();
    static QString getDefaultAudioDir();
    static QString getDefaultMidiDir();

    static inline constexpr auto kKeyLastProjectDir = "lastProjectDirectory";
    static inline constexpr auto kKeyLastExportDir = "lastExportDirectory";
    static inline constexpr auto kKeyRecentProjects = "recentProjects";
    static inline constexpr auto kKeyDefaultProjectDir = "defaultProjectDirectory";
    static inline constexpr auto kKeyDefaultAudioDir = "defaultAudioDirectory";
    static inline constexpr auto kKeyDefaultMidiDir = "defaultMidiDirectory";
    static inline constexpr auto kKeyLastBrowserDir = "lastBrowserDirectory";
    static inline constexpr auto kKeyWindowGeometry = "windowGeometry";
    static inline constexpr auto kKeyWindowState = "windowState";
    static inline constexpr auto kKeyHorizontalSplitter = "horizontalSplitter";
    static inline constexpr auto kKeyVerticalSplitter = "verticalSplitter";
    static inline constexpr auto kKeyBottomPanelIndex = "bottomPanelIndex";
    static inline constexpr auto kKeyMidiDevice = "midiDevice";
    static inline constexpr auto kKeyZoomPps = "zoomPixelsPerSecond";
    static inline constexpr auto kKeyGridType = "gridType";
    static inline constexpr auto kKeyFollowPlayhead = "followPlayhead";
    static inline constexpr auto kKeyCountInBars = "countInBars";
    static inline constexpr auto kKeyPluginScanPaths = "pluginScanPaths";
    static inline constexpr auto kSettingsOrg = "HDAW";
    static inline constexpr auto kSettingsApp = "HDAW";

signals:
    void preferencesApplied();

private slots:
    void onSave();
    void onApply();

private:
    void loadSettings();

    // Audio settings
    AudioEngine* audioEngine = nullptr;
    void buildAudioSettings(class QFormLayout* layout);
    void refreshAudioDevices();
    void refreshSampleRatesAndBufferSizes();
    void applyAudioDeviceType();
    void applyAudioOutputDevice();
    void applyAudioInputDevice();
    void applySampleRate();
    void applyBufferSize();

    QComboBox* deviceTypeCombo = nullptr;
    QComboBox* outputDeviceCombo = nullptr;
    QComboBox* inputDeviceCombo = nullptr;
    QComboBox* sampleRateCombo = nullptr;
    QComboBox* bufferSizeCombo = nullptr;
    QLabel* latencyLabel = nullptr;

    QDoubleSpinBox* clipDurSpinBox;
    QCheckBox* snapCheckBox;
    QComboBox* snapDivisionCombo;

    QLineEdit* mcpHostEdit;
    QSpinBox* mcpPortSpin;
    QCheckBox* mcpAutoStartCheck;

    QSpinBox* countInBarsSpin = nullptr;
};
