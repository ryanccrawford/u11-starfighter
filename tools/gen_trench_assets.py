#!/usr/bin/env python3
"""t252 Moon Base trench run: procedural textures + sounds.
Writes assets/trench/{trench_albedo.png, trench_normal.png, *.wav}.
Atlas (1024^2, 2x2 quadrants of 512): 0 metal plating, 1 concrete slabs, 2 tech greebles / vents, 3 ribs.
Albedo carries baked AO (cavities darker); normal map is derived from the same height field."""
import numpy as np, sys, os, wave
from PIL import Image, ImageFilter
out = sys.argv[1] if len(sys.argv) > 1 else 'assets/trench'
os.makedirs(out, exist_ok=True)
rng = np.random.default_rng(252)
N = 512

def blur(a, r):
    im = Image.fromarray(np.clip(a * 255, 0, 255).astype(np.uint8)); return np.asarray(im.filter(ImageFilter.GaussianBlur(r))).astype(np.float32) / 255

def fbm(n, oct=5, base=8):
    acc = np.zeros((n, n), np.float32); amp = 1; tot = 0
    for o in range(oct):
        g = base * 2 ** o
        r = rng.random((g + 1, g + 1)).astype(np.float32); r[-1, :] = r[0, :]; r[:, -1] = r[:, 0]
        im = Image.fromarray((r * 255).astype(np.uint8)).resize((n + n // g, n + n // g), Image.BICUBIC)
        acc += amp * np.asarray(im).astype(np.float32)[:n, :n] / 255; tot += amp; amp *= 0.5
    return acc / tot

def rects(h, alb, n_split, depth, seam, minsz=24, var=0.10, rivets=True):
    """recursive panel split: each panel raised/lowered a little, seams cut, rivets in corners"""
    def split(x0, y0, x1, y1, d):
        if d == 0 or (x1 - x0 < minsz * 2 and y1 - y0 < minsz * 2) or (d < n_split - 1 and rng.random() < 0.18):
            lv = rng.uniform(-depth, depth); h[y0:y1, x0:x1] += lv
            alb[y0:y1, x0:x1] += rng.uniform(-var, var)
            h[y0:y0 + seam, x0:x1] -= 0.35; h[y0:y1, x0:x0 + seam] -= 0.35
            if rivets and x1 - x0 > 30 and y1 - y0 > 30:
                for (cx, cy) in ((x0 + 6, y0 + 6), (x1 - 6, y0 + 6), (x0 + 6, y1 - 6), (x1 - 6, y1 - 6)):
                    yy, xx = np.ogrid[-2:3, -2:3]; m = (xx * xx + yy * yy) <= 4
                    sl = h[cy - 2:cy + 3, cx - 2:cx + 3]
                    if sl.shape == m.shape: sl[m] += 0.25
            return
        if (x1 - x0) > (y1 - y0) * rng.uniform(0.7, 1.4):
            c = int(rng.uniform(0.3, 0.7) * (x1 - x0)) + x0; split(x0, y0, c, y1, d - 1); split(c, y0, x1, y1, d - 1)
        else:
            c = int(rng.uniform(0.3, 0.7) * (y1 - y0)) + y0; split(x0, y0, x1, c, d - 1); split(x0, c, x1, y1, d - 1)
    split(0, 0, N, N, n_split)

def metal():
    h = np.zeros((N, N), np.float32); a = np.full((N, N), 0.70, np.float32)
    rects(h, a, 6, 0.12, 2)
    n = fbm(N, 6, 4); a += (n - 0.5) * 0.18
    streak = blur((rng.random((N, N)) < 0.004).astype(np.float32), 0.6); streak = np.cumsum(streak, axis=0) % 1.0
    a -= 0.06 * blur(rng.random((N, N)).astype(np.float32), 1.2) * (n > 0.55)
    h += (rng.random((N, N)) - 0.5) * 0.02
    return h, a

def concrete():
    h = np.zeros((N, N), np.float32); a = np.full((N, N), 0.62, np.float32)
    for k in range(0, N, 128): h[k:k + 3, :] -= 0.5; h[:, k:k + 3] -= 0.5
    for j in range(4):
        for i in range(4):
            a[j * 128:(j + 1) * 128, i * 128:(i + 1) * 128] += rng.uniform(-0.07, 0.07)
    n = fbm(N, 6, 6); a += (n - 0.5) * 0.25
    speck = rng.random((N, N)); a -= 0.10 * (speck > 0.97); a += 0.06 * (speck < 0.02)
    h += (speck - 0.5) * 0.05
    # cracks
    for c in range(10):
        x, y = rng.uniform(0, N, 2); ang = rng.uniform(0, 6.28)
        for s in range(int(rng.uniform(40, 160))):
            ang += rng.normal(0, 0.35); x = (x + np.cos(ang)) % N; y = (y + np.sin(ang)) % N
            h[int(y), int(x)] -= 0.4; a[int(y), int(x)] -= 0.15
    stain = blur(fbm(N, 3, 3), 6); a -= 0.12 * np.clip(stain - 0.55, 0, 1) * 4
    return h, a

def tech():
    h = np.zeros((N, N), np.float32); a = np.full((N, N), 0.66, np.float32)
    rects(h, a, 4, 0.1, 2, minsz=48, var=0.06, rivets=False)
    yy, xx = np.mgrid[0:N, 0:N]
    for k in range(70):
        kind = rng.integers(0, 4); cx, cy = rng.integers(16, N - 16, 2); r = int(rng.integers(6, 30))
        x0, x1, y0, y1 = max(cx - r, 0), min(cx + r, N), max(cy - r, 0), min(cy + r, N)
        if kind == 0:   # grille: parallel slots
            box = h[y0:y1, x0:x1]; box += 0.15
            slots = (np.arange(y1 - y0) % 5 < 2)[:, None]; box -= 0.4 * slots
            a[y0:y1, x0:x1] -= 0.1 * slots
        elif kind == 1:  # round disc / hatch with bolt ring
            d = np.sqrt((xx[y0:y1, x0:x1] - cx) ** 2 + (yy[y0:y1, x0:x1] - cy) ** 2)
            h[y0:y1, x0:x1] += 0.25 * (d < r) - 0.3 * ((d > r * 0.8) & (d < r * 0.88)) - 0.25 * (d < r * 0.25)
            a[y0:y1, x0:x1] += 0.05 * (d < r)
        elif kind == 2:  # raised box
            h[y0:y1, x0:x1] += rng.uniform(0.15, 0.4); a[y0:y1, x0:x1] += rng.uniform(-0.08, 0.08)
        else:            # recessed slot
            h[y0:y1, cx - 2:cx + 2] -= 0.4
    a += (fbm(N, 5, 8) - 0.5) * 0.14
    return h, a

def ribs():
    h = np.zeros((N, N), np.float32); a = np.full((N, N), 0.68, np.float32)
    x = np.arange(N)
    prof = np.zeros(N, np.float32)
    p = 0
    while p < N:
        w = int(rng.integers(10, 40)); g = int(rng.integers(3, 10))
        prof[p:p + w] = rng.uniform(0.2, 0.6); p += w + g
    h += prof[None, :]
    for k in range(0, N, 64):   # horizontal steps (stepped ledges)
        h[k:k + 4, :] -= 0.3
    a += (prof[None, :] - 0.3) * 0.2
    a += (fbm(N, 5, 8) - 0.5) * 0.16
    return h, a

H = np.zeros((1024, 1024), np.float32); A = np.zeros((1024, 1024), np.float32)
for q, fn in enumerate((metal, concrete, tech, ribs)):
    h, a = fn()
    qx, qy = q % 2, q // 2
    H[qy * N:(qy + 1) * N, qx * N:(qx + 1) * N] = h; A[qy * N:(qy + 1) * N, qx * N:(qx + 1) * N] = a
# baked AO: cavity = local height below its blurred neighbourhood
cav = np.clip((blur(np.clip(H * 0.5 + 0.5, 0, 1), 4) - np.clip(H * 0.5 + 0.5, 0, 1)) * 6, 0, 1)
A = np.clip(A * (1 - 0.55 * cav), 0.04, 1)
# normal from height (wraps within each quadrant)
gx = np.zeros_like(H); gy = np.zeros_like(H)
for q in range(4):
    qx, qy = q % 2, q // 2; s = (slice(qy * N, (qy + 1) * N), slice(qx * N, (qx + 1) * N)); t = H[s]
    gx[s] = (np.roll(t, -1, 1) - np.roll(t, 1, 1)) * 0.5; gy[s] = (np.roll(t, -1, 0) - np.roll(t, 1, 0)) * 0.5
k = 6.0
nx, ny, nz = -gx * k, -gy * k, np.ones_like(H)
l = np.sqrt(nx * nx + ny * ny + nz * nz); nx /= l; ny /= l; nz /= l
alb = np.stack([A * 0.985, A * 0.99, A], -1)   # cool neutral grey (film: light grey)
Image.fromarray((np.clip(alb, 0, 1) * 255).astype(np.uint8)).save(f'{out}/trench_albedo.png')
Image.fromarray((np.stack([nx, ny, nz], -1) * 127.5 + 127.5).clip(0, 255).astype(np.uint8)).save(f'{out}/trench_normal.png')

# ------------------------------------------------------------------ sounds
SR = 44100
def save(name, x):
    x = np.asarray(x, np.float32); x = x / max(1e-6, np.max(np.abs(x))) * 0.89
    with wave.open(f'{out}/{name}.wav', 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR); w.writeframes((x * 32767).astype(np.int16).tobytes())
def lp(x, a):   # one-pole lowpass, a in (0,1) per sample (vectorised via filtfilt-free recursion on chunks)
    y = np.empty_like(x); acc = 0.0
    for i in range(len(x)): acc += a * (x[i] - acc); y[i] = acc
    return y
def lp_fast(x, cutoff):
    X = np.fft.rfft(x); f = np.fft.rfftfreq(len(x), 1 / SR); X *= 1 / (1 + (f / cutoff) ** 4); return np.fft.irfft(X, len(x))
def bp_fast(x, lo, hi):
    X = np.fft.rfft(x); f = np.fft.rfftfreq(len(x), 1 / SR); X *= (1 / (1 + (f / hi) ** 4)) * (1 - 1 / (1 + (f / lo) ** 4)); return np.fft.irfft(X, len(x))
t = lambda d: np.arange(int(SR * d)) / SR
# engine roar loop (periodic: built with FFT so it loops seamlessly)
d = 4.0; n = int(SR * d); ns = rng.standard_normal(n)
roar = lp_fast(ns, 380) * 3 + bp_fast(rng.standard_normal(n), 900, 2600) * 0.35
tt = t(d); roar += 0.5 * np.sin(2 * np.pi * 55 * tt) * (0.7 + 0.3 * np.sin(2 * np.pi * 0.5 * tt)) + 0.18 * np.sin(2 * np.pi * 110.25 * tt)
roar += 0.10 * np.sin(2 * np.pi * 1760 * tt + 3 * np.sin(2 * np.pi * 7.5 * tt))
save('trench_roar', roar)
# whoosh-by (doppler sweep)
d = 1.4; tt = t(d); env = np.exp(-((tt - 0.55) / 0.22) ** 2)
x = rng.standard_normal(len(tt)); seg = []
w = np.zeros_like(tt)
for i, (lo, hi) in enumerate([(2500, 6000), (1500, 4000), (900, 2500), (500, 1400), (300, 900), (200, 600), (150, 400)]):
    m = np.exp(-((tt - (0.2 + i * 0.16)) / 0.13) ** 2); w += bp_fast(x, lo, hi) * m
fr = 420 * (1 + 0.35 * np.tanh((0.55 - tt) * 8)); ph = np.cumsum(fr) / SR
w = w * env * 1.4 + 0.5 * env * np.sign(np.sin(2 * np.pi * ph)) * 0.3
save('whoosh', w)
# turret shot (heavy) / chaser shot (thin, high)
tt = t(0.42); fr = 900 * np.exp(-tt * 9) + 120; ph = np.cumsum(fr) / SR
s = np.sin(2 * np.pi * ph) * np.exp(-tt * 9) + 0.4 * bp_fast(rng.standard_normal(len(tt)), 400, 3000) * np.exp(-tt * 18)
save('turret_shot', s)
tt = t(0.28); fr = 2400 * np.exp(-tt * 14) + 300; ph = np.cumsum(fr) / SR
s = (np.sin(2 * np.pi * ph) + 0.3 * np.sign(np.sin(2 * np.pi * ph * 0.5))) * np.exp(-tt * 13)
save('chaser_shot', s)
# bomb release: clunk + falling whistle
tt = t(1.8); clunk = lp_fast(rng.standard_normal(len(tt)), 700) * np.exp(-tt * 30) * 3 + np.sin(2 * np.pi * 70 * tt) * np.exp(-tt * 14)
fr = 2200 * np.exp(-tt * 0.9) + 300; ph = np.cumsum(fr) / SR
whistle = np.sin(2 * np.pi * ph) * np.clip(tt / 0.15, 0, 1) * np.exp(-((tt - 1.0) / 0.7) ** 2) * 0.35
save('bomb_drop', clunk + whistle)
# huge rolling explosion (10 s)
d = 10.0; tt = t(d); n = len(tt); x = np.zeros(n)
for k in range(14):
    t0 = 0 if k == 0 else rng.uniform(0.1, 6.5); a = 1.0 if k == 0 else rng.uniform(0.25, 0.7)
    e = np.clip((tt - t0) / 0.03, 0, 1) * np.exp(-np.clip(tt - t0, 0, None) * rng.uniform(0.5, 1.4))
    x += a * e * lp_fast(rng.standard_normal(n), rng.uniform(150, 600))
x += 0.6 * lp_fast(rng.standard_normal(n), 60) * np.exp(-tt * 0.25)   # sub rumble
x += 0.25 * bp_fast(rng.standard_normal(n), 1500, 6000) * np.exp(-tt * 2.5)   # crackle
save('bomb_blast', np.tanh(x * 2.2))
# crash (ship into the wall)
tt = t(1.6); x = lp_fast(rng.standard_normal(len(tt)), 900) * np.exp(-tt * 3.2) + 0.5 * bp_fast(rng.standard_normal(len(tt)), 2000, 7000) * np.exp(-tt * 9)
x += 0.6 * np.sin(2 * np.pi * 52 * tt) * np.exp(-tt * 4)
save('crash', np.tanh(x * 2))
# metal scrape loop
d = 1.0; n = int(SR * d); x = bp_fast(rng.standard_normal(n), 1800, 7000); y = x.copy()
for dl in (37, 53, 89): y += 0.6 * np.roll(x, dl)
y *= 0.7 + 0.3 * np.sin(2 * np.pi * 9 * t(d)) ** 2
save('scrape', y)
# targeting computer beep
tt = t(0.11); save('target_beep', np.sin(2 * np.pi * 1320 * tt) * np.clip(tt / 0.005, 0, 1) * np.clip((0.11 - tt) / 0.02, 0, 1))
print('ok')
