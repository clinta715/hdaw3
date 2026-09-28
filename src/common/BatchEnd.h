#pragma once
// end_batch — the ONE implementation behind the MCP `end_batch` tool and its RPC
// twin `project.endBatch` (S7 of docs/plans/2026-09-28-agent-mechanization.md §7).
//
// This header is deliberately Qt-LIGHT and JUCE-FREE: it exposes only the
// composition entry point and a Qt payload struct. The heavy includes
// (projectCommands / renderAndVerify / the engine) live in BatchEnd.cpp, because
// dragging JUCE's juce_gui_extra into a TU that already included Qt is what
// tripped Windows' `rpcndr.h` `small`->`char` macro on juce_PushNotifications.h.
//
// ORDERING of endBatchAndVerify (stated in both tool descriptions, pinned by test):
//  1. ARGUMENTS parse FIRST, so a schema-invalid `verify` is refused before
//     anything is sealed — the MCP validator pre-empts it on the tool surface, so
//     the route must refuse the same bytes WITHOUT having sealed.
//  2. The batch is SEALED SECOND. "no open batch" is the shared refusal.
//  3. Only then does verification run. A verification FAILURE MUST NOT un-seal the
//     batch: the payload stays {ok:true, sealed:true} PLUS `verificationError`.
//     With no `verify` the payload is exactly {ok:true, sealed:true}.

#include <QJsonObject>
#include <QString>

class AudioEngine;

namespace HDAW {

struct EndBatchResult
{
    bool ok = false;
    int errorCode = -32602;   // the code the route reports (the tool reports text)
    QString error;            // non-empty on failure; byte-identical on both surfaces
    QJsonObject payload;      // {ok, sealed, verification?|verificationError?}
};

// Parse → seal → (optional) render + verdict. Byte-identical on both surfaces.
EndBatchResult endBatchAndVerify(AudioEngine& engine, const QJsonObject& args);

} // namespace HDAW
