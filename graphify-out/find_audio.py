import json
from pathlib import Path
import networkx as nx

graph_data = json.loads(Path('graphify-out/graph.json').read_text(encoding='utf-8'))
G = nx.Graph()

for node in graph_data['nodes']:
    G.add_node(node['id'], **{k: v for k, v in node.items() if k != 'id'})

for edge in graph_data['edges']:
    G.add_edge(edge['source'], edge['target'], **{k: v for k, v in edge.items() if k not in ('source', 'target')})

# Find audio-related nodes
audio_keywords = ['audio', 'clip', 'gain', 'envelope', 'timestretch', 'waveform', 'editor']
audio_nodes = []
for n in G.nodes():
    n_lower = n.lower()
    if any(kw in n_lower for kw in audio_keywords):
        deg = G.degree(n)
        sf = G.nodes[n].get('source_file', '?')
        comm = G.nodes[n].get('community', '?')
        audio_nodes.append((n, deg, sf, comm))

print(f'Audio-related nodes: {len(audio_nodes)}')
print()
for n, deg, sf, comm in sorted(audio_nodes, key=lambda x: -x[1]):
    print(f'  {n} (degree={deg}) [src={sf} community={comm}]')
