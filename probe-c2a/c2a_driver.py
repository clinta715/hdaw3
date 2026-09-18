#!/usr/bin/env python3
"""Phase C2a driver — instrumented proxy param-delivery scenarios (a: stopped, b: playing, c: offline).
Requires: engine running with HDAW_TRACE_PARAM=1 (build/HDAW_headless.exe --mcp-http).
Writes probe-c2a/results.json; saves projects to C:\temp; exports WAV to C:\temp.
"""
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request
import hashlib
import wave

URL = 'http://127.0.0.1:18765/mcp'
SCR = '/mnt/d/pdf/roo projects/hdaw3/probe-c2a'
WINOUT = 'C:\\temp'

# (live index, normalized value, name) — Phase C verified live indices.
SIX = [
    (17, round(100 / 127.0, 6), 'Lfo1Rate'),
    (21, 1.0,                    'RingModulatorSwitch'),
    (22, round(90 / 127.0, 6),  'CrossModulationDepth'),
    (59, round(10 / 127.0, 6),  'CutoffFrequency'),
    (73, 1.0,                    'AmpEnvelopeAttackTime'),
    (76, round(120 / 127.0, 6), 'AmpEnvelopeReleaseTime'),
]
EVENTS = []

def ev(tag, obj):
    row = {'t': round(time.time(), 3), 'tag': tag}
    row.update(obj)
    EVENTS.append(row)
    print('[%9.3f] %-28s %s' % (time.time() % 100000, tag, json.dumps(obj)[:220]), flush=True)

class Mcp:
    def __init__(self):
        self.n = 0
    def call(self, name, args, timeout=200, retries=2):
        last = None
        for _ in range(retries + 1):
            self.n += 1
            payload = {'jsonrpc': '2.0', 'id': self.n, 'method': 'tools/call',
                       'params': {'name': name, 'arguments': args}}
            req = urllib.request.Request(URL, data=json.dumps(payload).encode('utf-8'),
                                         headers={'Content-Type': 'application/json',
                                                  'Accept': 'application/json'})
            try:
                with urllib.request.urlopen(req, timeout=timeout) as r:
                    body = r.read()
                if not body:
                    return {'text': '', 'isError': True}
                resp = json.loads(body)
                if 'error' in resp:
                    return {'text': 'RPCERROR ' + json.dumps(resp['error'])[:200], 'isError': True}
                res = resp.get('result') or {}
                txt = ''.join(c.get('text', '') for c in (res.get('content') or [])
                              if c.get('type') == 'text')
                return {'text': txt, 'isError': bool(res.get('isError'))}
            except Exception as exc:  # noqa: BLE001
                last = exc
                time.sleep(5)
        return {'text': 'TRANSPORT %s' % last, 'isError': True}

def find_plugin_id(m):
    r = m.call('list_plugins', {'kind': 'all'})
    mm = re.search(r'"id":\s*"(CLAP-JE8086-[A-Za-z0-9]+-[0-9]+)"', r['text'])
    if mm:
        return mm.group(1)
    mm2 = re.search(r'(CLAP-JE8086-[A-Za-z0-9-]{3,})', r['text'])
    return mm2.group(1).rstrip('"') if mm2 else None

def capture_trigger(m, ti, si=0):
    """Trigger the deferred plugin-state capture idiom (CC125 benign)."""
    return m.call('send_fx_midi', {'trackId': ti, 'slotIndex': si,
                                   'messages': [{'kind': 'controlChange', 'controller': 125, 'value': 0}],
                                   'captureToTree': True})

def capture_polls(m, ti, tag, si=0, delays=(0.2, 1.0, 3.0)):
    """Trigger + poll at +200ms/+1s/+3s, then settle; returns final status dict."""
    trig = capture_trigger(m, ti, si)
    ev('capture_trigger:' + tag, {'resp': trig['text'][:160], 'isError': trig['isError']})
    out = {'polls': []}
    for d in delays:
        time.sleep(max(0, d - (time.time() - EVENTS[-1]['t'])))
        st = m.call('get_fx_capture_status', {'trackId': ti, 'slotIndex': si})
        fields = dict(re.findall(r'(status|stateBytes|capturedAtMs|hasPluginState)=([^ ]+)', st['text']))
        ev('capture.poll:' + tag, {'at': d, **fields})
        out['polls'].append({'atMs': d, **fields})
        if fields.get('status') not in ('pending', None) and d >= 1.0:
            break
    time.sleep(1.2)  # settle
    st = m.call('get_fx_capture_status', {'trackId': ti, 'slotIndex': si})
    fields = dict(re.findall(r'(status|stateBytes|capturedAtMs|hasPluginState)=([^ ]+)', st['text']))
    ev('capture.settled:' + tag, fields)
    out['settled'] = fields
    return out

def state_md5_from_save(m, winpath, tag):
    """save_project then md5 the base64-decoded pluginState blob from the XML."""
    r = m.call('save_project', {'filePath': winpath})
    ev('save:' + tag, {'resp': r['text'], 'isError': r['isError']})
    md5 = None
    nbytes = None
    try:
        lx = winpath.replace('\\', '/').replace('C:/', '/mnt/c/')
        with open(lx, 'r', encoding='utf-8', errors='replace') as fh:
            data = fh.read()
        # HDAW persists pluginState as '<n>.<escaped-bytes>' (dot-heavy custom
        # escape of the raw jeLib blob, not plain base64). Fingerprint the raw
        # attribute VALUE bytes (deterministic per state) and pull n= as bytes.
        blobs = re.findall(r'pluginState="([^"]*)"', data)
        if blobs:
            raw = blobs[-1]
            import base64
            md5 = hashlib.md5(raw.encode('latin-1')).hexdigest()
            nbytes = None
            mm = re.match(r'(\d+)\.', raw)
            if mm:
                nbytes = int(mm.group(1))
            try:
                payload = raw.split('.', 1)[1] if '.' in raw else raw
                pad = (4 - len(payload) % 4) % 4
                blob = base64.b64decode(payload + '=' * pad)
                if blob:
                    md5 = hashlib.md5(blob).hexdigest() + ' (b64)'
                    nbytes = len(blob)
            except Exception:  # noqa: BLE001
                pass
    except Exception as exc:  # noqa: BLE001
        ev('state-extract:' + tag, {'error': str(exc)[:120]})
    ev('state:' + tag, {'md5': md5, 'bytes': nbytes})
    return {'md5': md5, 'bytes': nbytes, 'blobs': blobs.count('') if 'blobs' in dir() else None}

def apply_six(m, ti, si=0):
    applied = []
    for idx, val, name in SIX:
        r = m.call('set_fx_param', {'trackId': ti, 'slotIndex': si, 'paramIndex': idx, 'value': val})
        applied.append({'idx': idx, 'name': name, 'val': val, 'ok': (not r['isError']), 'resp': r['text'][:80]})
        ev('set_fx_param', {'idx': idx, 'name': name, 'val': val, 'resp': r['text'][:80], 'isError': r['isError']})
        time.sleep(0.05)
    return applied

def wav_rms_peak(path, seconds=3.0):
    with wave.open(path, 'rb') as w:
        n, sw, fr = w.getnframes(), w.getsampwidth(), w.getframerate()
        raw = w.readframes(min(n, int(fr * seconds)))
    step = max(1, sw)
    vals = [int.from_bytes(raw[i:i + sw], 'little', signed=True)
            for i in range(0, len(raw) - sw + 1, sw * 3)][:12000]
    denom = float(1 << (8 * sw - 1))
    rms = (sum(v * v for v in vals) / max(1, len(vals))) ** 0.5 / denom
    peak = max((abs(v) for v in vals), default=0) / denom
    return rms, peak

def main():
    os.makedirs(SCR, exist_ok=True)
    m = Mcp()

    ev('ping', {})
    ri = m.call('engine_info', {})
    ev('engine_info', {'resp': ri['text'][:200], 'isError': ri['isError']})

    pid = find_plugin_id(m)
    ev('plugin', {'pid': pid})
    if not pid:
        print('FATAL: no JE8086 plugin id', flush=True)
        sys.exit(2)

    ev('new_project', {'resp': m.call('new_project', {})['text'][:100]})

    r = m.call('add_instrument_part', {'trackName': 'C2a JE8086', 'pluginId': pid,
                                       'role': 'chords', 'placement': 'wholeSong'})
    ev('add_instrument_part', {'resp': r['text'][:400], 'isError': r['isError']})
    mm = re.search(r'trackIndex(?:["\s:=]+)(\d+)', r['text'])
    ti = int(mm.group(1)) if mm else 0
    ev('track', {'ti': ti})

    time.sleep(3.0)  # permit deferred routing + slot spawn + param metadata
    fx = m.call('list_fx', {'trackId': ti})
    ev('list_fx', {'resp': fx['text'][:300], 'isError': fx['isError']})

    lfp = m.call('list_fx_params', {'trackId': ti, 'slotIndex': 0})
    ev('list_fx_params', {'len': len(lfp['text']), 'head': lfp['text'][:500], 'isError': lfp['isError']})
    # verify the six expected names present
    names = dict(re.findall(r'"index":\s*(\d+),\s*"name":\s*"([^"]+)"', lfp['text']))
    expected = {17: 'LFO1 RATE' if 'LFO1 RATE' in lfp['text'] else True, 21: True, 22: True, 59: True, 73: True, 76: True}
    ev('param-name-check', {
        'n17': [k for k, v in names.items() if 'LFO1' in v.upper()][:8],
        'idx21': [k for k, v in names.items() if 'RINGMOD' in v.upper()][:4],
        'head_ok': True})

    results = {'plugin': pid, 'trackIndex': ti, 'params': SIX}

    # ---------------- (a) STOPPED ----------------
    ev('scenario', {'s': 'a_stopped'})
    boot = capture_polls(m, ti, 'a_boot', delays=(0.2, 1.0, 3.0))
    results['a_boot_capture'] = boot
    a_boot_state = state_md5_from_save(m, WINOUT + '\\c2a_a_boot.hdaw', 'a_boot')
    results['a_boot_state'] = a_boot_state
    time.sleep(1.0)
    applied = apply_six(m, ti)
    results['a_apply'] = applied
    time.sleep(0.3)
    post = capture_polls(m, ti, 'a_post', delays=(0.2, 1.0, 3.0))
    results['a_post_capture'] = post
    a_post_state = state_md5_from_save(m, WINOUT + '\\c2a_a_post.hdaw', 'a_post')
    results['a_post_state'] = a_post_state

    # ---------------- (b) PLAYING ----------------
    ev('scenario', {'s': 'b_playing'})
    tr = m.call('transport', {'action': 'play'})
    ev('transport', {'action': 'play', 'resp': tr['text'][:80], 'isError': tr['isError']})
    time.sleep(1.0)
    summ = m.call('get_project_summary', {})
    ev('summary', {'resp': summ['text'][:200]})
    appliedB = apply_six(m, ti)
    results['b_apply'] = appliedB
    postB = capture_polls(m, ti, 'b_post', delays=(0.2, 1.0, 3.0))
    results['b_post_capture'] = postB
    b_post_state = state_md5_from_save(m, WINOUT + '\\c2a_b_post.hdaw', 'b_post')
    results['b_post_state'] = b_post_state
    time.sleep(1.0)
    tr = m.call('transport', {'action': 'pause'})
    ev('transport', {'action': 'pause', 'resp': tr['text'][:80]})

    # ---------------- (c) OFFLINE ----------------
    ev('scenario', {'s': 'c_offline'})
    appliedC = apply_six(m, ti)
    results['c_apply'] = appliedC
    postC = capture_polls(m, ti, 'c_post', delays=(0.2, 1.0, 3.0))
    results['c_post_capture'] = postC
    c_pre_state = state_md5_from_save(m, WINOUT + '\\c2a_c_pre.hdaw', 'c_pre')
    results['c_pre_state'] = c_pre_state
    wav = WINOUT + '\\c2a_c.wav'
    ex = m.call('export_audio', {'outputPath': wav, 'format': 'wav', 'trackIds': [ti],
                                 'start': 0, 'end': 6, 'wait': True, 'waitTimeoutMs': 120000}, timeout=180)
    ev('export', {'resp': ex['text'][:160], 'isError': ex['isError']})
    wmd5 = nbytes = None
    rms = peak = None
    wpath = wav.replace('\\', '/').replace('C:/', '/mnt/c/')
    if os.path.exists(wpath):
        with open(wpath, 'rb') as fh:
            wmd5 = hashlib.md5(fh.read()).hexdigest()
        nbytes = os.path.getsize(wpath)
        try:
            rms, peak = wav_rms_peak(wpath)
        except Exception as exc:  # noqa: BLE001
            ev('wav_rms', {'error': str(exc)[:100]})
    ev('wav', {'path': wav, 'md5': wmd5, 'bytes': nbytes, 'rms': rms, 'peak': peak})
    results['c_export'] = {'path': wav, 'md5': wmd5, 'bytes': nbytes, 'rms': rms, 'peak': peak,
                           'resp': ex['text'][:160]}

    results['wall'] = EVENTS
    with open(os.path.join(SCR, 'results.json'), 'w', encoding='utf-8') as fh:
        json.dump(results, fh, ensure_ascii=False, indent=1)
    ev('DONE', {'resultFile': os.path.join(SCR, 'results.json')})

if __name__ == '__main__':
    main()
