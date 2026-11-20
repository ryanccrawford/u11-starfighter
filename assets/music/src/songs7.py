# Level 7: Liftoff (ground -> clouds -> space) - 120 BPM, D major, rising launch anthem
from synth import *
import subprocess, os, json, re, sys
K4 = 'x...x...x...x...'; OFFH = '..x...x...x...x.'
lift_A = ["F#5 - - - A5 - - - D6 - - - - - C#6 -", "D6 - - - - - - - A5 - - - F#5 - A5 -",
          "E5 - - - A5 - - - C#6 - - - E6 - - -", "C#6 - - - - - - - B5 - A5 - - - . .",
          "B5 - - - D6 - - - F#6 - - - E6 - D6 -", "C#6 - - - - - - - B5 - - - A5 - - -",
          "G5 - - - B5 - - - D6 - - - G6 - - -", "F#6 - - - - - - - E6 - - - D6 - - -"]
lift_B = ["B5 - - - - - D6 - - - B5 - G5 - - -", "A5 - - - B5 - - - D6 - - - - - - -",
          "C#6 - - - - - E6 - - - C#6 - A5 - - -", "B5 - - - C#6 - - - E6 - - - - - - -",
          "A5 - - - - - C#6 - - - F#6 - - - E6 -", "C#6 - - - - - - - A5 - - - F#5 - - -",
          "D6 - - - - - F#6 - - - D6 - B5 - - -", "A5 - - - - - - - C#6 - - - E6 - - -"]
for bar in lift_A + lift_B: assert len(bar.split()) == 16, bar
BASS = 'R . . R O . R . R . . R O . F .'
SN = '....x.......x...'
L7 = dict(name='level_07', bpm=120, bars=40, chord_bars=2,
  progs=[['D', 'A', 'Bm', 'G'], ['G', 'A', 'F#m', 'Bm']],
  arp_order=[0, 2, 3, 1, 2, 3, 0, 3], arp_rate=1, arp_oct=12, arp_cut=4200,
  lead_kind='saw', lead_cut=4800, pad_cut=1900, duck=0.32, bass_bright=1100, verb=0.42, verb_size=1.2, drum_verb=0.14,
  sections=[
    dict(**{'from': 0, 'to': 8}, prog=0, drums=dict(hat=lambda b: '................' if b < 4 else '..x...x...x...x.'), bass='R . . . . . . . R . . . . . . .', pad_vol=1.15, arp=True, arp_vol=0.55, riser=2),
    dict(**{'from': 8, 'to': 16}, prog=0, drums=dict(kick=K4, hat=OFFH), bass=BASS, arp=True, arp_vol=0.75),
    dict(**{'from': 16, 'to': 24}, prog=0, drums=dict(kick=K4, snare=SN, hat='x.o.x.o.x.o.x.o.'), bass=BASS, arp=True, arp_vol=0.55, lead=lift_A, stabs='x.......x.......'),
    dict(**{'from': 24, 'to': 32}, prog=1, drums=dict(kick=K4, snare=lambda b: SN if b < 31 else '....x...x.xxxxxx', hat='x.o.x.o.x.o.x.o.'), bass=BASS, arp=True, arp_vol=0.55, lead=lift_B, riser=2),
    dict(**{'from': 32, 'to': 40}, prog=0, drums=dict(kick=lambda b: K4 if b < 39 else 'x...x...x.x.x.x.', snare=lambda b: SN if b < 39 else '....x...x.x.xxxx', hat='xxo.xxo.xxo.xxo.'), bass='R R O R R R O R R R O R R R F O', arp=True, arp_vol=0.7, lead=lift_A, stabs='x.....x.....x...', pad_vol=1.2),
  ])
def lufs(path):
    out = subprocess.run(['ffmpeg', '-hide_banner', '-i', path, '-af', 'ebur128=peak=true', '-f', 'null', '-'], capture_output=True, text=True).stderr
    return float(re.findall(r'I:\s+(-?[\d.]+) LUFS', out)[-1]), float(re.findall(r'Peak:\s+(-?[\d.]+) dBFS', out)[-1])
def write_wav(path, y):
    import wave
    d = (np.clip(y, -1, 1) * 32767).astype('<i2')
    with wave.open(path, 'wb') as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR); w.writeframes(d.tobytes())
TARGET = -13.0
out = sys.argv[1] if len(sys.argv) > 1 else 'out'; os.makedirs(out, exist_ok=True)
mix, L = render(L7)
g = 0.3 / (np.sqrt(np.mean(mix ** 2)) + 1e-9)
for it in range(4):
    y = master(mix, g); wav = f'{out}/level_07.wav'; write_wav(wav, y)
    I, P = lufs(wav)
    if abs(I - TARGET) < 0.3: break
    g *= 10 ** ((TARGET - I) / 20)
print(json.dumps(dict(name='level_07', samples=L, seconds=round(L / SR, 3), lufs=I, peak_dbfs=P)))
