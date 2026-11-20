#!/usr/bin/env python3
"""t235: procedural SFX for the plasma lance and the Liftoff / Planetfall flight sequences.

Stdlib only (runs in WSL or on Windows Python).  Writes 16-bit 44.1 kHz stereo WAVs into
assets/sfx/ (the same folder and format as laser_fire.wav).  Fixed seeds: re-running gives the
same files.  *_loop.wav files repeat seamlessly (every periodic part has a whole number of cycles
in the loop and the noise filters are run circularly), one-shots start and end at silence.

Levels are baked in (RMS noted per file) so the in-game mix constants stay near 1; for reference
the existing SFX measure: laser_fire 0.030 RMS (x0.8 per bolt, ~5 overlapping), missile_launch 0.043
(x0.55), flamethrower_loop 0.129 (x0.72), weapon_overheat 0.040 (x0.8).

    python3 tools/gen_plasma_flight_sfx.py [assets/sfx]
"""
import math, os, random, struct, sys, wave

SR = 44100
TAU = 2 * math.pi
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'assets', 'sfx')


# ---------------------------------------------------------------- helpers
def write(name, L, R, rms_target=None, peak_max=0.92):
    n = len(L)
    if rms_target:
        rms = math.sqrt(sum(a * a + b * b for a, b in zip(L, R)) / (2 * n)) or 1e-9
        k = rms_target / rms
        L = [a * k for a in L]; R = [b * k for b in R]
    pk = max(max(abs(a) for a in L), max(abs(b) for b in R))
    if pk > peak_max:   # soft limit only the loud part (keeps the RMS target close)
        lim = lambda x: math.tanh(x / peak_max) * peak_max
        L = [lim(a) for a in L]; R = [lim(b) for b in R]
    path = os.path.join(OUT, name)
    with wave.open(path, 'wb') as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes(b''.join(struct.pack('<hh', int(max(-1, min(1, a)) * 32000), int(max(-1, min(1, b)) * 32000)) for a, b in zip(L, R)))
    rms = math.sqrt(sum(a * a + b * b for a, b in zip(L, R)) / (2 * n))
    print('%-26s %5.2fs rms %.3f peak %.3f' % (name, n / SR, rms, max(max(abs(a) for a in L), max(abs(b) for b in R))))


def noise(n, seed):
    r = random.Random(seed)
    return [r.uniform(-1, 1) for _ in range(n)]


def onepole(x, fc, circular=False):
    a = 1 - math.exp(-TAU * fc / SR); y = 0.0; out = [0.0] * len(x)
    for _ in range(2 if circular else 1):   # circular: the second pass starts from the loop's end state
        for i, v in enumerate(x):
            y += a * (v - y); out[i] = y
    return out


def biquad(x, kind, fc, q, circular=False, fc_fn=None):
    """RBJ biquad (lowpass / highpass / bandpass).  fc_fn(i) -> cutoff per sample (updated every 32)."""
    n = len(x); out = [0.0] * n
    x1 = x2 = y1 = y2 = 0.0
    def coef(f):
        f = min(max(f, 20.0), SR * 0.45)
        w = TAU * f / SR; cw = math.cos(w); al = math.sin(w) / (2 * q); a0 = 1 + al
        if kind == 'bp': b0, b1, b2 = al, 0.0, -al
        elif kind == 'hp': b0, b1, b2 = (1 + cw) / 2, -(1 + cw), (1 + cw) / 2
        else: b0, b1, b2 = (1 - cw) / 2, 1 - cw, (1 - cw) / 2
        return b0 / a0, b1 / a0, b2 / a0, -2 * cw / a0, (1 - al) / a0
    c = coef(fc)
    for _ in range(2 if circular else 1):
        for i in range(n):
            if fc_fn and (i & 31) == 0: c = coef(fc_fn(i))
            b0, b1, b2, a1, a2 = c
            v = x[i]; y = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
            x2, x1, y2, y1 = x1, v, y1, y; out[i] = y
    return out


def saw(f, n, phase=0.0):
    """polyBLEP sawtooth, f in Hz (constant)."""
    dt = f / SR; p = phase; out = [0.0] * n
    for i in range(n):
        s = 2 * p - 1
        if p < dt: t = p / dt; s -= t + t - t * t - 1
        elif p > 1 - dt: t = (p - 1) / dt; s -= t * t + t + t + 1
        out[i] = s; p += dt
        if p >= 1: p -= 1
    return out


def crackle(n, rate, seed, decay_ms=(2, 8), circular=True, amp=(0.4, 1.0)):
    """Random decaying pops; with circular=True pops near the end wrap round to the start."""
    r = random.Random(seed); out = [0.0] * n
    for _ in range(int(rate * n / SR)):
        t0 = r.randrange(n); a = r.uniform(*amp) * (1 if r.random() < 0.5 else -1)
        tau = r.uniform(*decay_ms) * SR / 1000; f = r.uniform(1500, 6000)
        for k in range(int(tau * 5)):
            j = t0 + k
            if j >= n:
                if not circular: break
                j -= n
            out[j] += a * math.exp(-k / tau) * math.sin(TAU * f * k / SR)
    return out


def env_fade(x, fin=0.004, fout=0.02):
    n = len(x); a = int(fin * SR); b = int(fout * SR)
    for i in range(min(a, n)): x[i] *= i / a
    for i in range(min(b, n)): x[n - 1 - i] *= i / b
    return x


def ss(a, b, x):
    x = min(1.0, max(0.0, (x - a) / (b - a))); return x * x * (3 - 2 * x)


# ---------------------------------------------------------------- plasma lance
def plasma_beam_loop():
    """2 s: the beam's hum (detuned 98/98.5 Hz saws, 24 Hz throb), a 1174 Hz lance whine with a 6 Hz
    vibrato, a hot electric crackle and a low roar.  The game raises its playback rate with heat."""
    n = 2 * SR
    s1, s2, s3 = saw(98.0, n), saw(98.5, n, 0.37), saw(196.0, n, 0.11)
    body = [0.0] * n
    for i in range(n):
        t = i / SR
        throb = 0.72 + 0.28 * math.sin(TAU * 24 * t)
        whine = math.sin(TAU * 1174 * t + 2.2 * math.sin(TAU * 6 * t))
        body[i] = (0.34 * s1[i] + 0.26 * s2[i] + 0.12 * s3[i]) * throb + 0.05 * whine
    body = biquad(body, 'lp', 2600, 0.8, circular=True)
    nz = noise(n, 11)
    roar = biquad(nz, 'lp', 380, 0.7, circular=True)
    hiss = biquad(nz[::-1], 'bp', 4200, 1.2, circular=True)
    cr = crackle(n, 70, 12, (1.5, 5))
    g = noise(n, 13); gate = onepole([1.0 if v > 0.55 else 0.0 for v in g], 35, circular=True)   # sputtering hiss bursts
    L = [0.0] * n; R = [0.0] * n
    for i in range(n):
        v = body[i] + 0.9 * roar[i] + 0.25 * hiss[i] * (0.3 + 1.4 * gate[i]) + 0.12 * cr[i]
        L[i] = v; R[i] = v * 0.94 + 0.06 * hiss[(i + 700) % n]
    write('plasma_beam_loop.wav', L, R, rms_target=0.075)


def plasma_strain_loop():
    """2 s: the overheat strain - a tense 1567/2349 Hz whine with a 12 Hz tremolo and band hiss.
    Faded in by heat (over ~35 %), so the lance sounds more and more stressed toward overheat."""
    n = 2 * SR; nz = biquad(noise(n, 21), 'bp', 3100, 3.0, circular=True)
    L = [0.0] * n; R = [0.0] * n
    for i in range(n):
        t = i / SR; trem = 0.6 + 0.4 * math.sin(TAU * 12 * t)
        v = (0.5 * math.sin(TAU * 1567 * t) + 0.3 * math.sin(TAU * 2349 * t + 0.6 * math.sin(TAU * 7 * t))) * trem + 0.6 * nz[i]
        L[i] = v; R[i] = 0.9 * v + 0.1 * nz[(i + 333) % n]
    write('plasma_strain_loop.wav', L, R, rms_target=0.040)


def plasma_sizzle_loop():
    """1 s: frying sizzle where the beam touches a hull - dense pops over a bright hiss."""
    n = SR; nz = noise(n, 31)
    hiss = biquad(nz, 'hp', 2500, 0.7, circular=True)
    band = biquad(nz, 'bp', 6500, 1.5, circular=True)
    cr = crackle(n, 260, 32, (0.6, 3.0))
    flut = onepole(noise(n, 33), 25, circular=True)
    L = [0.0] * n; R = [0.0] * n
    for i in range(n):
        v = (0.5 * hiss[i] + 0.5 * band[i]) * (0.6 + 2.0 * abs(flut[i])) + 0.35 * cr[i]
        L[i] = v; R[i] = 0.92 * v + 0.08 * hiss[(i + 911) % n]
    write('plasma_sizzle_loop.wav', L, R, rms_target=0.050)


def plasma_overheat():
    """1.2 s one-shot at lockout: a three-beep rising alarm, then a pressurised vent burst + thump."""
    n = int(1.2 * SR); L = [0.0] * n; R = [0.0] * n
    beeps = [(0.00, 990), (0.11, 1320), (0.22, 1760)]
    for t0, f in beeps:
        a = int(t0 * SR)
        for k in range(int(0.085 * SR)):
            t = k / SR; e = min(1, k / 80) * min(1, (int(0.085 * SR) - k) / 200)
            v = (math.sin(TAU * f * t) + 0.33 * math.sin(3 * TAU * f * t) + 0.2 * math.sin(5 * TAU * f * t)) * 0.55 * e
            L[a + k] += v; R[a + k] += v
    nz = noise(n, 41); t_v = 0.30
    vent = biquad(nz, 'lp', 6000, 0.9, fc_fn=lambda i: 7000 * math.exp(-max(0, i / SR - t_v) / 0.35) + 500)
    for i in range(n):
        t = i / SR - t_v
        if t < 0: continue
        e = min(1, t / 0.012) * math.exp(-t / 0.32)
        thump = math.sin(TAU * (70 - 30 * min(1, t / 0.15)) * t) * math.exp(-t / 0.09)
        L[i] += 1.1 * vent[i] * e + 0.7 * thump; R[i] += 1.1 * vent[(i + 157) % n] * e + 0.7 * thump
    write('plasma_overheat.wav', env_fade(L), env_fade(R), rms_target=0.085)


def plasma_cooling():
    """1.5 s one-shot during the lockout: a decaying cooling hiss with metal ticks (contracting)."""
    n = int(1.5 * SR); nz = noise(n, 51)
    hiss = biquad(nz, 'hp', 3000, 0.7); body = biquad(noise(n, 52), 'bp', 1200, 0.8)
    r = random.Random(53); ticks = [0.0] * n
    for _ in range(7):
        t0 = int(r.uniform(0.2, 1.35) * SR); f = r.uniform(2600, 4600); a = r.uniform(0.25, 0.5)
        for k in range(int(0.06 * SR)):
            if t0 + k < n: ticks[t0 + k] += a * math.sin(TAU * f * k / SR) * math.exp(-k / (0.012 * SR))
    L = [0.0] * n; R = [0.0] * n
    for i in range(n):
        t = i / SR; e = (1 - t / 1.5) ** 1.6 * min(1, t / 0.05) * (0.85 + 0.15 * math.sin(TAU * 7 * t))
        L[i] = (0.8 * hiss[i] + 0.3 * body[i]) * e + 0.4 * ticks[i]; R[i] = (0.8 * hiss[(i + 401) % n] + 0.3 * body[i]) * e + 0.4 * ticks[i]
    write('plasma_cooling.wav', env_fade(L), env_fade(R, fout=0.05), rms_target=0.040)


# ---------------------------------------------------------------- flight (Liftoff / Planetfall)
def rocket_roar_loop():
    """3 s: deep rocket roar - sub rumble, a mid crackle (the rocket 'rattle') and a 37/41 Hz growl."""
    n = 3 * SR; nz = noise(n, 61)
    sub = biquad(nz, 'lp', 140, 0.9, circular=True)
    mid = biquad(noise(n, 62), 'bp', 700, 0.6, circular=True)
    rat = onepole(noise(n, 63), 18, circular=True)
    cr = crackle(n, 90, 64, (3, 10))
    L = [0.0] * n; R = [0.0] * n
    for i in range(n):
        t = i / SR
        growl = 0.25 * math.sin(TAU * 37 * t) + 0.18 * math.sin(TAU * 41 * t + 1.0)
        v = 2.4 * sub[i] + mid[i] * (0.5 + 3.0 * abs(rat[i])) * 0.8 + growl + 0.08 * cr[i]
        L[i] = v; R[i] = 2.4 * sub[(i + 2205) % n] + mid[(i + 1300) % n] * (0.5 + 3.0 * abs(rat[i])) * 0.8 + growl + 0.08 * cr[(i + 999) % n]
    write('rocket_roar_loop.wav', L, R, rms_target=0.120)


def wind_loop():
    """4 s: atmospheric wind - two band-passed noise streams whose centres drift (periodic over 4 s)."""
    n = 4 * SR
    a = biquad(noise(n, 71), 'bp', 600, 0.9, circular=True, fc_fn=lambda i: 600 + 350 * math.sin(TAU * 0.5 * i / SR) + 150 * math.sin(TAU * 1.25 * i / SR))
    b = biquad(noise(n, 72), 'bp', 1500, 1.4, circular=True, fc_fn=lambda i: 1500 + 600 * math.sin(TAU * 0.25 * i / SR + 1.3))
    low = biquad(noise(n, 73), 'lp', 220, 0.7, circular=True)
    L = [0.0] * n; R = [0.0] * n
    for i in range(n):
        t = i / SR; gust = 0.75 + 0.25 * math.sin(TAU * 0.75 * t) * math.sin(TAU * 0.25 * t)
        L[i] = (a[i] + 0.5 * b[i] + 1.2 * low[i]) * gust; R[i] = (a[(i + 5000) % n] + 0.5 * b[(i + 9000) % n] + 1.2 * low[(i + 3000) % n]) * gust
    write('wind_loop.wav', L, R, rms_target=0.090)


def entry_plasma_loop():
    """3 s: atmospheric-entry plasma - harsh fizz, dense crackle, a flame-like flutter over a roar."""
    n = 3 * SR; nz = noise(n, 81)
    fizz = biquad(nz, 'bp', 2400, 0.7, circular=True)
    roar = biquad(noise(n, 82), 'lp', 300, 0.8, circular=True)
    flut = onepole(noise(n, 83), 40, circular=True)
    cr = crackle(n, 180, 84, (1, 6))
    L = [0.0] * n; R = [0.0] * n
    for i in range(n):
        v = fizz[i] * (0.4 + 2.5 * abs(flut[i])) + 1.6 * roar[i] + 0.3 * cr[i]
        L[i] = v; R[i] = fizz[(i + 4410) % n] * (0.4 + 2.5 * abs(flut[(i + 2000) % n])) + 1.6 * roar[(i + 1000) % n] + 0.3 * cr[(i + 7000) % n]
    write('entry_plasma_loop.wav', L, R, rms_target=0.100)


def space_drone_loop():
    """4 s: low space hum for the side view / approach - 55 Hz + fifths, slow beating, faint air."""
    n = 4 * SR; air = biquad(noise(n, 91), 'lp', 500, 0.7, circular=True)
    L = [0.0] * n; R = [0.0] * n
    for i in range(n):
        t = i / SR
        v = (0.45 * math.sin(TAU * 55 * t) + 0.3 * math.sin(TAU * 55.5 * t) + 0.22 * math.sin(TAU * 82.5 * t)
             + 0.12 * math.sin(TAU * 110.25 * t)) * (0.85 + 0.15 * math.sin(TAU * 0.25 * t))
        L[i] = v + 0.6 * air[i]; R[i] = v + 0.6 * air[(i + 7777) % n]
    write('space_drone_loop.wav', L, R, rms_target=0.050)


def liftoff_ignition():
    """6 s one-shot on the pad: igniter pops, the ignition whump, a turbine spool-up 260 -> 1250 Hz
    with rising rumble, settling (by ~5 s) into the steady roar loop the game fades in under it."""
    n = 6 * SR; L = [0.0] * n; R = [0.0] * n
    cr = crackle(n, 0, 0)   # empty
    r = random.Random(101)
    for _ in range(14):   # igniter pops in the first 0.35 s
        t0 = int(r.uniform(0.0, 0.35) * SR); tau = r.uniform(2, 6) * SR / 1000; f = r.uniform(1800, 4000); a = r.uniform(0.3, 0.8)
        for k in range(int(tau * 5)): cr[t0 + k] += a * math.exp(-k / tau) * math.sin(TAU * f * k / SR)
    nz = noise(n, 102)
    rumble = biquad(nz, 'lp', 160, 0.9)
    roar = biquad(noise(n, 103), 'bp', 600, 0.6)
    whump = biquad(noise(n, 104), 'lp', 900, 0.9, fc_fn=lambda i: 2500 * math.exp(-max(0, i / SR - 0.35) / 0.25) + 200)
    ph = 0.0; ph2 = 0.0
    for i in range(n):
        t = i / SR
        f = 260 + 990 * ss(0.4, 3.0, t); ph += f / SR; ph2 += 1.5 * f / SR
        spool = (math.sin(TAU * ph) + 0.35 * math.sin(TAU * ph2)) * 0.22 * ss(0.3, 1.0, t) * (1 - 0.6 * ss(3.2, 5.8, t))
        tw = t - 0.35
        wh = (whump[i] * 2.2 * min(1, tw / 0.01) * math.exp(-tw / 0.35) + 0.9 * math.sin(TAU * 48 * tw) * math.exp(-tw / 0.18)) if tw > 0 else 0
        body = (3.0 * rumble[i] + 1.1 * roar[i]) * ss(0.35, 2.6, t) * (1 - 0.5 * ss(4.0, 6.0, t))
        v = cr[i] + wh + spool + body
        L[i] = v; R[i] = v * 0.95 + 0.05 * roar[(i + 3000) % n]
    write('liftoff_ignition.wav', env_fade(L), env_fade(R, fout=0.3), rms_target=0.110)


def cloud_whoosh():
    """2.5 s one-shot: punching through a cloud layer - a swelling, sweeping band of air, L -> R."""
    n = int(2.5 * SR)
    sweep = lambda i: 400 + 2600 * math.sin(math.pi * min(1, i / n)) ** 2
    a = biquad(noise(n, 111), 'bp', 800, 1.0, fc_fn=sweep)
    low = biquad(noise(n, 112), 'lp', 250, 0.8)
    L = [0.0] * n; R = [0.0] * n
    for i in range(n):
        t = i / SR; e = ss(0.0, 0.7, t) * (1 - ss(0.9, 2.5, t)) ** 1.3
        p = min(1, t / 2.0); v = (a[i] + 1.4 * low[i]) * e
        L[i] = v * math.cos(p * math.pi / 2) * 1.3; R[i] = v * math.sin(p * math.pi / 2 + 0.25) * 1.3
    write('cloud_whoosh.wav', env_fade(L), env_fade(R), rms_target=0.080)


if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    for fn in (plasma_beam_loop, plasma_strain_loop, plasma_sizzle_loop, plasma_overheat, plasma_cooling,
               rocket_roar_loop, wind_loop, entry_plasma_loop, space_drone_loop, liftoff_ignition, cloud_whoosh):
        fn()
