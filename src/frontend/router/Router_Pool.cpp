#include "Router_Pool.h"
#include "RouterHelpers.h"
#include "../../common/ReadModel.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QString>

#include <juce_audio_formats/juce_audio_formats.h>

#include <map>
#include <string>

using namespace frontend::router_helpers;

namespace frontend {

DispatchResult dispatchPool(ReadModel& readModel, juce::AudioFormatManager& formatManager,
                            const QString& m, const QJsonValue& /*params*/) {
    if (m == "list") {
        auto snapshot = readModel.snapshot();

        struct PoolEntry {
            std::string sourceFile;
            std::string name;
            int usageCount = 0;
            double duration = 0.0;
            int sampleRate = 0;
            int channels = 0;
        };
        std::map<std::string, PoolEntry> poolMap;

        for (const auto& clip : snapshot.clips) {
            if (clip.sourceFile.empty()) continue;
            auto it = poolMap.find(clip.sourceFile);
            if (it == poolMap.end()) {
                PoolEntry entry;
                entry.sourceFile = clip.sourceFile;
                entry.name = clip.name;
                entry.usageCount = 1;

                juce::File file(clip.sourceFile);
                if (file.existsAsFile()) {
                    std::unique_ptr<juce::AudioFormatReader> reader(
                        formatManager.createReaderFor(file));
                    if (reader) {
                        entry.duration = reader->lengthInSamples / reader->sampleRate;
                        entry.sampleRate = static_cast<int>(reader->sampleRate);
                        entry.channels = static_cast<int>(reader->numChannels);
                    }
                }
                poolMap[clip.sourceFile] = std::move(entry);
            } else {
                it->second.usageCount++;
            }
        }

        QJsonArray arr;
        for (const auto& [path, entry] : poolMap) {
            (void)path;
            arr.append(QJsonObject{
                {"sourceFile",  QString::fromStdString(entry.sourceFile)},
                {"name",        QString::fromStdString(entry.name)},
                {"usageCount",  entry.usageCount},
                {"duration",    entry.duration},
                {"sampleRate",  entry.sampleRate},
                {"channels",    entry.channels}
            });
        }
        return {false, arr};
    }

    if (m == "cleanup") {
        auto snapshot = readModel.snapshot();
        std::map<std::string, int> usageCounts;
        for (const auto& clip : snapshot.clips) {
            if (clip.sourceFile.empty()) continue;
            usageCounts[clip.sourceFile]++;
        }

        QJsonArray missing;
        for (const auto& [path, count] : usageCounts) {
            (void)count;
            juce::File file(path);
            if (!file.existsAsFile())
                missing.append(QString::fromStdString(path));
        }
        return {false, missing};
    }

    return makeError(-32601, "unknown pool method: " + m);
}

} // namespace frontend
