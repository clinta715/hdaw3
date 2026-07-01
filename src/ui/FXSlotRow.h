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
#include <juce_audio_processors/juce_audio_processors.h>
#include "../model/ProjectModel.h"

class AudioEngine;
class QTimer;

class FXSlotRow : public QWidget
{
    Q_OBJECT
public:
    FXSlotRow(juce::ValueTree slotTree, int index, int trackIdx, AudioEngine& engine, QWidget* parent = nullptr);
    ~FXSlotRow() override;

    int getSlotIndex() const { return slotIndex; }
    void setSlotIndex(int idx) { slotIndex = idx; }

signals:
    void removeRequested(int index);
    void moveUpRequested(int index);
    void moveDownRequested(int index);
    void slotChanged();
    void editRequested(int index);

private:
    void populateTypeCombo(const QString& filter = QString());
    void rebuildParamUI();
    void onTypeChanged(const juce::String& type);
    void pollParamUpdates();

    juce::ValueTree slotTree;
    int slotIndex;
    int trackIndex;
    AudioEngine& engine;
    QLineEdit* filterEdit;
    QComboBox* typeCombo;
    QPushButton* bypassBtn;
    QPushButton* editBtn;
    QPushButton* paramsBtn;
    QWidget* paramContainer;
    bool bypassed = false;

    // Listens to plugin param changes from the audio thread and forwards to Qt main thread
    struct ParamListener : public juce::AudioProcessorListener {
        std::function<void(int,float)> onChanged;
        void audioProcessorParameterChanged(juce::AudioProcessor*, int idx, float v) override {
            if (onChanged) onChanged(idx, v);
        }
        void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) override {}
        void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int) override {}
        void audioProcessorChanged(juce::AudioProcessor*, const juce::AudioProcessorListener::ChangeDetails&) override {}
    };
    std::unique_ptr<ParamListener> paramListener;
    juce::AudioPluginInstance* registeredInstance = nullptr;

    // Lock-free SPSC bridge for audio-thread param changes → UI timer.
    // Producer: AudioProcessorListener (audio thread). Consumer: pollTimer (UI thread).
    // No allocation, no locks on the audio path (mirrors the LevelMeter atomic pattern).
    struct ParamUpdateRing {
        static constexpr size_t capacity = 256;
        struct Entry { int idx = 0; float value = 0.0f; };
        std::array<Entry, capacity> buffer{};
        std::atomic<size_t> head{0}; // consumer read position
        std::atomic<size_t> tail{0}; // producer write position

        void push(int idx, float value) noexcept {
            const size_t t = tail.load(std::memory_order_relaxed);
            const size_t h = head.load(std::memory_order_acquire);
            if (t - h == capacity) return; // full → drop oldest-in-flight; timer still reaches newest
            buffer[t % capacity] = Entry{ idx, value };
            tail.store(t + 1, std::memory_order_release);
        }
        bool pop(Entry& out) noexcept {
            const size_t h = head.load(std::memory_order_relaxed);
            const size_t t = tail.load(std::memory_order_acquire);
            if (h == t) return false; // empty
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
