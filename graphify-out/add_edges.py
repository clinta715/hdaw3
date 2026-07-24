import json
from pathlib import Path

graph = json.loads(Path('graphify-out/graph.json').read_text(encoding='utf-8'))

# New cross-community edges to add
new_edges = [
    # Community 0 (Qt/JUCE Pitfalls) → Community 44 (TrackHeaderWidget)
    {
        "source": "concept_qgraphicsview_scroll_pitfall",
        "target": "src_ui_trackheaderwidget_trackheaderwidget",
        "relation": "cross_community_reference",
        "confidence": "INFERRED",
        "confidence_score": 0.85,
        "weight": 1.0,
        "_origin": "manual"
    },
    # Community 1 (Architecture & Frontend) → Community 6 (MCP & Transport)
    {
        "source": "concept_react_frontend",
        "target": "frontend_src_components_audioclipeditor",
        "relation": "cross_community_reference",
        "confidence": "INFERRED",
        "confidence_score": 0.9,
        "weight": 1.0,
        "_origin": "manual"
    },
    # Community 2 (Data Model & State) → Community 10 (ProjectModel)
    {
        "source": "concept_automation_cache_rebuild",
        "target": "src_model_projectmodel_projectmodel",
        "relation": "cross_community_reference",
        "confidence": "INFERRED",
        "confidence_score": 0.9,
        "weight": 1.0,
        "_origin": "manual"
    },
    # Community 3 (Build, Test & Engine) → Community 62 (AudioEngine)
    {
        "source": "src_common_projectcommands_projectcommands",
        "target": "src_engine_audioengine_audioengine",
        "relation": "cross_community_reference",
        "confidence": "INFERRED",
        "confidence_score": 0.95,
        "weight": 1.0,
        "_origin": "manual"
    },
    # Community 4 (GUI-Engine Decoupling) → Community 3 (Build, Test & Engine)
    {
        "source": "concept_gui_engine_decoupling",
        "target": "src_common_projectcommands_projectcommands",
        "relation": "cross_community_reference",
        "confidence": "INFERRED",
        "confidence_score": 0.9,
        "weight": 1.0,
        "_origin": "manual"
    },
    # Community 5 (Realtime Audio Safety) → Community 17 (MainAudioProcessor)
    {
        "source": "concept_realtime_audio_thread_safety",
        "target": "src_engine_mainaudioprocessor_mainaudioprocessor",
        "relation": "cross_community_reference",
        "confidence": "INFERRED",
        "confidence_score": 0.95,
        "weight": 1.0,
        "_origin": "manual"
    },
    # Community 6 (MCP & Transport) → Community 1 (Architecture & Frontend)
    {
        "source": "frontend_src_components_audioclipeditor",
        "target": "concept_react_frontend",
        "relation": "cross_community_reference",
        "confidence": "INFERRED",
        "confidence_score": 0.9,
        "weight": 1.0,
        "_origin": "manual"
    },
    # Community 7 (Lint Sweep) → Community 0 (Qt/JUCE Pitfalls)
    {
        "source": "bitset",
        "target": "concept_qgraphicsview_scroll_pitfall",
        "relation": "cross_community_reference",
        "confidence": "INFERRED",
        "confidence_score": 0.8,
        "weight": 1.0,
        "_origin": "manual"
    },
]

# Add edges to graph
graph['links'].extend(new_edges)

# Write back
Path('graphify-out/graph.json').write_text(json.dumps(graph, indent=2, ensure_ascii=False), encoding='utf-8')

print(f'Added {len(new_edges)} cross-community edges')
print(f'Total edges: {len(graph["links"])}')
