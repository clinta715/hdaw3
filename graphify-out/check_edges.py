import json
from pathlib import Path

graph = json.loads(Path('graphify-out/graph.json').read_text(encoding='utf-8'))
manual = [e for e in graph['links'] if e.get('_origin') == 'manual']
print(f'Manual edges: {len(manual)}')
for e in manual:
    src = e['source']
    tgt = e['target']
    print(f'  {src} --> {tgt}')
