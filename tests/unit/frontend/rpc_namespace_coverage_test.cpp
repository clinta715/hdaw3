// RPC namespace coverage gate — backlog item 2 of
// docs/plans/2026-09-21-rpc-parity-retrofit.md.
//
// The method:: constants in src/frontend/FrontendRpc.h are a hand-maintained list,
// and nothing asserted that each one actually has a branch in
// FrontendRouter.cpp::dispatch. That is how the whole matrix domain stayed
// unreachable over RPC until 2026-09-21 (slice 2 added method::Matrix): a missing
// namespace is invisible — the RPC method just answers "unknown method namespace".
//
// The namespace list comes from frontend::allMethodNamespaces() (derived from the
// constants, single source), so adding a constant without wiring a branch fails
// here rather than in the field.

#include <gtest/gtest.h>

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include "engine/AudioEngine.h"
#include "frontend/FrontendRouter.h"

TEST(RpcNamespaceCoverage, EveryNamespaceHasADispatchBranch) {
    AudioEngine engine;
    engine.initialize();

    QStringList missing;
    QStringList seen;
    for (const char* ns : frontend::allMethodNamespaces()) {
        const QString name(ns);
        seen << name;
        // A deliberately unknown sub-method: a wired namespace answers
        // "unknown <domain> method", an unwired one answers
        // "unknown method namespace". Only the latter is a gap.
        const auto r = frontend::dispatch(engine, name + ".probeNamespaceCoverage",
                                          QJsonObject{});
        const QString msg = r.payload.isObject()
                                ? r.payload.toObject().value("message").toString()
                                : QString();
        if (r.isError && msg.contains("unknown method namespace"))
            missing << name;
    }

    EXPECT_FALSE(seen.isEmpty());
    EXPECT_TRUE(missing.isEmpty())
        << "namespaces with no dispatch branch in FrontendRouter.cpp: "
        << missing.join(", ").toStdString();
}

// The list must stay in sync with the constants: a namespace that exists in
// FrontendRpc.h but is absent from allMethodNamespaces() would silently skip the
// gate above. Assert the two known sentinels are present.
TEST(RpcNamespaceCoverage, ListIncludesTheNamespacesAddedByTheRetrofit) {
    QStringList seen;
    for (const char* ns : frontend::allMethodNamespaces())
        seen << QString(ns);
    EXPECT_TRUE(seen.contains("psy_fm"));   // slice 1
    EXPECT_TRUE(seen.contains("matrix"));   // slice 2
    EXPECT_TRUE(seen.contains("device"));   // slice 0
}
