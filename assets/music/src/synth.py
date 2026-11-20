# Original procedural music for U11 Starfighter 3D (levels 2-4). Pure numpy/scipy synthesis, no samples.
# Every loop is rendered circularly (note tails + reverb/delay wrap from the end into the start) so the
# file loops seamlessly when raylib restarts the stream.
import numpy as np, sys, json
from scipy import signal
SR = 44100
rng = np.random.default_rng(7)
PC = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}

def midi(n):
    pc = PC[n[0]]; i = 1
    if len(n) > 1 and n[1] in '#b': pc += 1 if n[1] == '#' else -1; i = 2
    return 12 * (int(n[i:]) + 1) + pc
def hz(m): return 440.0 * 2 ** ((m - 69) / 12)
def chord(name):
    i = 2 if len(name) > 1 and name[1] in '#b' else 1
    root = midi(name[:i] + '3'); minor = name[i:].startswith('m')
    return root, [0, 3, 7] if minor else [0, 4, 7]

# ---------------------------------------------------------------- oscillators / filters
def phase_from(freq, n):
    f = np.broadcast_to(np.asarray(freq, dtype=np.float64), (n,))
    dt = f / SR
    return (np.cumsum(dt) + rng.random()) % 1.0, dt
def blep_saw(freq, n):
    ph, dt = phase_from(freq, n)
    y = 2 * ph - 1
    m = ph < dt; t = ph[m] / dt[m]; y[m] -= t + t - t * t - 1
    m = ph > 1 - dt; t = (ph[m] - 1) / dt[m]; y[m] -= t * t + t + t + 1
    return y
def blep_pulse(freq, n, duty=0.5):
    ph, dt = phase_from(freq, n)
    def s(p):
        y = 2 * p - 1
        m = p < dt; t = p[m] / dt[m]; y[m] -= t + t - t * t - 1
        m = p > 1 - dt; t = (p[m] - 1) / dt[m]; y[m] -= t * t + t + t + 1
        return y
    return 0.5 * (s(ph) - s((ph + duty) % 1.0))
def lp(x, fc, order=2):
    sos = signal.butter(order, min(fc, SR * 0.45), 'low', fs=SR, output='sos'); return signal.sosfilt(sos, x)
def hp(x, fc, order=2):
    sos = signal.butter(order, fc, 'high', fs=SR, output='sos'); return signal.sosfilt(sos, x)
def bp(x, lo, hi):
    sos = signal.butter(2, [lo, hi], 'band', fs=SR, output='sos'); return signal.sosfilt(sos, x)
def fit(e, N):
    return e[:N] if len(e) >= N else np.concatenate([e, np.zeros(N - len(e))])
def adsr(n_hold, a, d, s, r):
    na, nd, nr = max(1, int(a * SR)), max(1, int(d * SR)), max(1, int(r * SR))
    n_hold = max(n_hold, 1)
    e = np.empty(n_hold + nr)
    t = np.arange(n_hold)
    e[:n_hold] = np.where(t < na, t / na, s + (1 - s) * np.exp(-(t - na) / nd))
    last = e[n_hold - 1]
    e[n_hold:] = last * np.exp(-np.arange(nr) / (nr / 5))
    return e

# ---------------------------------------------------------------- instruments (mono arrays)
def kick(level=1.0):
    n = int(0.42 * SR); t = np.arange(n) / SR
    f = 44 + 120 * np.exp(-t * 32)
    y = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * 7.5)
    y += 0.35 * hp(rng.standard_normal(n), 2500) * np.exp(-t * 350)
    return np.tanh(1.6 * y) * level
def snare(level=1.0, tone=185):
    n = int(0.3 * SR); t = np.arange(n) / SR
    y = 0.55 * np.sin(2 * np.pi * tone * t) * np.exp(-t * 22)
    y += bp(rng.standard_normal(n), 1200, 9000) * np.exp(-t * 15) * 0.9
    return y * level
def hat(open_=False, level=1.0):
    n = int((0.3 if open_ else 0.06) * SR); t = np.arange(n) / SR
    return hp(rng.standard_normal(n), 7000) * np.exp(-t * (11 if open_ else 70)) * level * 0.5
def tom(base=90, level=1.0):
    n = int(0.6 * SR); t = np.arange(n) / SR
    f = base * (1 + 0.7 * np.exp(-t * 14))
    y = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * 5.5)
    y += 0.15 * lp(rng.standard_normal(n), 3000) * np.exp(-t * 30)
    return np.tanh(1.3 * y) * level
def bass(m, dur, bright=900, level=1.0):
    n = int(dur * SR); f = hz(m)
    x = 0.7 * blep_saw(f, n + int(0.08 * SR)) + 0.5 * np.sin(2 * np.pi * f / 2 * np.arange(n + int(0.08 * SR)) / SR)
    e = fit(adsr(n, 0.004, 0.12, 0.7, 0.06), len(x))
    dark, br = lp(x, 260, 4), lp(x, bright, 4)
    fe = np.exp(-np.arange(len(x)) / (0.09 * SR))
    return np.tanh(1.4 * (dark + fe * (br - dark))) * e * level
def pad(notes, dur, cutoff=1600, level=1.0):
    n = int(dur * SR); tail = int(1.2 * SR); out = np.zeros(n + tail)
    for m in notes:
        for det in (-0.09, 0.0, 0.1):
            out += blep_saw(hz(m + det), n + tail)
    out = lp(out, cutoff, 2) * fit(adsr(n, 0.5, 1.0, 0.85, 1.1), n + tail)
    return out * level / (len(notes) * 3) * 2.2
def pluck(m, dur, level=1.0, duty=0.3, cutoff=3200):
    n = int(dur * SR); tail = int(0.15 * SR)
    x = blep_pulse(hz(m), n + tail, duty)
    e = fit(adsr(n, 0.002, 0.09, 0.25, 0.08), n + tail)
    return lp(x, cutoff, 2) * e * level
def lead(m, dur, level=1.0, cutoff=4200, vib=0.12, kind='saw'):
    n = int(dur * SR); tail = int(0.25 * SR); N = n + tail
    t = np.arange(N) / SR
    v = vib * np.sin(2 * np.pi * 5.4 * t) * np.clip((t - 0.18) / 0.25, 0, 1)
    f = hz(m) * 2 ** (v / 12)
    if kind == 'saw': x = 0.6 * blep_saw(f, N) + 0.4 * blep_saw(f * 1.004, N)
    elif kind == 'brass': x = 0.5 * blep_saw(f, N) + 0.5 * blep_saw(f * 0.997, N)
    else: x = blep_pulse(f, N, 0.5)
    e = fit(adsr(n, 0.012 if kind != 'brass' else 0.06, 0.25, 0.75, 0.22), N)
    if kind == 'brass':
        dark, br = lp(x, 500), lp(x, cutoff)
        x = dark + np.clip(e * 1.2, 0, 1) * (br - dark)
    else:
        x = lp(x, cutoff, 2)
    return x * e * level
def riser(dur, level=1.0):
    n = int(dur * SR); t = np.arange(n) / n
    x = rng.standard_normal(n); lo, hi = lp(x, 600), lp(x, 9000)
    return (lo + t ** 2 * (hi - lo)) * t ** 1.5 * level * 0.35

# ---------------------------------------------------------------- FX (block-recursive, circular)
def comb(x, D, g, damp=0.0):
    y = np.zeros_like(x)
    for k in range(0, len(x), D):
        e = min(k + D, len(x)); seg = x[k:e].copy()
        if k >= D: seg += g * y[k - D:e - D]
        y[k:e] = seg
    return lp(y, 5000) if damp else y
def allpass(x, D, g):
    y = np.zeros_like(x)
    for k in range(0, len(x), D):
        e = min(k + D, len(x)); seg = -g * x[k:e]
        if k >= D: seg += x[k - D:e - D] + g * y[k - D:e - D]
        y[k:e] = seg
    return y
def reverb(x, size=1.0, fb=0.82):
    outs = []
    for side, ds in enumerate(((1116, 1188, 1277, 1356), (1139, 1211, 1300, 1379))):
        y = sum(comb(x, int(d * size), fb) for d in ds) / 4
        y = allpass(allpass(y, 556 + side * 23, 0.5), 441 + side * 23, 0.5)
        outs.append(lp(y, 6000))
    return np.stack(outs, 1)
def circular(fx, x, L):
    """run fx on two copies of the loop and keep the second: tails wrap seamlessly"""
    y = fx(np.concatenate([x, x], 0))
    return y[L:2 * L]

# ---------------------------------------------------------------- song renderer
def render(song):
    bpm, bars = song['bpm'], song['bars']
    step = 60.0 / bpm / 4; spb = 16
    L = int(round(bars * spb * step * SR))
    buses = {k: np.zeros(L + 4 * SR) for k in ('drums', 'bass', 'pad', 'arp', 'lead', 'brass', 'fx')}
    pans = song.get('pans', {})
    kicks = []
    def put(bus, pos, y, vol=1.0):
        p = int(pos * SR); buses[bus][p:p + len(y)] += y[:len(buses[bus]) - p] * vol
    def sec_of(b):
        for s in song['sections']:
            if s['from'] <= b < s['to']: return s
    def chord_of(b):
        prog = song['progs'][sec_of(b).get('prog', 0)]
        per = song.get('chord_bars', 1)
        return prog[((b - sec_of(b)['from']) // per) % len(prog)]
    # pads: merge consecutive bars with the same chord
    b = 0
    while b < bars:
        s = sec_of(b); c = chord_of(b); e = b + 1
        while e < bars and chord_of(e) == c and sec_of(e) is s: e += 1
        if s.get('pad', True):
            root, iv = chord(c)
            notes = [root + 12 + i for i in iv] + [root + 24]
            put('pad', b * spb * step, pad(notes, (e - b) * spb * step, song.get('pad_cut', 1600)), s.get('pad_vol', 1.0))
        b = e
    for b in range(bars):
        s = sec_of(b); c = chord_of(b); root, iv = chord(c); t0 = b * spb * step
        dr = s.get('drums', {})
        for name, pat in dr.items():
            if callable(pat): pat = pat(b)
            for i, ch in enumerate(pat):
                if ch in '.- ': continue
                tt = t0 + i * step
                acc = 1.0 if ch.isupper() or ch in 'xo' else 0.7
                if name == 'kick': put('drums', tt, kick(), 0.95 * acc); kicks.append(tt)
                elif name == 'snare': put('drums', tt, snare(), 0.6 * (acc if ch != 'g' else 0.3))
                elif name == 'hat': put('drums', tt, hat(open_=(ch in 'oO')), 0.35 * (1.0 if i % 2 == 0 else 0.7))
                elif name == 'tom': put('drums', tt, tom(base={'1': 70, '2': 95, '3': 120, 'x': 85}.get(ch, 85)), 0.7)
        bp_ = s.get('bass')
        if bp_:
            toks = bp_.split()
            for i, tk in enumerate(toks):
                if tk == '.': continue
                ln = 1
                while i + ln < len(toks) and toks[i + ln] == '-': ln += 1
                off = {'R': 0, 'O': 12, 'F': 7, 'f': 7 + 12, 'T': iv[1]}[tk]
                put('bass', t0 + i * step, bass(root - 12 + off, ln * step * 0.9, song.get('bass_bright', 900)), 0.55)
        if s.get('arp'):
            tones = [root + 12 + i for i in iv] + [root + 24]
            order = song.get('arp_order', [0, 1, 2, 3, 2, 1])
            rate = song.get('arp_rate', 1)
            for i in range(0, 16, rate):
                m = tones[order[(i // rate) % len(order)]] + song.get('arp_oct', 12)
                put('arp', t0 + i * step, pluck(m, step * rate * 0.7, cutoff=song.get('arp_cut', 3200)), 0.22 * s.get('arp_vol', 1.0))
        if s.get('stabs'):
            for i, ch in enumerate(s['stabs']):
                if ch == 'x':
                    for m in [root + 12 + k for k in iv]:
                        put('brass', t0 + i * step, lead(m, step * 1.6, kind='brass', cutoff=2400, vib=0), 0.16)
        mel = s.get('lead')
        if mel:
            bar = mel[(b - s['from']) % len(mel)].split()
            for i, tk in enumerate(bar):
                if tk in '.-': continue
                ln = 1
                while i + ln < len(bar) and bar[i + ln] == '-': ln += 1
                kind = song.get('lead_kind', 'saw')
                put('lead', t0 + i * step, lead(midi(tk) + song.get('lead_shift', 0), ln * step * 0.95, kind=kind, cutoff=song.get('lead_cut', 4200)), 0.32)
                if song.get('lead_double'):
                    put('lead', t0 + i * step, lead(midi(tk) + 12 + song.get('lead_shift', 0), ln * step * 0.95, kind=kind, cutoff=3000), 0.12)
        if s.get('riser') and b == s['to'] - s['riser']:
            put('fx', t0, riser(s['riser'] * spb * step), 0.9)
    # fold tails past the loop end back to the start (circular loop)
    for k in buses:
        x = buses[k]; y = x[:L].copy(); tail = x[L:]
        y[:len(tail)] += tail; buses[k] = y
    # sidechain pump from the kick on bass/pad
    duck = np.ones(L); t = np.arange(L) / SR
    env = np.zeros(L)
    for tk in kicks:
        p = int(tk * SR); n = min(int(0.3 * SR), L - p)
        env[p:p + n] = np.maximum(env[p:p + n], np.exp(-np.arange(n) / (0.07 * SR)))
    duck = 1 - song.get('duck', 0.35) * env
    mix = np.zeros((L, 2))
    def pan(x, p):  # p -1..1
        return np.stack([x * np.sqrt((1 - p) / 2), x * np.sqrt((1 + p) / 2)], 1)
    mix += pan(buses['drums'], 0) * 1.0
    mix += pan(hp(buses['bass'], 35) * duck, 0) * 1.0
    padst = pan(buses['pad'] * duck, -0.15) + pan(np.roll(buses['pad'] * duck, int(0.011 * SR)), 0.15)
    mix += padst * 0.5
    arp = buses['arp']
    dly = int(round(3 * step * SR))
    arp_wet = circular(lambda x: comb(x, dly, 0.38) - x, arp, L)
    mix += pan(arp, -0.25) + pan(arp_wet, 0.45) * 0.5
    ld = buses['lead']
    ld_d = circular(lambda x: comb(x, dly, 0.3) - x, ld, L)
    mix += pan(ld, 0.05) + pan(ld_d, -0.3) * 0.35
    mix += pan(buses['brass'], 0.0)
    mix += pan(buses['fx'], 0)
    send = buses['lead'] * 0.6 + buses['pad'] * 0.3 + buses['arp'] * 0.4 + buses['brass'] * 0.5 + buses['drums'] * song.get('drum_verb', 0.12)
    rv = circular(lambda x: reverb(x, song.get('verb_size', 1.0)), send, L)
    mix += rv * song.get('verb', 0.35)
    mix = circular(lambda x: hp(x.T, 28).T, mix, L)
    return mix, L

def master(mix, gain):
    y = mix * gain
    # soft limiter: transparent below ~-3 dBFS, smooth above
    th = 0.7
    a = np.abs(y); over = a > th
    y[over] = np.sign(y[over]) * (th + (1 - th) * np.tanh((a[over] - th) / (1 - th)))
    return y * 0.84
