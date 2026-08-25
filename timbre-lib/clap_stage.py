"""Stage 2: CLAP zero-shot audio understanding (LAION checkpoints via transformers)."""
import numpy as np
import torch
from transformers import ClapModel, ClapProcessor

_MODEL = None
_PROC = None
_CAP_EMB = None
_LAB_EMB = None
_LAB_FILE = None
_MAX_SEC = 10.0   # CLAP embeds best on <=10s; long files -> first 10s window

def get_model(name="laion/clap-htsat-unfused"):
    global _MODEL, _PROC
    if _MODEL is None:
        _MODEL = ClapModel.from_pretrained(name)
        _PROC = ClapProcessor.from_pretrained(name)
        _MODEL.eval()
        if torch.cuda.is_available():
            _MODEL = _MODEL.to("cuda")
    return _MODEL, _PROC

@torch.inference_mode()
def audio_embed(wav_path_or_array, sr=48000):
    model, proc = get_model()
    if isinstance(wav_path_or_array, str):
        import librosa
        x, sr = librosa.load(wav_path_or_array, sr=48000, mono=True)
    else:
        x = np.asarray(wav_path_or_array, dtype=np.float32)
        if sr != 48000:
            import librosa
            x = librosa.resample(x, orig_sr=sr, target_sr=48000); sr = 48000
    x = x[:int(_MAX_SEC*sr)]
    inputs = proc(audio=x, sampling_rate=sr, return_tensors="pt")
    inputs = {k: v.to(model.device) for k, v in inputs.items()}
    emb = model.get_audio_features(**inputs)
    emb = emb.pooler_output if hasattr(emb, 'pooler_output') else emb
    return emb / (emb.norm(dim=-1, keepdim=True) + 1e-9)

@torch.inference_mode()
def text_embed(texts):
    model, proc = get_model()
    inputs = proc(text=texts, return_tensors="pt", padding=True)
    inputs = {k: v.to(model.device) for k, v in inputs.items()}
    emb = model.get_text_features(**inputs)
    emb = emb.pooler_output if hasattr(emb, 'pooler_output') else emb
    return emb / (emb.norm(dim=-1, keepdim=True) + 1e-9)

def rank_captions(audio_emb, captions, top_k=5):
    global _CAP_EMB
    if _CAP_EMB is None:
        _CAP_EMB = text_embed(captions)
    text_emb = _CAP_EMB
    sims = (audio_emb @ text_emb.T).squeeze(0).cpu().numpy()
    order = np.argsort(-sims)
    return [(captions[i], float(sims[i])) for i in order[:top_k]]

def tag_audioset(audio_emb, label_file, top_k=8, threshold=0.0):
    global _LAB_EMB, _LAB_FILE
    labels = [l.strip() for l in open(label_file, encoding="utf-8") if l.strip()]
    if _LAB_EMB is None or _LAB_FILE != label_file:
        _LAB_EMB = text_embed(labels)
        _LAB_FILE = label_file
    lab_emb = _LAB_EMB
    sims = (audio_emb @ lab_emb.T).squeeze(0).cpu().numpy()
    order = np.argsort(-sims)
    return [(labels[i], float(sims[i])) for i in order[:top_k] if sims[i] > threshold]

_CAPTIONS = [
    "a bright, buzzy sawtooth synth lead", "a warm dark low-passed synth pad",
    "a pure clean sine tone", "a hollow square wave with a reedy character",
    "white noise, airy and hissing", "a dark brown noise rumble",
    "a metallic glassy bell-like tone", "a punchy low kick drum thump",
    "a woody plucked string", "a sizzling high-hat cymbal",
    "distorted gritty electric guitar", "a soft dreamy pad with chorus",
    "a deep sub bass", "a clicky percussive transient", "a breathy flute-like tone",
]
