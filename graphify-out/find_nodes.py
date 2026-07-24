import json
from pathlib import Path

graph = json.loads(Path('graphify-out/graph.json').read_text(encoding='utf-8'))

# Find representative nodes for each community
target_communities = [0, 1, 2, 3, 4, 5, 6, 7, 10, 17, 44, 62]

for node in graph['nodes']:
    comm = node.get('community', -1)
    if comm in target_communities:
        label = node.get('label', '')
        print(f'Community {comm}: id={node["id"]} label={label}')
