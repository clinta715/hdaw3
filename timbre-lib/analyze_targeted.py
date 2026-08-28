#!/usr/bin/env python3
import json, os, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lib_analyze as LA
import llm_stage as LS
B = "/mnt/e/samples/Antinomy Psytrance Sounds Vol.2 WAV MiDi-ARCADiA"
FILES = [
    f"{B}/ANTINOMY_01_Kicks/ANTINOMY_01_Kick.wav",
    f"{B}/ANTINOMY_01_Kicks/ANTINOMY_04_Kick.wav",
    f"{B}/ANTINOMY_02_Bassline/ANTINOMY_05_Offbeat_Bass_140_Bpm/ANTINOMY_01_Offbeat_Bass_140_Bpm_C.wav",
    f"{B}/ANTINOMY_02_Bassline/ANTINOMY_03_Deep_Bass/ANTINOMY_01_Deep_Bass_C.wav",
    f"{B}/ANTINOMY_02_Bassline/ANTINOMY_04_Glide_Bass/ANTINOMY_01_Glide_Bass_C.wav",
    f"{B}/ANTINOMY_03_Bass_Layers/ANTINOMY_05_Bass_Layers_140_Bpm_Rolling/ANTINOMY_01_Bass_Layers_140_Bpm_Rolling_C.wav",
    f"{B}/ANTINOMY_04_Drums/ANTINOMY_04_Hihat_Loops/ANTINOMY_01_Hihat_Loops_Rolling_142_Bpm.wav",
    f"{B}/ANTINOMY_04_Drums/ANTINOMY_04_Hihat_Loops/ANTINOMY_05_Hihat_Loops_Offbeat_142_Bpm.wav",
    f"{B}/ANTINOMY_04_Drums/ANTINOMY_06_Rhythm Loops/ANTINOMY_06_Rhythm Loops_Perc_142_Bpm.wav",
    f"{B}/ANTINOMY_04_Drums/ANTINOMY_06_Rhythm Loops/ANTINOMY_04_Rhythm Loops_Cinematic_142_Bpm.wav",
    f"{B}/ANTINOMY_04_Drums/ANTINOMY_03_Clap_&_Snare/ANTINOMY_01_Clap.wav",
    f"{B}/ANTINOMY_04_Drums/ANTINOMY_05_Crash_&_Impact/ANTINOMY_01_Crash_&_Impact_Crash.wav",
    f"{B}/ANTINOMY_08_One_Shot/ANTINOMY_01_One_Shot_Stab_Full_Octave/ANTINOMY_01_One_Shot_Stab_C.wav",
    f"{B}/ANTINOMY_06_Atmosphere/ANTINOMY_01_Atmosphere_Texture_C.wav",
    f"{B}/ANTINOMY_05_Tonal_Reverse/ANTINOMY_01_Tonal_Reverse_C.wav",
    f"{B}/ANTINOMY_09_Effects_&_Noise/ANTINOMY_01_Effects_&_Noise_Riser.wav",
]
t0 = time.perf_counter()
try:
    recs = LA.run_all(FILES, use_llm=True, sidecars=True)
finally:
    LS.close()
errs = {k: v for k, v in recs.items() if "error" in v}
print(f"done: {len(recs)} records, {len(errs)} errors, {time.perf_counter()-t0:.0f}s")
if errs: json.dump(errs, open("/tmp/antinomy_errors.json", "w"), indent=1)
