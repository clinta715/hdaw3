#pragma once
#include <QWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QSlider>
#include <QLabel>
#include <vector>
#include <memory>
#include <array>
#include <atomic>
#include <cstddef>
#include <juce_data_structures/juce_data_structures.h>
#include "../common/ProjectCommands.h"
#include "../common/PluginService.h"
#include "../common/PluginParamService.h"

class AudioEngine;
class QTimer;

class FXSlotRow : public QWidget
{
    Q_OBJECT
public:
    FXSlotRow(juce::ValueTree slotTree, int index, int trackIdx,
              PluginParamService* paramSvc, PluginService* plugSvc,
              ProjectCommands* cmds, QWidget* parent = nullptr);
    ~FXSlotRow() override;

    int getSlotIndex() const { return slotIndex; }
    void setSlotIndex(int idx) { slotIndex = idx; }

    void cleanup();

signals:
    void removeRequested(int index);
    void moveUpRequested(int index);
    void moveDownRequested(int index);
    void slotChanged();
    void editRequested(int index);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    QPoint dragStartPos;
    void startDrag();

    void populateTypeCombo(const QString& filter = QString());
    void rebuildParamUI();
    void onTypeChanged(const juce::String& type);
    void pollParamUpdates();

    juce::ValueTree slotTree;
    int slotIndex;
    int trackIndex;
    PluginParamService* paramService = nullptr;
    PluginService* pluginService = nullptr;
    ProjectCommands* projectCmds = nullptr;
    QLineEdit* filterEdit;
    QComboBox* typeCombo;
    QPushButton* bypassBtn;
    QPushButton* editBtn;
    QPushButton* paramsBtn;
    QWidget* paramContainer;
    bool bypassed = false;

    // Lock-free SPSC bridge for audio-thread param changes → UI timer.
    // Producer: PluginParamService callback (audio thread). Consumer: pollTimer (UI thread).
    struct ParamUpdateRing {
        static constexpr size_t capacity = 256;
        struct Entry { int idx = 0; float value = 0.0f; };
        std::array<Entry, capacity> buffer{};
        std::atomic<size_t> head{0};
        std::atomic<size_t> tail{0};

        void push(int idx, float value) noexcept {
            const size_t t = tail.load(std::memory_order_relaxed);
            const size_t h = head.load(std::memory_order_acquire);
            if (t - h == capacity) return;
            buffer[t % capacity] = Entry{ idx, value };
            tail.store(t + 1, std::memory_order_release);
        }
        bool pop(Entry& out) noexcept {
            const size_t h = head.load(std::memory_order_relaxed);
            const size_t t = tail.load(std::memory_order_acquire);
            if (h == t) return false;
            out = buffer[h % capacity];
            head.store(h + 1, std::memory_order_release);
            return true;
        }
        void clear() noexcept {
            head.store(tail.load(std::memory_order_relaxed), std::memory_order_relaxed);
        }
    };
    std::unique_ptr<ParamUpdateRing> paramRing;
    QTimer* pollTimer = nullptr;

    struct ParamSlider {
        QSlider* slider;
        QLabel* valueLabel;
        int paramIdx;
    };
    std::vector<ParamSlider> paramSliders;
};
