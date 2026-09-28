#pragma once
// The ONE JSON-integer predicate, shared by the MCP validator mirror
// (common/BatchEditJson.h's parseIntArg / parseIntArray) and the frontend
// router's argument helpers (frontend/router/RouterHelpers.h's requireInt /
// refArgs / optInt, mcp/McpArgs.h's refArgs).
//
// WHY a separate, Qt-only header: the predicate is mcp::validateSchema's
// integrality test (McpSchema.cpp's typeMatches for an `{"type":"integer"}`
// property), and every surface must answer a numeric-but-non-integral value
// with the SAME refusal text. It used to be spelled once in BatchEditJson.h
// and once inline in RouterHelpers.h (to keep that widely-included header free
// of the heavy ProjectCommands.h include BatchEditJson.h drags in). Hoisting it
// here gives one definition without that include cost — no second rule.
#include <QJsonValue>

namespace HDAW {

// mcp::validateSchema's integer test, mirrored: a JSON number whose double
// value is exactly an integer. The qint64 round-trip is what makes it a
// MIRROR (not just "x == floor(x)"): a value outside the qint64 range is not
// an integer argument here, exactly as the validator would answer.
inline bool isJsonInteger(const QJsonValue& v)
{
    return v.isDouble()
        && v.toDouble() == static_cast<double>(static_cast<qint64>(v.toDouble()));
}

} // namespace HDAW
