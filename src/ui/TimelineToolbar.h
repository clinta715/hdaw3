#pragma once
#include <QWidget>
#include <QPushButton>
#include <QToolButton>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QTimer>
#include <juce_core/juce_core.h>

namespace HDAW { class PluginManager; }

class TimelineToolbar : public QWidget
{
    Q_OBJECT
public:
    explicit TimelineToolbar(QWidget* parent = nullptr);
    ~TimelineToolbar() override;

public slots:
    void setPlaying(bool playing);
    void setTimecode(const QString& text);
    void setBPM(double bpm);
    void setMetronomeEnabled(bool enabled);
    void setDefaultClipLen(double beats);
    void setSnap(bool enabled);
    void setLoopEnabled(bool enabled);
    void setSnapDivision(int index);
    void setCountInEnabled(bool enabled);
    void setTimeSig(int numerator, int denominator);
    void populateMidiDevices(const QStringList& devices);

signals:
    void addTrackClicked();
    void addTrackWithFX(const juce::String& fxType);
    void addTrackWithPlugin(const juce::String& pluginID, const juce::String& pluginFormat);
    void snapToggleChanged(bool enabled);
    void snapDivisionChanged(int index);
    void zoomInClicked();
    void zoomOutClicked();
    void gridTypeChanged(bool showBeats);
    void loopToggleChanged(bool enabled);
    void followPlayheadChanged(bool enabled);
    void recordClicked();
    void playClicked();
    void stopClicked();
    void rewindClicked();
    void bpmChanged(double bpm);
    void metronomeToggled(bool enabled);
    void countInToggled(bool enabled);
    void timeSigChanged(int numerator, int denominator);
    void midiDeviceChanged(const QString& deviceIdentifier);
    void defaultClipLenChanged(double beats);

public:
    void addTrackPluginMenu(QMenu* parentMenu, HDAW::PluginManager& pluginManager);

private slots:
    void onTimeSigChanged(int index);
    void onMidiDeviceChanged(int index);

private:
    QToolButton* addTrackBtn;
    QPushButton* snapBtn;
    QComboBox* snapCombo;
    QPushButton* zoomInBtn;
    QPushButton* zoomOutBtn;
    QComboBox* gridCombo;
    QPushButton* loopBtn;
    QPushButton* followBtn;
    QPushButton* playBtn;
    QPushButton* stopBtn;
    QPushButton* rewindBtn;
    QPushButton* recordBtn;
    QLabel* timecodeLabel;
    QDoubleSpinBox* bpmSpinBox;
    QComboBox* timeSigCombo;
    QComboBox* midiDeviceCombo;
    QPushButton* metronomeBtn;
    QPushButton* countInBtn;
    QDoubleSpinBox* defaultClipLenSpinBox;
    QMenu* trackMenu;
};
