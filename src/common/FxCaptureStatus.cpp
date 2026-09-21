#include "FxCaptureStatus.h"

#include "../model/ProjectModel.h"   // IDs

namespace HDAW {

FxCaptureStatus readFxCaptureStatus(const juce::ValueTree& slotTree)
{
    FxCaptureStatus s;
    if (!slotTree.isValid())
        return s;
    s.status = QString::fromUtf8(
        slotTree.getProperty(IDs::captureStatus, "none").toString().toRawUTF8());
    s.stateBytes = static_cast<int>(slotTree.getProperty(IDs::captureBytes, 0));
    s.capturedAtMs = static_cast<qint64>(
        static_cast<juce::int64>(slotTree.getProperty(IDs::captureTimeMs, 0)));
    const QString stateB64 = QString::fromUtf8(
        slotTree.getProperty(IDs::pluginState, "").toString().toRawUTF8());
    s.hasPluginState = !stateB64.isEmpty();
    return s;
}

} // namespace HDAW
