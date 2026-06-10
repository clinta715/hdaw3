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

private slots:
    void onSave();

private:
    void loadSettings();

    QDoubleSpinBox* clipDurSpinBox;
    QCheckBox* snapCheckBox;
    QComboBox* snapDivisionCombo;
};
