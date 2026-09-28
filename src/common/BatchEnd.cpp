// BatchEnd.cpp — the heavy half of end_batch (see BatchEnd.h for the contract).
//
// INCLUDE ORDER IS LOAD-BEARING: the JUCE/engine headers come FIRST and every Qt
// header comes after them. Windows' rpcndr.h (reached through the Qt headers)
// `#define small char`, and JUCE's juce_PushNotifications.h declares
// `enum BadgeIconType { none, small, large }` — Qt-first order corrupts that
// enum (error C2144 at :310) in any TU that also drags juce_gui_extra. Mirroring
// AudioEngineCommands_Undo.cpp (juce_core before its Qt includes) keeps this TU,
// and the TUs that only see the Qt-light BatchEnd.h, clean.

#include "../engine/AudioEngine.h"

#include "ProjectCommands.h"
#include "RenderAndVerify.h"
#include "RenderToolArgs.h"
#include "BatchEnd.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <algorithm>
#include <cstdint>

namespace HDAW {

namespace {

struct EndBatchArgs
{
    bool hasVerify = false;
    QJsonObject targets;
    QString outputPath;            // empty => a process-unique temp file
};

// `end_batch {verify?: {targets?: object, outputPath?: string}}`.
// The refusals mirror src/mcp/McpSchema.cpp's wording for the nested object — the
// unknown-property pass FIRST (validator order), then the declared-property type
// pass in sorted-key order — so both surfaces speak the validator's exact bytes.
bool parseEndBatchArgs(const QJsonObject& o, EndBatchArgs& out, QString& error)
{
    error.clear();
    if (!s4TypeMatches(o, "verify", "object"))
    {
        error = s4ExpectedType("verify", "object");
        return false;
    }
    if (!o.contains("verify") || o.value("verify").isNull() || o.value("verify").isUndefined())
        return true;   // no verify requested

    const QJsonObject v = o.value("verify").toObject();
    for (auto it = v.begin(); it != v.end(); ++it)
    {
        if (it.key() != QLatin1String("outputPath") && it.key() != QLatin1String("targets"))
        {
            error = QStringLiteral("invalid params: verify.%1: unknown property").arg(it.key());
            return false;
        }
    }
    // Sorted-key order: outputPath < targets.
    if (!s4TypeMatches(v, "outputPath", "string"))
    {
        error = s4ExpectedType("verify.outputPath", "string");
        return false;
    }
    if (!s4TypeMatches(v, "targets", "object"))
    {
        error = s4ExpectedType("verify.targets", "object");
        return false;
    }

    out.hasVerify = true;
    out.outputPath = v.value("outputPath").toString();
    out.targets = v.value("targets").toObject();
    return true;
}

QString endBatchVerifyTempPath()
{
    const juce::File temp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                .getChildFile("hdaw_end_batch_verify_"
                                              + juce::Uuid().toString() + ".wav");
    return QString::fromUtf8(temp.getFullPathName().toRawUTF8());
}

} // namespace

EndBatchResult endBatchAndVerify(AudioEngine& engine, const QJsonObject& args)
{
    EndBatchResult out;

    EndBatchArgs parsed;
    QString argError;
    if (!parseEndBatchArgs(args, parsed, argError))
    {
        out.errorCode = -32602;
        out.error = argError;
        return out;
    }

    // SEAL FIRST. Nothing below can reopen the batch.
    if (!engine.getProjectCommands().endBatch())
    {
        out.errorCode = -32602;
        out.error = QString::fromUtf8(kNoOpenBatchError);
        return out;
    }
    out.ok = true;
    // No `verify` => NO verification key, so the payload is the bare sealed result.
    out.payload = QJsonObject{ { "ok", true }, { "sealed", true } };

    if (!parsed.hasVerify)
        return out;

    const QString outPath = parsed.outputPath.isEmpty() ? endBatchVerifyTempPath()
                                                        : parsed.outputPath;
    const auto r = renderAndVerify(engine, outPath, parsed.targets);
    if (!r.ok)
        // Sealed result + the verification error: the batch is NOT un-sealed.
        out.payload["verificationError"] = r.error;
    else
        out.payload["verification"] = r.payload;   // {wavPath, verdict}
    return out;
}

} // namespace HDAW
