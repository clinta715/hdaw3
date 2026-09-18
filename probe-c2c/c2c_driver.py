#!/usr/bin/env python3
"""Phase C2c driver — deterministic capture-status verification.
Run against a fresh engine launched with C:\temp\launch_hdaw_diag.cmd
(HDAW_TRACE_PARAM=1 JE8086_TRACE=1 TEMP=C:\temp, MCP at 127.0.0.1:18765).

Flow:
  G1 x2 (determinism): new_project -> add JE8086 -> BOOT capture (expect unchanged)
                        -> apply 6 live params -> bake-settle 20s -> POST capture
                        (EXPECT status=ok stateBytes>0 hasPluginState=1, persisted
                         pluginState md5 differs from boot)
  G3: save pre-load (already saved) -> load_project -> settle -> capture again
                        -> save -> persisted pluginState md5 EQUALS pre-save.
Writes probe-c2c/results.json; saves projects to C:\temp.
"""
import json, os, re, sys, time, urllib.request, hashlib

URL = 'http://127.0.0.1:18765/mcp'
SCR  = '/mnt/d/pdf/roo projects/hdaw3/probe-c2c'
WINOUT = 'C:\\temp'
SETTLE = 12        # bake settle after apply (warm 800 blocks ~1.6s + idle ~2.5s + margin)
SIX = [
    (17, round(100 / 127.0, 6), 'Lfo1Rate'),
    (21, 1.0,                   'RingModulatorSwitch'),
    (22, round(90 / 127.0, 6),  'CrossModulationDepth'),
    (59, round(10 / 127.0, 6),  'CutoffFrequency'),
    (73, 1.0,                   'AmpEnvelopeAttackTime'),
    (76, round(120 / 127.0, 6), 'AmpEnvelopeReleaseTime'),
]
EVENTS = []
def ev(tag, obj):
    row = {'t': round(time.time(), 3), 'tag': tag}; row.update(obj)
    EVENTS.append(row)
    print('[%9.3f] %-26s %s' % (time.time() % 100000, tag, json.dumps(obj)[:200]), flush=True)

class Mcp:
    def __init__(self): self.n = 0
    def call(self, name, args, timeout=200, retries=3):
        last = None
        for _ in range(retries + 1):
            self.n += 1
            payload = {'jsonrpc': '2.0', 'id': self.n, 'method': 'tools/call',
                       'params': {'name': name, 'arguments': args}}
            req = urllib.request.Request(URL, data=json.dumps(payload).encode('utf-8'),
                                         headers={'Content-Type': 'application/json',
                                                  'Accept': 'application/json'})
            try:
                with urllib.request.urlopen(req, timeout=timeout) as r: body = r.read()
                if not body: return {'text': '', 'isError': True}
                resp = json.loads(body)
                if 'error' in resp: return {'text': 'RPCERROR ' + json.dumps(resp['error'])[:200], 'isError': True}
                res = resp.get('result') or {}
                txt = ''.join(c.get('text', '') for c in (res.get('content') or [])
                              if c.get('type') == 'text')
                return {'text': txt, 'isError': bool(res.get('isError'))}
            except Exception as exc:
                last = exc; time.sleep(5)
        return {'text': 'TRANSPORT %s' % last, 'isError': True}

def find_plugin_id(m):
    r = m.call('list_plugins', {'kind': 'all'})
    mm = re.search(r'"id":\s*"(CLAP-JE8086-[A-Za-z0-9]+-[0-9]+)"', r['text'])
    if mm: return mm.group(1)
    mm2 = re.search(r'(CLAP-JE8086-[A-Za-z0-9-]{3,})', r['text'])
    return mm2.group(1).rstrip('"') if mm2 else None

def capture_trigger(m, ti, si=0):
    return m.call('send_fx_midi', {'trackId': ti, 'slotIndex': si,
        'messages': [{'kind': 'controlChange', 'controller': 125, 'value': 0}],
        'captureToTree': True})

def capture_polls(m, ti, tag, si=0, delays=(0.2, 1.0, 3.0)):
    trig = capture_trigger(m, ti, si)
    ev('capture_trigger:' + tag, {'resp': trig['text'][:160], 'isError': trig['isError']})
    out = {'polls': []}
    for d in delays:
        time.sleep(max(0, d - (time.time() - EVENTS[-1]['t'])))
        st = m.call('get_fx_capture_status', {'trackId': ti, 'slotIndex': si})
        fields = dict(re.findall(r'(status|stateBytes|capturedAtMs|hasPluginState)=([^ ]+)', st['text']))
        ev('capture.poll:' + tag, {'at': d, **fields})
        out['polls'].append({'atMs': d, **fields})
        if fields.get('status') not in ('pending', None):
            break
    time.sleep(1.2)
    st = m.call('get_fx_capture_status', {'trackId': ti, 'slotIndex': si})
    fields = dict(re.findall(r'(status|stateBytes|capturedAtMs|hasPluginState)=([^ ]+)', st['text']))
    ev('capture.settled:' + tag, fields)
    out['settled'] = fields
    return out

def state_md5_from_save(m, winpath, tag):
    r = m.call('save_project', {'filePath': winpath})
    ev('save:' + tag, {'resp': r['text'], 'isError': r['isError']})
    md5 = nbytes = None
    try:
        lx = winpath.replace('\\', '/').replace('C:/', '/mnt/c/')
        with open(lx, 'r', encoding='utf-8', errors='replace') as fh: data = fh.read()
        blobs = re.findall(r'pluginState="([^"]*)"', data)
        if blobs:
            raw = blobs[-1]
            md5 = hashlib.md5(raw.encode('latin-1')).hexdigest()
            mm = re.match(r'(\d+)\.', raw)
            nbytes = int(mm.group(1)) if mm else None
    except Exception as exc:
        ev('state-extract:' + tag, {'error': str(exc)[:120]})
    ev('state:' + tag, {'md5': md5, 'bytes': nbytes, 'present': md5 is not None})
    return {'md5': md5, 'bytes': nbytes}

def apply_six(m, ti, si=0):
    applied = []
    for idx, val, name in SIX:
        r = m.call('set_fx_param', {'trackId': ti, 'slotIndex': si, 'paramIndex': idx, 'value': val})
        applied.append({'idx': idx, 'name': name, 'val': val, 'ok': (not r['isError']), 'resp': r['text'][:80]})
        ev('set_fx_param', {'idx': idx, 'name': name, 'val': val, 'resp': r['text'][:80], 'isError': r['isError']})
        time.sleep(0.05)
    return applied

def g1_round(m, pid, rep):
    ev('g1_round', {'rep': rep})
    m.call('new_project', {})
    r = m.call('add_instrument_part', {'trackName': 'C2c JE8086', 'pluginId': pid,
                                       'role': 'chords', 'placement': 'wholeSong'})
    ev('add_instrument_part', {'resp': r['text'][:200], 'isError': r['isError']})
    mm = re.search(r'trackIndex(?:["\s:=]+)(\d+)', r['text'])
    ti = int(mm.group(1)) if mm else 0
    time.sleep(4.0)  # deferred routing + slot spawn + param metadata settle
    boot = capture_polls(m, ti, 'g%d_boot' % rep)
    boot_state = state_md5_from_save(m, WINOUT + ('\\c2c_g%d_boot.hdaw' % rep), 'g%d_boot' % rep)
    applied = apply_six(m, ti)
    ev('apply_done', {'rep': rep, 'ok': all(a['ok'] for a in applied)})
    time.sleep(SETTLE)
    post = capture_polls(m, ti, 'g%d_post' % rep)
    post_state = state_md5_from_save(m, WINOUT + ('\\c2c_g%d_post.hdaw' % rep), 'g%d_post' % rep)
    gate_ok = (post['settled'].get('status') == 'ok'
               and int(post['settled'].get('stateBytes') or 0) > 0
               and post['settled'].get('hasPluginState') == '1')
    if boot_state['md5'] is not None and post_state['md5'] is not None:
        md5_diff = boot_state['md5'] != post_state['md5']
    else:
        md5_diff = boot_state['md5'] is None and post_state['md5'] is not None
    ev('g1_verdict', {'rep': rep, 'status_ok_bytes_has_state': gate_ok,
                      'md5_differs_from_boot': md5_diff})
    return {'rep': rep, 'ti': ti, 'boot': boot, 'boot_state': boot_state,
            'applied': applied, 'post': post, 'post_state': post_state,
            'verdict': {'status_ok_bytes_state': gate_ok, 'md5_differs': md5_diff}}

def g3_round(m, pid, rep, pre_path, pre_state):
    """Load pre-focused project (saved baked state) and verify md5 fidelity."""
    ev('g3_round', {'rep': rep})
    mm = re.search(r'trackIndex(?:["\s:=]+)(\d+)', pre_path['text']) if False else None
    lx = pre_path.replace('\\', '/').replace('C:/', '/mnt/c/')
    r = m.call('load_project', {'filePath': pre_path})
    ev('load_project', {'resp': r['text'], 'isError': r['isError']})
    time.sleep(8.0)  # child spawn + settle
    # track index = 0 in a single-track project
    post = capture_polls(m, 0, 'g%d_postload' % rep)
    post_state = state_md5_from_save(m, WINOUT + ('\\c2c_g%d_postload.hdaw' % rep), 'g%d_postload' % rep)
    fidelity = (pre_state['md5'] is not None and post_state['md5'] == pre_state['md5'])
    ev('g3_verdict', {'rep': rep, 'pre_md5': pre_state['md5'], 'post_md5': post_state['md5'],
                      'fidelity_ok': fidelity, 'capture_status': post['settled'].get('status')})
    return {'rep': rep, 'load_resp': r['text'], 'post': post, 'post_state': post_state,
            'fidelity_ok': fidelity}

def main():
    os.makedirs(SCR, exist_ok=True)
    m = Mcp()
    ev('ping', {})
    ri = m.call('engine_info', {})
    ev('engine_info', {'resp': ri['text'][:200], 'isError': ri['isError']})
    pid = find_plugin_id(m)
    ev('plugin', {'pid': pid})
    if not pid:
        print('FATAL: no JE8086 plugin id'); sys.exit(2)
    results = {'plugin': pid}
    g1 = [g1_round(m, pid, rep) for rep in (1, 2)]
    results['g1'] = g1
    # G3 on the LAST g1 round's saved post state
    last = g1[-1]
    pre = WINOUT + '\\c2c_g2_post.hdaw'
    g3 = g3_round(m, pid, 2, pre, last['post_state'])
    results['g3'] = g3
    results['wall'] = EVENTS
    with open(os.path.join(SCR, 'results.json'), 'w', encoding='utf-8') as fh:
        json.dump(results, fh, ensure_ascii=False, indent=1)
    ev('DONE', {'resultFile': os.path.join(SCR, 'results.json')})

if __name__ == '__main__':
    main()
