#pragma once
#include <QString>
#include "../engine/AudioEngine.h"

class QWidget;

namespace HDAW
{
    bool importMidiFile(AudioEngine& engine, QWidget* parent, const QString& path);
}
