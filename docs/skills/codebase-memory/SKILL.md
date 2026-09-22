---
name: codebase-memory
description: REQUIRED for semantic, cross-file, or cross-repository codebase analysis after graphify has handled repository-local structural discovery. Complements graphify; never replaces it.
---

# Codebase Memory

Use codebase-memory as the complementary semantic index for cross-file reasoning,
vocabulary-bridging searches, and cross-repository relationships. Keep `graphify`
as the canonical first tool for repository-local structural dependency and
blast-radius analysis.

## Routing policy

1. Start local structural questions with graphify: use its BFS query for
   dependencies and blast radius, and its path/explain commands for specific
   caller, callee, and definition checks.
2. Use codebase-memory after that for semantic or cross-file discovery,
   relationships that cross repository boundaries, and searches where the
   graphify vocabulary or exact symbol is not known.
3. Do not make codebase-memory replace graphify for local dependency ordering,
   commit-based freshness, or structural blast-radius decisions. When the two
   disagree, prefer verified graphify structure plus direct source inspection;
   treat semantic results as leads until confirmed.

## Tools

- `index_repository`: index this repository (or an explicitly requested target)
  when no usable semantic index exists. Do not request persistence artifacts
  unless the task explicitly requires them.
- `index_status`: check the live index revision and indexing state before
  trusting any codebase-memory result.
- `detect_changes`: inspect changed files and their semantic impact after the
  relevant baseline or commit is known.
- `search_graph`: search definitions and relationships by natural language,
  exact name, file, or semantic vocabulary.
- `trace_path`: trace inbound/outbound calls, data flow, or cross-service paths.
- `query_graph`: run bounded Cypher for multi-hop or aggregate questions; add a
  query limit for broad exploration.
- `get_code_snippet`: read a known symbol after `search_graph` supplies its
  exact qualified name. Confirm important findings against the source tree.

## Freshness contract

Before trusting results, perform both freshness checks:

1. **Graphify:** read `graphify-out/GRAPH_REPORT.md` and compare its
   `Built from commit: <hash>` value with `git rev-parse HEAD`. If it differs,
   refresh graphify with the project-approved incremental update, then repeat
   the comparison before using structural results.
2. **Codebase-memory:** call the live `index_status` tool for this project.
   Treat the semantic index as usable only when its indexed revision/state
   covers the current checkout. If it is stale, missing, indexing, or unclear,
   run `index_repository` (with persistence omitted unless explicitly asked),
   wait for indexing to finish, and call `index_status` again.

After edits, re-check both sources before relying on cached paths or impact
results. The generated `.pi/fabric/mcp-cache.json` is tool-schema/cache
metadata only: its `fetchedAt`, `updatedAt`, `stale`, or stored tool payloads
are **never** evidence that a live codebase-memory index is fresh. Do not edit
that generated cache. If either freshness check cannot be established, report
that limitation and fall back to graphify plus direct source reads rather than
presenting semantic results as authoritative.
