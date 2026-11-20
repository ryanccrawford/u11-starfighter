from synth import *
import subprocess, os, json, re, sys

K4 = 'x...x...x...x...'
OFFH = '..x...x...x...x.'
H16 = 'xxxxxxxxxxxxxxxx'

# ---------------- Level 2: Invasion Grid (top-down) - 128 BPM, A minor, marching synth arcade
inv_leadA = ["E5 - - . A5 - - . G5 - E5 - D5 - C5 -", "D5 - E5 - - - - - . . C5 - B4 - C5 -",
             "A4 - - . C5 - - . F5 - E5 - C5 - A4 -", "C5 - - - - - - - . . A4 - C5 - D5 -",
             "E5 - - . G5 - - . C6 - B5 - G5 - E5 -", "G5 - - - - - - - . . E5 - G5 - A5 -",
             "B5 - - . A5 - - . G5 - D5 - B4 - D5 -", "D5 - - - - - - - E5 - - - - - . ."]
inv_leadB = ["A5 - - - G5 - E5 - - - D5 - E5 - - -", "C5 - - - - - - - . . . . . . . .",
             "F5 - - - E5 - C5 - - - A4 - C5 - - -", "A4 - - - - - - - . . . . . . . .",
             "D5 - F5 - A5 - - - G5 - F5 - E5 - D5 -", "F5 - - - - - - - E5 - - - D5 - - -",
             "E5 - - - G#5 - - - B5 - - - G#5 - - -", "E5 - - - - - - - . . . . . . . ."]
def inv_snare(b): return '....x.......x...' if b % 8 != 7 else '....x.......x.xx'
L2 = dict(name='level_02', bpm=128, bars=32, chord_bars=2,
  progs=[['Am', 'F', 'C', 'G'], ['Am', 'F', 'Dm', 'E']],
  arp_order=[0, 1, 2, 3, 2, 1, 0, 2], lead_kind='pulse', lead_cut=3800, pad_cut=1400, duck=0.4,
  sections=[
    dict(**{'from': 0, 'to': 8}, prog=0, drums=dict(kick=K4, hat=OFFH), bass='R . O . R . O . R . O . R R O .', pad_vol=0.8),
    dict(**{'from': 8, 'to': 16}, prog=0, drums=dict(kick=K4, hat=OFFH, snare=inv_snare), bass='R . O . R . O . R . O . R R O .', arp=True),
    dict(**{'from': 16, 'to': 24}, prog=0, drums=dict(kick=K4, hat='xxx.xxx.xxx.xxx.', snare=inv_snare), bass='R R O R R R O R R R O R R R O R', arp=True, arp_vol=0.7, lead=inv_leadA),
    dict(**{'from': 24, 'to': 32}, prog=1, drums=dict(kick=lambda b: K4 if b < 31 else 'x...x...x.x.x.x.', hat='xxx.xxx.xxx.xxx.', snare=lambda b: '....x.......x...' if b < 31 else '....x...x.xxxxxx'), bass='R R O R R R O R R R O R R R O R', arp=True, arp_vol=0.7, lead=inv_leadB, riser=2),
  ])

# ---------------- Level 3: Asteroid Run (side-scroll) - 150 BPM, D minor, breakbeat chase
ast_l1 = ["D5 - - F5 - - A5 - - - G5 - F5 - E5 -", "F5 - - - D5 - - - Bb4 - - - C5 - D5 -",
          "E5 - - G5 - - C6 - - - Bb5 - A5 - G5 -", "A5 - - - - - - - E5 - - - . . . .",
          "D6 - - C6 - - A5 - - - F5 - G5 - A5 -", "Bb5 - - - A5 - - - F5 - - - D5 - - -",
          "C5 - E5 - G5 - C6 - - - E6 - D6 - C6 -", "A5 - - - - - - - - - - - . . . ."]
ast_l2 = ["A5 - - - - - D6 - - - C6 - A5 - - -", "Bb5 - - - - - F5 - - - D5 - F5 - - -",
          "G5 - - - Bb5 - - - D6 - - - C6 - Bb5 -", "A5 - - - C#6 - - - E6 - - - C#6 - - -",
          "D6 - - - A5 - - - F5 - - - D5 - - -", "F5 - - - Bb5 - - - D6 - - - F6 - - -",
          "E6 - - - D6 - - - Bb5 - - - G5 - - -", "A5 - - - - - - - E5 - - - C#5 - E5 -"]
BK = 'x.....x...x.....'
BS = '....x.......x...'
L3 = dict(name='level_03', bpm=150, bars=40, chord_bars=1,
  progs=[['Dm', 'Bb', 'C', 'Am'], ['Dm', 'Bb', 'Gm', 'A']],
  arp_order=[0, 2, 1, 3, 2, 0, 3, 1], arp_rate=1, lead_kind='saw', lead_cut=5200, pad_cut=1800, duck=0.3, bass_bright=1300,
  sections=[
    dict(**{'from': 0, 'to': 8}, prog=0, drums=dict(kick=BK, snare=BS, hat='x.x.x.x.x.x.x.x.'), bass='R . R R . R R . R . R R . R O .'),
    dict(**{'from': 8, 'to': 16}, prog=0, drums=dict(kick=BK, snare=lambda b: BS if b % 4 != 3 else '....x.......x..x', hat=H16), bass='R . R R . R R . R . R R . R O .', arp=True),
    dict(**{'from': 16, 'to': 24}, prog=0, drums=dict(kick='x.....x...x...x.', snare=BS, hat=H16), bass='R R R O R R R O R R R O R R F O', arp=True, arp_vol=0.6, lead=ast_l1),
    dict(**{'from': 24, 'to': 32}, prog=1, drums=dict(kick='x.....x...x...x.', snare=lambda b: BS if b % 4 != 3 else '....x.......xgxx', hat=H16), bass='R R R O R R R O R R R O R R F O', arp=True, arp_vol=0.6, lead=ast_l2),
    dict(**{'from': 32, 'to': 40}, prog=1, drums=dict(kick=lambda b: '................' if b < 36 else ('x...x...x...x...' if b < 39 else 'x.x.x.x.xxxxxxxx'), snare=lambda b: '................' if b < 37 else ('....x.......x...' if b < 39 else 'x.x.x.xxxxxxxxxx'), hat=lambda b: 'x.x.x.x.x.x.x.x.' if b < 36 else H16), bass='R . . . R . . . R . . . R . . .', arp=True, riser=4, pad_vol=1.2),
  ])

# ---------------- Level 4: Fortress Approach (top-down over terrain) - 108 BPM, E minor/phrygian, war march
ft_lead2 = ["E4 - - - - - - - B4 - - - - - G4 -", "A4 - - - G4 - - - F#4 - - - E4 - - -",
            "F4 - - - - - - - C5 - - - - - A4 -", "C5 - - - Bb4 - - - A4 - - - F4 - - -",
            "G4 - - - - - - - D5 - - - - - B4 -", "D5 - - - E5 - - - F#5 - - - G5 - - -",
            "F#5 - - - - - - - D#5 - - - - - B4 -", "F#4 - - - - - - - . . . . . . . ."]
ft_lead1 = ["B4 - - - E5 - - - G5 - - - F#5 - E5 -", "D5 - - - - - - - B4 - - - - - - -",
            "C5 - - - E5 - - - G5 - - - E5 - C5 -", "G5 - - - - - - - E5 - - - - - - -",
            "A4 - - - C5 - - - E5 - - - A5 - - -", "G5 - - - F#5 - - - E5 - - - C5 - - -",
            "B4 - - - D#5 - - - F#5 - - - A5 - - -", "F#5 - - - - - - - D#5 - - - B4 - - -"]
TOMS = '1..1..2.1..1.3..'
L4 = dict(name='level_04', bpm=108, bars=32, chord_bars=2,
  progs=[['Em', 'C', 'Am', 'B'], ['Em', 'F', 'G', 'B']],
  lead_kind='brass', lead_cut=3600, lead_double=True, pad_cut=1100, duck=0.25, bass_bright=700, verb=0.45, verb_size=1.25, drum_verb=0.2,
  arp_order=[0, 1, 2, 3], arp_rate=2, arp_oct=24, arp_cut=5000,
  sections=[
    dict(**{'from': 0, 'to': 8}, prog=0, drums=dict(tom=TOMS, hat='..x...x...x...x.'), bass='R . R R R . R R R . R R R . F .', pad_vol=1.1),
    dict(**{'from': 8, 'to': 16}, prog=0, drums=dict(kick='x.......x.x.....', snare='........x.......', tom=TOMS, hat='x.x.x.x.x.x.x.x.'), bass='R . R R R . R R R . R R R . F .', stabs='x.....x.....x...'),
    dict(**{'from': 16, 'to': 24}, prog=1, drums=dict(kick='x.......x.x.....', snare='........x.......', tom=TOMS, hat='x.x.x.x.x.x.x.x.'), bass='R R R R R R R R R R R R R R F O', lead=ft_lead2, stabs='x...............'),
    dict(**{'from': 24, 'to': 32}, prog=0, drums=dict(kick='x.......x.x.....', snare=lambda b: '........x.......' if b < 31 else '........x...x.xx', tom=lambda b: TOMS if b < 31 else '1.1.2.2.3.3.2.1.', hat='x.x.x.x.x.x.x.x.'), bass='R R R R R R R R R R R R R R F O', lead=ft_lead1, arp=True, arp_vol=0.6, stabs='x.....x.....x...'),
  ])

def lufs(path):
    out = subprocess.run(['ffmpeg', '-hide_banner', '-i', path, '-af', 'ebur128=peak=true', '-f', 'null', '-'], capture_output=True, text=True).stderr
    I = float(re.findall(r'I:\s+(-?[\d.]+) LUFS', out)[-1]); P = float(re.findall(r'Peak:\s+(-?[\d.]+) dBFS', out)[-1])
    return I, P

def write_wav(path, y):
    import wave
    d = (np.clip(y, -1, 1) * 32767).astype('<i2')
    with wave.open(path, 'wb') as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR); w.writeframes(d.tobytes())

TARGET = -13.0
out = '/workspace/assetwork/music/out'; os.makedirs(out, exist_ok=True)
for song in [s for s in (L2, L3, L4) if len(sys.argv) < 2 or s['name'] in sys.argv[1:]]:
    mix, L = render(song)
    g = 0.3 / (np.sqrt(np.mean(mix ** 2)) + 1e-9)
    for it in range(4):
        y = master(mix, g); wav = f'{out}/{song["name"]}.wav'; write_wav(wav, y)
        I, P = lufs(wav)
        if abs(I - TARGET) < 0.3: break
        g *= 10 ** ((TARGET - I) / 20)
    print(json.dumps(dict(name=song['name'], samples=L, seconds=round(L / SR, 3), lufs=I, peak_dbfs=P)))
