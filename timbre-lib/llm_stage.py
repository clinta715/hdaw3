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


# ── Role-aware probe evaluation (synth probe analyzer) ───────────────────────
# Additive: adds ROLE_SYSTEM + build_probe_evidence + run_llm_role without
# touching SYSTEM/USER/build_evidence/close/run_llm above. Role thresholds are
# NOT embedded here — they live in role_targets.py and are never in a prompt.

ROLE_SYSTEM = """You are a sound designer evaluating a synthesized probe against the psytrance production role: {role}.
You only explain the measured evidence provided below. Never override a deterministic role check and never invent instruments, effects, or parameter values that the evidence does not support.
Give 2-3 sentences plus concrete, evidence-backed suggestions based on the measurements."""


def build_probe_evidence(words, tags, captions, role, name=None, plugin=None):
    """Assemble the role-aware evidence block for the probe LLM call."""
    meta = []
    if role:
        meta.append(f"role: {role}")
    if name:
        meta.append(f"patch name: {name}")
    if plugin:
        meta.append(f"plugin: {plugin}")
    ev = [" | ".join(meta)]
    ev.append("DSP measurements -> " + words)
    ev.append("CLAP top caption matches: " + ", ".join(c for c, _ in captions))
    ev.append("Sound-event tags (CLAP zero-shot): " + ", ".join(t for t, _ in tags))
    return "\n".join(ev)


def run_llm_role(evidence, role, model_path, n_ctx=4096, max_tokens=220, temperature=0.4, n_gpu_layers=-1):
    """Role-aware LLM prose. Uses ROLE_SYSTEM (formatted with the role) and the
    caller-supplied evidence; reuses the module-global _LLM cache. Raises
    ImportError when llama_cpp is missing (analyze_probe guards it)."""
    global _LLM
    from llama_cpp import Llama  # raises ImportError when llama_cpp is missing
    if _LLM is None:
        _LLM = Llama(model_path=model_path, n_ctx=n_ctx, n_gpu_layers=n_gpu_layers, verbose=False)
    out = _LLM.create_chat_completion(
        messages=[
            {"role": "system", "content": ROLE_SYSTEM.format(role=role)},
            {"role": "user", "content": evidence},
        ],
        max_tokens=max_tokens, temperature=temperature)
    return out["choices"][0]["message"]["content"].strip()
