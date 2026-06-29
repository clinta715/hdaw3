#pragma once
#include <QDialog>
#include <QComboBox>
#include <QSpinBox>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QStackedWidget>
#include <QWidget>
#include "../engine/AudioEngine.h"

class PhraseGeneratorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PhraseGeneratorDialog(AudioEngine& engine, int targetTrackIndex, QWidget* parent = nullptr);
    ~PhraseGeneratorDialog() override;

private slots:
    void onModeChanged(int index);
    void onStyleChanged(int index);
    void onGenerate();

private:
    void updateChordControls();
    void createPhraseControls(QWidget* parent);
    void createChordControls(QWidget* parent);
    void createProgressionControls(QWidget* parent);

    AudioEngine& engine;
    int trackIndex;

    // Common
    QComboBox* rootCombo;
    QComboBox* modeCombo;
    QComboBox* modeTypeCombo; // Phrase / Chord / Progression
    QSpinBox* lowNoteSpin;
    QSpinBox* highNoteSpin;
    QSlider* velocitySlider;
    QLabel* previewLabel;
    QStackedWidget* paramStack;

    // Phrase controls
    QWidget* phrasePage;
    QComboBox* styleCombo;
    QSpinBox* lengthSpin;
    QSpinBox* densitySpin;

    // Chord controls
    QWidget* chordPage;
    QComboBox* chordTypeCombo;
    QComboBox* voicingCombo;
    QComboBox* inversionCombo;
    QCheckBox* arpeggiateChk;
    QDoubleSpinBox* arpRateSpin;
    QDoubleSpinBox* chordDurationSpin;

    // Progression controls
    QWidget* progPage;
    QComboBox* patternCombo;
    QComboBox* chordOverrideCombo;
    QCheckBox* progArpChk;
    QDoubleSpinBox* progArpRateSpin;
    QDoubleSpinBox* progDurSpin;
    QDoubleSpinBox* beatsPerChordSpin;
};
