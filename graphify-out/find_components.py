import json
from pathlib import Path
import networkx as nx

graph_data = json.loads(Path('graphify-out/graph.json').read_text(encoding='utf-8'))
G = nx.Graph()

for node in graph_data['nodes']:
    G.add_node(node['id'], **{k: v for k, v in node.items() if k != 'id'})

for edge in graph_data['edges']:
    G.add_edge(edge['source'], edge['target'], **{k: v for k, v in edge.items() if k not in ('source', 'target')})

# Check for specific components
search_terms = ['AudioClipEditor', 'ClipEditor', 'GainEnvelope', 'Waveform', 'AudioEditor']
print("Searching for specific components:")
for term in search_terms:
    matches = [n for n in G.nodes() if term.lower() in n.lower()]
    if matches:
        for m in matches:
            print(f"  {m} (degree={G.degree(m)}) [src={G.nodes[m].get('source_file', '?')}]")
    else:
        print(f"  {term}: NOT FOUND")
print()

# Show all nodes with degree=0 (truly isolated)
isolated = [n for n in G.nodes() if G.degree(n) == 0]
print(f"Truly isolated nodes (degree=0): {len(isolated)}")
for n in sorted(isolated):
    print(f"  {n} [src={G.nodes[n].get('source_file', '?')}]")
