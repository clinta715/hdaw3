// RPC-parity ratchet — backlog item 3 of docs/plans/2026-09-21-rpc-parity-retrofit.md.
//
// The standing AGENTS.md rule is "every MCP tool must also be reachable as namespace.method
// over the frontend JSON-RPC surface". Nothing enforced it, which is how the whole matrix
// and tuning domains stayed MCP-only until the retrofit.
//
// The mapping is SEMANTIC (rave_get_config -> settings.getRaveConfig, pool_list ->
// pool.list), so no name-only check can prove parity. What this gate DOES guarantee:
//   1. every LIVE MCP tool has a ledger row — a new tool cannot be added without being
//      classified (regenerate with `node tools/rpc_parity_map.mjs` and review the diff);
//   2. every ledger row still corresponds to a live tool (no stale rows);
//   3. every mapped RPC target RESOLVES on the live dispatch surface — a rename/removal
//      fails here instead of silently orphaning the tool;
//   4. every mcp-only / unresolved row carries a review reason.
// It does NOT prove semantic equivalence and it is not a substitute for the per-domain
// audit: it makes drift impossible to introduce QUIETLY. `unresolved` rows are an explicit
// review queue, and their count is printed on every run.
//
// The ledger is generated (tools/rpc_parity_map.mjs) and included as a raw string literal,
// so the test needs no runtime path resolution.

#include <gtest/gtest.h>

#include "engine/AudioEngine.h"
#include "frontend/FrontendRouter.h"
#include "mcp/McpJsonRpc.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <iostream>
#include <map>
#include <memory>
#include <string>

namespace {
#include "rpc_parity_map.inc"
}

namespace {

struct Row
{
    QString tool;
    QString status;   // mapped | mcp-only | unresolved
    QString rpc;      // "ns.method", or "-" when there is no route
    QString note;
};

std::map<QString, Row> parseLedger()
{
    std::map<QString, Row> rows;
    const QString text = QString::fromUtf8(kRpcParityMap);
    for (const QString& line : text.split('\n'))
    {
        const QString t = line.trimmed();
        if (t.isEmpty() || t.startsWith("//"))
            continue;
        const QStringList f = t.split('\t');
        if (f.size() < 3)
            continue;
        Row r;
        r.tool = f[0].trimmed();
        r.status = f[1].trimmed();
        r.rpc = f[2].trimmed();
        r.note = f.size() > 3 ? f[3].trimmed() : QString();
        if (!r.tool.isEmpty())
            rows[r.tool] = r;
    }
    return rows;
}

class RpcParityRatchet : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        server = std::make_unique<mcp::McpServer>();
        server->setEngine(engine.get());
        mcp::registerAllTools(*server);
        rows = parseLedger();
    }

    // The authoritative tool list: the live registry, not the source scan that generated
    // the ledger.
    QStringList liveTools() {
        const auto resp = server->handleRequestOnTestThread(1, "tools/list", QJsonObject{}).toObject();
        QStringList out;
        for (const auto& v : resp.value("tools").toArray())
            out << v.toObject().value("name").toString();
        return out;
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;
    std::map<QString, Row> rows;
};

// G1 (the ratchet): a new MCP tool must be classified before it can land.
TEST_F(RpcParityRatchet, EveryLiveToolIsClassified)
{
    const QStringList live = liveTools();
    ASSERT_GT(live.size(), 200) << "the MCP surface should be registered";

    QStringList missing;
    for (const QString& t : live)
        if (!rows.count(t))
            missing << t;
    EXPECT_TRUE(missing.isEmpty())
        << "MCP tools with no ledger row: " << missing.join(", ").toStdString()
        << " — regenerate with `node tools/rpc_parity_map.mjs` and classify them";
}

// G2: the ledger must not outlive the tools it describes.
TEST_F(RpcParityRatchet, NoStaleLedgerRows)
{
    const QStringList live = liveTools();
    QStringList stale;
    for (const auto& kv : rows)
        if (!live.contains(kv.first))
            stale << kv.first;
    EXPECT_TRUE(stale.isEmpty())
        << "ledger rows for tools that no longer exist: " << stale.join(", ").toStdString();
}

// G3: every mapped target must exist on the LIVE dispatch surface. Probing with empty
// params is enough: a real method answers with a validation error (-32602) or its own
// result, while a missing one answers "unknown method namespace" / "unknown <domain> method".
TEST_F(RpcParityRatchet, MappedTargetsResolveOnTheDispatchSurface)
{
    int probed = 0;
    QStringList orphaned;
    for (const auto& kv : rows)
    {
        const Row& r = kv.second;
        if (r.status != "mapped" || r.rpc.isEmpty() || r.rpc == "-")
            continue;
        ++probed;
        const auto res = frontend::dispatch(*engine, r.rpc, QJsonObject{});
        if (!res.isError)
            continue;   // the method ran and was happy with no params
        const QString msg = res.payload.isObject()
                                ? res.payload.toObject().value("message").toString()
                                : QString();
        if (msg.contains("unknown method namespace") || msg.contains("unknown "))
            if (msg.contains("method"))
                orphaned << (r.tool + " -> " + r.rpc);
    }
    EXPECT_GT(probed, 100) << "the ledger should carry a substantial mapped set";
    EXPECT_TRUE(orphaned.isEmpty())
        << "ledger rows mapping to routes that do not resolve: "
        << orphaned.join(", ").toStdString();
}

// G4: a row with no route must say why, and a row with a route must not carry a status we
// do not understand.
TEST_F(RpcParityRatchet, EveryNonMappedRowHasAReason)
{
    QStringList bad;
    for (const auto& kv : rows)
    {
        const Row& r = kv.second;
        const bool known = (r.status == "mapped" || r.status == "mcp-only" || r.status == "unresolved");
        if (!known)
            bad << (r.tool + " (unknown status '" + r.status + "')");
        else if (r.status != "mapped" && r.note.isEmpty())
            bad << (r.tool + " (" + r.status + " with no review note)");
        else if (r.status == "mapped" && (r.rpc.isEmpty() || r.rpc == "-"))
            bad << (r.tool + " (mapped but no target)");
    }
    EXPECT_TRUE(bad.isEmpty())
        << "ledger rows needing attention: " << bad.join(", ").toStdString();
}

// G5: surface the review queue. This is a REPORT, not a gate: the unresolved set is
// expected to shrink as the semantic audit continues, and it must stay visible so nobody
// mistakes "the ratchet is green" for "every tool has a verified route".
TEST_F(RpcParityRatchet, ReviewQueueIsReported)
{
    int mapped = 0, mcpOnly = 0, unresolved = 0;
    QStringList unresolvedNames;
    for (const auto& kv : rows)
    {
        const Row& r = kv.second;
        if (r.status == "mapped") ++mapped;
        else if (r.status == "mcp-only") ++mcpOnly;
        else if (r.status == "unresolved") { ++unresolved; unresolvedNames << r.tool; }
    }
    std::cout << "[RpcParityRatchet] ledger rows=" << rows.size()
              << " mapped=" << mapped << " mcp-only=" << mcpOnly
              << " unresolved(review queue)=" << unresolved << "\n";
    EXPECT_GT(mapped, 0);
    EXPECT_EQ(mapped + mcpOnly + unresolved, static_cast<int>(rows.size()));
}

} // namespace
