#!/usr/bin/env python3
"""Measure the visible nose pitch of every side_scroller frame and store it as
side_scroller.pitch_deg_by_frame (rows x cols, degrees, + = nose up) in a ship sprite JSON.
Pitch is relative to the cruise row (row 0) and averages two estimates from the real
alpha mask: the hull's principal axis, and the nozzle-midpoint -> nose-tip line.
usage: tools/measure_pitch.py assets/sprites/<ship>_pi3_sprites.json ..."""
import json, math, sys, os
import numpy as np
from PIL import Image

def measure(path):
    j = json.load(open(path)); s = j["side_scroller"]
    im = np.array(Image.open(os.path.join(os.path.dirname(path), s["file"])).convert("RGBA"))[..., 3]
    fw, fh = j["frame_size_px"]; R, C = s["rows"], s["columns"]
    def est(r, c):
        a = im[r*fh:(r+1)*fh, c*fw:(c+1)*fw] > 40; ys, xs = np.nonzero(a)
        w, v = np.linalg.eigh(np.cov(np.vstack([xs - xs.mean(), ys - ys.mean()]))); d = v[:, 1]
        pca = -((math.degrees(math.atan2(d[1], d[0])) + 90) % 180 - 90)
        m = xs >= xs.max() - 3; n = s["nozzles_by_frame"][r][c]
        mx = (n[0]["x"] + n[1]["x"]) / 2 * fw; my = (n[0]["y"] + n[1]["y"]) / 2 * fh
        tip = -math.degrees(math.atan2(ys[m].mean() - my, xs[m].mean() - mx))
        return pca, tip
    base = [est(0, c) for c in range(C)]
    out = []
    for r in range(R):
        row = []
        for c in range(C):
            p, t = est(r, c)
            row.append(round(((p - base[c][0]) + (t - base[c][1])) / 2, 1))
        out.append(row)
    s["pitch_deg_by_frame"] = out
    json.dump(j, open(path, "w"), indent=None)
    print(path, [r[0] for r in out])

for p in sys.argv[1:]: measure(p)
