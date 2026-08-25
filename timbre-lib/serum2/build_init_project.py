
"""build_init_project.py — minimal project with 2 bare Serum 2 slots (no state)."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from buildproject import clip_xml
from statecodec import serum2_plugin_id

pid = serum2_plugin_id()
tracks = []
for i in (0, 1):
    tracks.append(
        f'<TRACK name="init{i}" volume="0.8" pan="0.0" isMuted="0" isSoloed="0" '
        f'parentBus="0" color="-1213135" midiChannel="1">\n'
        f'  <CLIP_LIST>\n    {clip_xml(2000 + i, 7000 + i * 100, 8.0)}\n  </CLIP_LIST>\n'
        f'  <FX_CHAIN>\n'
        f'    <FX_SLOT fxType="plugin" pluginID="{pid}" pluginFormat="VST3" name="init{i}" bypassed="0"/>\n'
        f'  </FX_CHAIN>\n'
        f'  <AUTOMATION_LIST/>\n</TRACK>'
    )
doc = (
    '<?xml version="1.0" encoding="UTF-8"?>\n\n'
    '<PROJECT name="Serum2 Init Template" tempo="120.0">\n'
    '  <TRANSPORT position="0.0" isPlaying="0" loopStart="0.0" loopEnd="8.0" isLooping="0" '
    'timeSigNumerator="4" timeSigDenominator="4"/>\n'
    '  <TRACK_LIST>\n    ' + "\n    ".join(tracks) + "\n  </TRACK_LIST>\n"
    '  <SCALE_INFO scaleRoot="0" scaleMode="0"/>\n'
    '  <TEMPO_POINT_LIST>\n    <TEMPO_POINT startTime="0.0" tempo="120.0"/>\n  </TEMPO_POINT_LIST>\n'
    '  <ROUTING_GRAPH>\n    <BUS_LIST>\n      <BUS name="Master" busID="0" busType="master" busTarget="-1" fxType="none"/>\n'
    '    </BUS_LIST>\n  </ROUTING_GRAPH>\n</PROJECT>\n'
)
out = sys.argv[1] if len(sys.argv) > 1 else "serum2/test_projects/serum2_init.hdaw3"
open(out, "w", encoding="utf-8").write(doc)
print("wrote", out, os.path.getsize(out))
