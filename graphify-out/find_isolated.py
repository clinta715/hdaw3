import json
from pathlib import Path
import networkx as nx
from collections import defaultdict

graph_data = json.loads(Path('graphify-out/graph.json').read_text(encoding='utf-8'))
G = nx.Graph()

for node in graph_data['nodes']:
    G.add_node(node['id'], **{k: v for k, v in node.items() if k != 'id'})

for edge in graph_data.get('links', graph_data.get('edges', [])):
    G.add_edge(edge['source'], edge['target'], **{k: v for k, v in edge.items() if k not in ('source', 'target')})

# Find nodes with <=1 connection (effectively isolated / near-isolated)
weak = [n for n in G.nodes() if G.degree(n) <= 1]
print(f'Nodes with <=1 connection: {len(weak)}')
print()

# Group by community
by_comm = defaultdict(list)
for n in weak:
    comm = str(G.nodes[n].get('community', '?'))
    sf = G.nodes[n].get('source_file', '?')
    by_comm[comm].append((n, sf))

for comm in sorted(by_comm.keys(), key=lambda x: (x.isdigit(), x)):
    print(f'Community {comm}:')
    for n, sf in sorted(by_comm[comm]):
        deg = G.degree(n)
        print(f'  {n} (degree={deg}) [src={sf}]')
    print()
