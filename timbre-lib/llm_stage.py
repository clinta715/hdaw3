"""Stage 3: small local LLM fuses descriptors + CLAP captions + tags into prose."""
SYSTEM = """You are a sound designer describing audio clips for a DAW sample browser.
You only describe what the evidence supports — never invent instruments or effects that are not implied by the data.
Your description must be 2-4 sentences, plain and useful for browsing. Include:
1. the dominant character (tonal/noisy, bright/dark, pitch)
2. envelope shape (attack/decay/sustain)
3. texture details (grit, air, roughness, purity)
4. a suggested use in music production (one short clause)
Do NOT hedge with "it might be" or list numbers."""

USER = """MEASURED AUDIO EVIDENCE:
{evidence}

Write the description now."""

def build_evidence(desc, tags, captions):
    ev = []
    ev.append("DSP measurements -> " + (", ".join(desc) if isinstance(desc, (list, tuple)) else desc))
    ev.append("CLAP top caption matches: " + ", ".join(c for c, _ in captions))
    ev.append("Sound-event tags (CLAP zero-shot): " + ", ".join(t for t, _ in tags))
    return "\n".join(ev)

_LLM = None

def close():
    """Explicitly free the loaded GGUF model so llama_cpp's exit-time __del__
    teardown does not run after the C extension modules have been torn down
    (avoids the harmless 'TypeError: NoneType is not callable' noise at exit)."""
    global _LLM
    if _LLM is not None:
        _LLM.close()
        _LLM = None

def run_llm(evidence, model_path, n_ctx=4096, max_tokens=220, temperature=0.4, n_gpu_layers=-1):
    global _LLM
    if _LLM is None:
        from llama_cpp import Llama
        _LLM = Llama(model_path=model_path, n_ctx=n_ctx, n_gpu_layers=n_gpu_layers, verbose=False)
    out = _LLM.create_chat_completion(
        messages=[
            {"role": "system", "content": SYSTEM},
            {"role": "user", "content": USER.format(evidence=evidence)},
        ],
        max_tokens=max_tokens, temperature=temperature)
    return out["choices"][0]["message"]["content"].strip()
