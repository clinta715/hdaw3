# Graphify Practice Guide

## What graphify is

A persistent knowledge graph of the entire HDAW codebase: **13,353 nodes** (functions, classes, files, docs) connected by **20,265 edges** (calls, references, imports, defines), grouped into **541 communities**. It lives in `graphify-out/` and survives across sessions.

**The key insight:** graphify gives you *structural answers* — "who calls X", "what lives between A and B", "what would break if I changed Y" — without reading source files. It's a map of the codebase you can navigate instead of grepping.

---

## The five core operations

### 1. `graphify query "<question>"` — BFS/DFS traversal

**What it does:** Finds nodes matching your terms, walks neighbors up to depth 2-3, returns the subgraph.

**When to use:** "How does X work?", "What is connected to Y?", "Trace the flow through Z"

```powershell
graphify query "transport play stop loop" --budget 2000
```

**Critical step — vocab expansion:** graphify matches by case-folded substring. If your question uses different words than the code labels, you get nothing. Before querying:
1. Check the graph vocabulary for matching tokens
2. Use the tokens *the graph actually has*, not synonyms from your head

**BFS vs DFS:**
- **BFS** (default) — "What is X connected to?" — broad context, nearest neighbors first
- **DFS** (`--dfs`) — "How does X reach Y?" — trace a specific chain, go deep before breadth

**Budget:** `--budget N` caps output at ~N tokens. Start at 1500-2000, raise if truncated.

### 2. `graphify path "NodeA" "NodeB"` — shortest path

**What it does:** Finds the shortest route between two concepts across the entire graph.

**When to use:** "How are these two things connected?", "What's between the frontend and the audio engine?"

```powershell
graphify path "TransportManager" "RoutingManager" --undirected
```

**Gotcha:** Ambiguous names match multiple nodes. Use the full node ID when the tool reports ambiguity:
```
src_engine_routingmanager_routingmanager
```

**Directed vs undirected:** Default is directed (follows edge direction). Add `--undirected` when there's no path — it ignores edge direction and often finds connections the directed search misses.

### 3. `graphify explain "NodeName"` — node deep-dive

**What it does:** Everything connected to a single node — all edges, grouped by file, with source locations.

**When to use:** "What does this class do?", "What depends on this function?", "What would I break if I changed this?"

```powershell
graphify explain "src_engine_routingmanager_routingmanager"
```

Returns: degree (connection count), all connections with edge type + confidence + source location.

### 4. `graphify affected "NodeName" --depth N` — blast radius

**What it does:** Reverse traversal — finds everything that *depends on* the given node.

**When to use:** Before making a change — "If I modify X, what breaks?"

```powershell
graphify affected "src_engine_routingmanager_routingmanager" --depth 2
```

Depth 1 = direct dependents. Depth 2 = their dependents. For a risky engine change, depth 2 is usually enough.

### 5. `graphify god-nodes` — architectural hubs

**What it does:** Lists the most connected nodes — the things everything touches.

**When to use:** Onboarding, architecture review, identifying high-risk change targets.

**Current HDAW god nodes (top 10):**
| Rank | Node | Edges |
|------|------|-------|
| 1 | AudioEngineCommands | 229 |
| 2 | ProjectCommands | 185 |
| 3 | ValueTree | 174 |
| 4 | TrackFXSlot | 110 |
| 5 | AudioEngine | 105 |
| 6 | RoutingManager | 95 |
| 7 | Track | 94 |
| 8 | MainAudioProcessor | 93 |
| 9 | ClipSourceProcessor | 102 |
| 10 | ProjectModel | 86 |

These are the nodes where bugs hide and changes propagate widest.

---

## When to use each tool

| Situation | Tool | Example |
|-----------|------|---------|
| Starting work on an unfamiliar area | `query` + `explain` | Understand the module before editing |
| Pre-change blast radius check | `affected` | "I'm changing RoutingManager — what breaks?" |
| Debugging a connection between systems | `path` | "How does the MCP server reach the audio engine?" |
| Onboarding / architecture review | `god-nodes` | See the structural hubs |
| Verifying a refactoring plan | `query` + `affected` | "If I rename X, will graphify still find it?" |
| Post-change validation | `update` | Re-extract changed files, then `affected` again |

---

## The practice workflow

### Before ANY code change (hdaw-guard integration)

```
1. graphify query "<what you're about to touch>"     # understand the area
2. graphify explain "<key node>"                     # see all connections
3. graphify affected "<node you'll modify>" --depth 2  # blast radius
4. Make the change
5. graphify update .                                  # re-extract (no LLM cost)
6. graphify affected "<node>" --depth 1              # verify nothing new broke
```

### After structural changes (new files, new classes, new RPC methods)

```powershell
graphify update .    # incremental re-extract, no API cost
# or for major restructures:
graphify . --update  # full pipeline with update mode
```

### Exploration session (learning the codebase)

```
1. graphify god-nodes                           # find the hubs
2. graphify explain "<hub>"                     # understand what it connects
3. graphify path "<hub A>" "<hub B>"           # see how major systems connect
4. graphify query "<concept>" --budget 3000    # broad exploration
```

---

## Node IDs and ambiguity

graphify matches nodes by case-folded substring. When a name appears in multiple files, you get ambiguity:

```
Ambiguous: 'RoutingManager' matches 2 nodes in different files.
  src/engine/RoutingManager.h
    id: src_engine_routingmanager_routingmanager
  src/engine/MainAudioProcessor.h
    id: src_engine_mainaudioprocessor_mainaudioprocessor_routingmanager
```

**Use the full node ID** (the `id:` field) for disambiguation. The ID format is:
```
<source_file_path_with_underscores>_<node_label_lowercase>
```

---

## Graph freshness

The graph was built from commit `bda6a894` (Aug 29). Check if it's current:

```powershell
git rev-parse HEAD   # compare to built_at_commit in graph.json
```

After code changes, run `graphify update .` — it re-extracts only changed files, costs zero LLM tokens, and keeps the graph current.

---

## Costs

Graphify for HDAW ran with **0 tokens** — it's a pure code corpus, so only AST extraction was used (no LLM needed). Docs/papers/images would use semantic extraction (Gemini or host agent), but HDAW's graph is code-only.

---

## The graph.html visualization

Open `graphify-out/graph.html` in a browser for an interactive force-directed graph. Communities are color-coded, nodes are sized by degree. Useful for getting a spatial sense of the architecture — who clusters with whom.

---

## Key communities in HDAW

| Community | Nodes | What it represents |
|-----------|-------|--------------------|
| AudioEngineCommands | 197 | All engine command handlers (RPC → engine) |
| ProjectCommands | 177 | Project-level mutation commands |
| TrackFXSlot | 95 | FX chain per track |
| PluginHost / PluginProxySlot | 89 + 88 | Plugin hosting (isolated process model) |
| RoutingManager | 76 | Audio graph construction |
| ProjectModel | 86 | The ValueTree project model |
| MainAudioProcessor | 84 | Top-level audio processor |
| ClipSourceProcessor | 80 | Clip audio source |
| TransportManager | 67 | Transport state machine |
| dispatchComposition | 88 | Frontend command dispatch |
| FileBrowser.tsx | 94 | File browser UI |
| FmSynthEngine | 75 | FM synthesis engine |

---

## Practice rules

1. **Always expand your query against the graph vocabulary** before traversing. The graph has specific labels; your question uses natural language. Bridge the gap.
2. **Use full node IDs when ambiguous.** Don't guess — the tool tells you the exact ID.
3. **Run `affected` before touching high-degree nodes.** The god-nodes list is your risk ranking.
4. **`update` after code changes.** A stale graph gives stale answers. Zero cost to keep it current.
5. **Save results back** with `graphify save-result` when you get a good answer — it improves future queries.
6. **Start sessions with `god-nodes`** to reorient — the hub list is the quickest architecture summary.
